#include "storage.h"
#include "storage_test.h"

#include <cloud/storage/core/libs/common/timer.h>

#include <library/cpp/testing/unittest/registar.h>

#include <util/generic/list.h>

namespace NCloud::NBlockStore {

using namespace NThreading;

////////////////////////////////////////////////////////////////////////////////
namespace {

class TTickOnGetNowTimer: public ITimer
{
public:
    using TCallback = std::function<void()>;

private:
    TInstant CurrentTime;
    const TDuration Step;
    TList<TCallback> CallbackList;

public:
    explicit TTickOnGetNowTimer(TDuration step)
        : Step(step)
    {}

    TInstant Now() override
    {
        if (!CallbackList.empty()) {
            auto callback = std::move(CallbackList.front());
            callback();
            CallbackList.pop_front();
        }
        CurrentTime += Step;
        return CurrentTime;
    }

    void AddOnTickCallback(TCallback callback)
    {
        CallbackList.push_back(std::move(callback));
    }
};

}   // namespace

////////////////////////////////////////////////////////////////////////////////

Y_UNIT_TEST_SUITE(TStorageTest)
{
    void ShouldHandleNonNormalizedRequests(ui32 requestBlockSize)
    {
        auto storage = std::make_shared<TTestStorage>();
        storage->WriteBlocksLocalHandler = [] (auto ctx, auto request) {
            Y_UNUSED(ctx);

            UNIT_ASSERT_VALUES_EQUAL(1_MB / 4_KB, request->BlocksCount);
            UNIT_ASSERT_VALUES_EQUAL(4_KB, request->BlockSize);

            auto guard = request->Sglist.Acquire();
            UNIT_ASSERT(guard);

            const auto& sgList = guard.Get();
            UNIT_ASSERT_VALUES_EQUAL(1, sgList.size());
            UNIT_ASSERT_VALUES_EQUAL(1_MB, sgList[0].Size());
            UNIT_ASSERT_VALUES_EQUAL(1_MB, Count(sgList[0].AsStringBuf(), 'X'));

            return MakeFuture<NProto::TWriteBlocksLocalResponse>();
        };

        auto adapter = std::make_shared<TStorageAdapter>(
            storage,
            4_KB,   // storageBlockSize
            false,  // normalize
            32_MB); // maxRequestSize

        auto request = std::make_shared<NProto::TWriteBlocksRequest>();
        request->SetStartIndex(1000);

        auto& iov = *request->MutableBlocks();
        auto& buffers = *iov.MutableBuffers();
        auto& buffer = *buffers.Add();
        buffer.ReserveAndResize(1_MB);
        memset(const_cast<char*>(buffer.data()), 'X', buffer.size());

        auto future = adapter->WriteBlocks(
            Now(),
            MakeIntrusive<TCallContext>(),
            std::move(request),
            requestBlockSize);

        auto response = future.GetValue(TDuration::Seconds(5));
        UNIT_ASSERT(!HasError(response));
    }

    Y_UNIT_TEST(ShouldHandleNonNormalizedRequests4K)
    {
        ShouldHandleNonNormalizedRequests(4_KB);
    }

    Y_UNIT_TEST(ShouldHandleNonNormalizedRequests8K)
    {
        ShouldHandleNonNormalizedRequests(8_KB);
    }

    void ShouldNormalizeRequests(ui32 requestBlockSize)
    {
        auto storage = std::make_shared<TTestStorage>();
        storage->WriteBlocksLocalHandler = [] (auto ctx, auto request) {
            Y_UNUSED(ctx);

            UNIT_ASSERT_VALUES_EQUAL(1_MB / 4_KB, request->BlocksCount);
            UNIT_ASSERT_VALUES_EQUAL(4_KB, request->BlockSize);

            auto guard = request->Sglist.Acquire();
            UNIT_ASSERT(guard);

            const auto& sgList = guard.Get();
            UNIT_ASSERT_VALUES_EQUAL(1_MB / 4_KB, sgList.size());
            for (auto b: sgList) {
                UNIT_ASSERT_VALUES_EQUAL(4_KB, b.Size());
                UNIT_ASSERT_VALUES_EQUAL(4_KB, Count(b.AsStringBuf(), 'X'));
            }

            return MakeFuture<NProto::TWriteBlocksLocalResponse>();
        };

        auto adapter = std::make_shared<TStorageAdapter>(
            storage,
            4_KB,   // storageBlockSize
            true,   // normalize
            32_MB); // maxRequestSize

        auto request = std::make_shared<NProto::TWriteBlocksRequest>();
        request->SetStartIndex(1000);

        auto& iov = *request->MutableBlocks();
        auto& buffers = *iov.MutableBuffers();
        auto& buffer = *buffers.Add();
        buffer.ReserveAndResize(1_MB);
        memset(const_cast<char*>(buffer.data()), 'X', buffer.size());

        auto future = adapter->WriteBlocks(
            Now(),
            MakeIntrusive<TCallContext>(),
            std::move(request),
            requestBlockSize);

        auto response = future.GetValue(TDuration::Seconds(5));
        UNIT_ASSERT(!HasError(response));
    }

    Y_UNIT_TEST(ShouldNormalizeRequests4K)
    {
        ShouldNormalizeRequests(4_KB);
    }

    Y_UNIT_TEST(ShouldNormalizeRequests8K)
    {
        ShouldNormalizeRequests(8_KB);
    }

    template <typename F>
    void DoShouldHandleTimedOutRequests(F runRequest)
    {
        constexpr TDuration WaitTimeout = TDuration::Seconds(2);

        auto storage = std::make_shared<TTestStorage>();
        storage->ReadBlocksLocalHandler = [&](auto ctx, auto request) {
            auto promise = NewPromise<NProto::TReadBlocksLocalResponse>();
            Y_UNUSED(ctx);
            Y_UNUSED(request);
            return promise;
        };
        storage->WriteBlocksLocalHandler = [&](auto ctx, auto request) {
            auto promise = NewPromise<NProto::TWriteBlocksLocalResponse>();
            Y_UNUSED(ctx);
            Y_UNUSED(request);
            return promise;
        };
        storage->ZeroBlocksHandler = [&](auto ctx, auto request) {
            auto promise = NewPromise<NProto::TZeroBlocksResponse>();
            Y_UNUSED(ctx);
            Y_UNUSED(request);
            return promise;
        };

        auto adapter = std::make_shared<TStorageAdapter>(
            storage,
            4_KB,                   // storageBlockSize
            false,                  // normalize
            32_MB,                  // maxRequestSize
            TDuration::Seconds(1)   // timeout
        );

        auto now = TInstant::Seconds(1);

        // Run request.
        auto future = runRequest(adapter, now);

        // Assert request is not handled.
        bool responseReady = future.Wait(WaitTimeout);
        UNIT_ASSERT(!responseReady);
        UNIT_ASSERT_VALUES_EQUAL(0, storage->ErrorCount);

        // Skip more time than IO timeout.
        now += TDuration::Seconds(5);

        // Assert request finished with IO error.
        adapter->CheckIOTimeouts(now);
        UNIT_ASSERT_VALUES_EQUAL(1, storage->ErrorCount);
        responseReady = future.Wait(TDuration());
        UNIT_ASSERT(responseReady);
        auto response = future.GetValue(TDuration());
        UNIT_ASSERT(HasError(response));
        UNIT_ASSERT_VALUES_EQUAL(E_IO, response.GetError().GetCode());
    }

    Y_UNIT_TEST(ShouldHandleTimedOutReadRequests)
    {
        auto runRequest =
            [](std::shared_ptr<TStorageAdapter> adapter, TInstant now)
        {
            auto request = std::make_shared<NProto::TReadBlocksLocalRequest>();
            request->SetStartIndex(1000);
            request->SetBlocksCount(1);

            return adapter->ReadBlocks(
                now,
                MakeIntrusive<TCallContext>(),
                std::move(request),
                4096);
        };
        DoShouldHandleTimedOutRequests(runRequest);
    }

    Y_UNIT_TEST(ShouldHandleTimedOutWriteRequests)
    {
        auto runRequest =
            [](std::shared_ptr<TStorageAdapter> adapter, TInstant now)
        {
            auto request = std::make_shared<NProto::TWriteBlocksLocalRequest>();
            request->SetStartIndex(1000);
            auto& iov = *request->MutableBlocks();
            auto& buffers = *iov.MutableBuffers();
            auto& buffer = *buffers.Add();
            buffer.ReserveAndResize(1_MB);
            memset(const_cast<char*>(buffer.data()), 'X', buffer.size());

            return adapter->WriteBlocks(
                now,
                MakeIntrusive<TCallContext>(),
                std::move(request),
                4096);
        };
        DoShouldHandleTimedOutRequests(runRequest);
    }

    Y_UNIT_TEST(ShouldHandleTimeedOutZeroRequests)
    {
        auto runRequest =
            [](std::shared_ptr<TStorageAdapter> adapter, TInstant now)
        {
            auto request = std::make_shared<NProto::TZeroBlocksRequest>();
            request->SetStartIndex(1000);
            request->SetBlocksCount(1);

            return adapter->ZeroBlocks(
                now,
                MakeIntrusive<TCallContext>(),
                std::move(request),
                4096);
        };
        DoShouldHandleTimedOutRequests(runRequest);
    }

    Y_UNIT_TEST(ShouldWaitForRequestsToCompleteOnShutdown)
    {
        constexpr TDuration waitTimeout = TDuration::Seconds(1);
        constexpr TDuration shutdownTimeout = TDuration::Seconds(10);
        auto fastTimer =
            std::make_shared<TTickOnGetNowTimer>(TDuration::Seconds(1));

        auto storage = std::make_shared<TTestStorage>();
        auto readPromise = NewPromise<NProto::TReadBlocksLocalResponse>();
        auto writePromise = NewPromise<NProto::TWriteBlocksLocalResponse>();
        auto zeroPromise = NewPromise<NProto::TZeroBlocksResponse>();
        storage->ReadBlocksLocalHandler = [&](auto ctx, auto request) {
            Y_UNUSED(ctx);
            Y_UNUSED(request);
            return readPromise;
        };
        storage->WriteBlocksLocalHandler = [&](auto ctx, auto request) {
            Y_UNUSED(ctx);
            Y_UNUSED(request);
            return writePromise;
        };
        storage->ZeroBlocksHandler = [&](auto ctx, auto request) {
            Y_UNUSED(ctx);
            Y_UNUSED(request);
            return zeroPromise;
        };

        auto adapter = std::make_shared<TStorageAdapter>(
            storage,
            4_KB,                    // storageBlockSize
            false,                   // normalize
            32_MB,                   // maxRequestSize
            TDuration::Seconds(1),   // request timeout
            shutdownTimeout          // shutdown timeout
        );

        auto now = fastTimer->Now();

        {   // Run Read request.
            auto request = std::make_shared<NProto::TReadBlocksLocalRequest>();
            request->SetStartIndex(1000);
            request->SetBlocksCount(1);

            auto response = adapter->ReadBlocks(
                now,
                MakeIntrusive<TCallContext>(),
                std::move(request),
                4096);

            // Assert request is not handled.
            bool responseReady = response.Wait(waitTimeout);
            UNIT_ASSERT(!responseReady);
        }
        {   // Run Write request.
            auto request = std::make_shared<NProto::TWriteBlocksLocalRequest>();
            request->SetStartIndex(1000);
            auto& iov = *request->MutableBlocks();
            auto& buffers = *iov.MutableBuffers();
            auto& buffer = *buffers.Add();
            buffer.ReserveAndResize(1_MB);
            memset(const_cast<char*>(buffer.data()), 'X', buffer.size());

            auto response = adapter->WriteBlocks(
                now,
                MakeIntrusive<TCallContext>(),
                std::move(request),
                4096);

            // Assert request is not handled.
            bool responseReady = response.Wait(waitTimeout);
            UNIT_ASSERT(!responseReady);
        }
        {
            // Run Zero request.
            auto request = std::make_shared<NProto::TZeroBlocksRequest>();
            request->SetStartIndex(1000);
            request->SetBlocksCount(1);
            auto response = adapter->ZeroBlocks(
                now,
                MakeIntrusive<TCallContext>(),
                std::move(request),
                4096);

            // Assert request is not handled.
            bool responseReady = response.Wait(waitTimeout);
            UNIT_ASSERT(!responseReady);
        }

        // Assert that there are 3 incomplete operations left on shutdown.
        UNIT_ASSERT_VALUES_EQUAL(3, adapter->Shutdown(fastTimer));
        // Assert that the time has passed more than the shutdown timeout.
        UNIT_ASSERT_LT(now + shutdownTimeout, fastTimer->Now());

        now = fastTimer->Now();
        // We will complete one request for every fastTimer->Now() call. Since
        // these calls will occur in the Shutdown(), we will simulate the
        // successful waiting for requests to be completed during Shutdown().
        fastTimer->AddOnTickCallback(
            [&]() { readPromise.SetValue(NProto::TReadBlocksResponse{}); });
        fastTimer->AddOnTickCallback(
            [&]() { writePromise.SetValue(NProto::TWriteBlocksResponse{}); });
        fastTimer->AddOnTickCallback(
            [&]() { zeroPromise.SetValue(NProto::TZeroBlocksResponse{}); });

        // Assert that all requests completed during shutdown.
        UNIT_ASSERT_VALUES_EQUAL(0, adapter->Shutdown(fastTimer));

        // Assert that the time has passed less than the shutdown timeout.
        UNIT_ASSERT_GT(now + shutdownTimeout, fastTimer->Now());
    }
}

}   // namespace NCloud::NBlockStore
