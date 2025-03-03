#include "disk_agent_actor.h"

#include "disk_agent.h"

#include <cloud/blockstore/libs/common/block_checksum.h>
#include <cloud/blockstore/libs/common/iovector.h>
#include <cloud/blockstore/libs/diagnostics/critical_events.h>
#include <cloud/blockstore/libs/storage/disk_agent/testlib/test_env.h>
#include <cloud/blockstore/libs/storage/model/composite_id.h>

#include <library/cpp/lwtrace/all.h>
#include <library/cpp/monlib/dynamic_counters/counters.h>
#include <library/cpp/protobuf/util/pb_io.h>
#include <library/cpp/testing/gmock_in_unittest/gmock.h>

#include <util/folder/tempdir.h>

namespace NCloud::NBlockStore::NStorage {

using namespace NActors;
using namespace NKikimr;
using namespace NServer;
using namespace NThreading;

using namespace NDiskAgentTest;

namespace {

////////////////////////////////////////////////////////////////////////////////

const TDuration WaitTimeout = TDuration::Seconds(5);

////////////////////////////////////////////////////////////////////////////////

NLWTrace::TQuery QueryFromString(const TString& text)
{
    TStringInput in(text);

    NLWTrace::TQuery query;
    ParseFromTextFormat(in, query);
    return query;
}

TFsPath TryGetRamDrivePath()
{
    auto p = GetRamDrivePath();
    return !p
        ? GetSystemTempDir()
        : p;
}

}   // namespace

////////////////////////////////////////////////////////////////////////////////

Y_UNIT_TEST_SUITE(TDiskAgentTest)
{
    Y_UNIT_TEST(ShouldRegisterDevicesOnStartup)
    {
        TTestBasicRuntime runtime;

        auto env = TTestEnvBuilder(runtime)
            .With(DiskAgentConfig()
                | WithBackend(NProto::DISK_AGENT_BACKEND_SPDK)
                | WithMemoryDevices({
                    MemoryDevice("MemoryDevice1") | WithPool("memory"),
                    MemoryDevice("MemoryDevice2")
                })
                | WithFileDevices({
                    FileDevice("FileDevice3"),
                    FileDevice("FileDevice4") | WithPool("local-ssd")
                })
                | WithNVMeDevices({
                    NVMeDevice("nvme1", {"NVMeDevice5"}) | WithPool("nvme"),
                    NVMeDevice("nvme2", {"NVMeDevice6"})
                }))
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        const auto& devices = env.DiskRegistryState->Devices;
        UNIT_ASSERT_VALUES_EQUAL(6, devices.size());

        // common properties
        for (auto& [uuid, device]: devices) {
            UNIT_ASSERT_VALUES_EQUAL(uuid, device.GetDeviceUUID());
            UNIT_ASSERT_VALUES_EQUAL(uuid, device.GetDeviceName());
            UNIT_ASSERT_VALUES_EQUAL("the-rack", device.GetRack());
        }

        {
            auto& device = devices.at("MemoryDevice1");
            UNIT_ASSERT_VALUES_EQUAL("", device.GetTransportId());
            UNIT_ASSERT_VALUES_EQUAL("memory", device.GetPoolName());
            UNIT_ASSERT_VALUES_EQUAL(DefaultDeviceBlockSize, device.GetBlockSize());
            UNIT_ASSERT_VALUES_EQUAL(DefaultBlocksCount, device.GetBlocksCount());
            UNIT_ASSERT_VALUES_EQUAL("agent:", device.GetBaseName());
        }

        {
            auto& device = devices.at("MemoryDevice2");
            UNIT_ASSERT_VALUES_EQUAL("", device.GetTransportId());
            UNIT_ASSERT_VALUES_EQUAL("", device.GetPoolName());
            UNIT_ASSERT_VALUES_EQUAL(DefaultDeviceBlockSize, device.GetBlockSize());
            UNIT_ASSERT_VALUES_EQUAL(DefaultBlocksCount, device.GetBlocksCount());
            UNIT_ASSERT_VALUES_EQUAL("agent:", device.GetBaseName());
        }

        {
            auto& device = devices.at("FileDevice3");
            UNIT_ASSERT_VALUES_EQUAL("", device.GetTransportId());
            UNIT_ASSERT_VALUES_EQUAL("", device.GetPoolName());
            UNIT_ASSERT_VALUES_EQUAL(DefaultDeviceBlockSize, device.GetBlockSize());
            UNIT_ASSERT_VALUES_EQUAL(DefaultStubBlocksCount, device.GetBlocksCount());
            UNIT_ASSERT_VALUES_EQUAL("agent:", device.GetBaseName());
        }

        {
            auto& device = devices.at("FileDevice4");
            UNIT_ASSERT_VALUES_EQUAL("", device.GetTransportId());
            UNIT_ASSERT_VALUES_EQUAL("local-ssd", device.GetPoolName());
            UNIT_ASSERT_VALUES_EQUAL(DefaultDeviceBlockSize, device.GetBlockSize());
            UNIT_ASSERT_VALUES_EQUAL(DefaultStubBlocksCount, device.GetBlocksCount());
            UNIT_ASSERT_VALUES_EQUAL("agent:", device.GetBaseName());
        }

        {
            auto& device = devices.at("NVMeDevice5");
            UNIT_ASSERT_VALUES_EQUAL("", device.GetTransportId());
            UNIT_ASSERT_VALUES_EQUAL("nvme", device.GetPoolName());
            UNIT_ASSERT_VALUES_EQUAL(DefaultBlockSize, device.GetBlockSize());
            UNIT_ASSERT_VALUES_EQUAL(DefaultStubBlocksCount, device.GetBlocksCount());
            UNIT_ASSERT_VALUES_EQUAL("agent:nvme1:", device.GetBaseName());
        }

        {
            auto& device = devices.at("NVMeDevice6");
            UNIT_ASSERT_VALUES_EQUAL("", device.GetTransportId());
            UNIT_ASSERT_VALUES_EQUAL("", device.GetPoolName());
            UNIT_ASSERT_VALUES_EQUAL(DefaultBlockSize, device.GetBlockSize());
            UNIT_ASSERT_VALUES_EQUAL(DefaultStubBlocksCount, device.GetBlocksCount());
            UNIT_ASSERT_VALUES_EQUAL("agent:nvme2:", device.GetBaseName());
        }
    }

    Y_UNIT_TEST(ShouldAcquireAndReleaseDevices)
    {
        TTestBasicRuntime runtime;

        const TVector<TString> uuids {
            "MemoryDevice1",
            "MemoryDevice2",
            "MemoryDevice3"
        };

        auto env = TTestEnvBuilder(runtime)
            .With(DiskAgentConfig(uuids))
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        diskAgent.AcquireDevices(
            uuids,
            "client-1",
            NProto::VOLUME_ACCESS_READ_WRITE
        );
        diskAgent.ReleaseDevices(uuids, "client-1");
    }

    Y_UNIT_TEST(ShouldAcquireWithRateLimits)
    {
        TTestBasicRuntime runtime;

        const TVector<TString> uuids {
            "MemoryDevice1",
            "MemoryDevice2",
            "MemoryDevice3"
        };

        auto env = TTestEnvBuilder(runtime)
            .With(DiskAgentConfig(uuids))
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        NSpdk::TDeviceRateLimits limits {
            .IopsLimit = 1000
        };

        diskAgent.AcquireDevices(
            uuids,
            "client-1",
            NProto::VOLUME_ACCESS_READ_WRITE,
            0,
            limits
        );
        diskAgent.ReleaseDevices(uuids, "client-1");
    }

    Y_UNIT_TEST(ShouldAcquireDevicesOnlyOnce)
    {
        TTestBasicRuntime runtime;

        auto env = TTestEnvBuilder(runtime)
            .With(DiskAgentConfig({"MemoryDevice1"}))
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        UNIT_ASSERT(env.DiskRegistryState->Devices.contains("MemoryDevice1"));

        const TVector<TString> uuids{
            "MemoryDevice1"
        };

        {
            auto response = diskAgent.AcquireDevices(
                uuids,
                "client-id",
                NProto::VOLUME_ACCESS_READ_WRITE
            );
            UNIT_ASSERT(!HasError(response->Record));
        }

        {
            diskAgent.SendAcquireDevicesRequest(
                uuids,
                "client-id2",
                NProto::VOLUME_ACCESS_READ_WRITE
            );
            auto response = diskAgent.RecvAcquireDevicesResponse();
            UNIT_ASSERT_VALUES_EQUAL(response->GetStatus(), E_BS_INVALID_SESSION);
            UNIT_ASSERT(response->GetErrorReason().Contains("already acquired"));
        }

        // reacquire with same client id is ok
        {
            auto response = diskAgent.AcquireDevices(
                uuids,
                "client-id",
                NProto::VOLUME_ACCESS_READ_WRITE
            );
            UNIT_ASSERT(!HasError(response->Record));
        }
    }

    Y_UNIT_TEST(ShouldAcquireDevicesWithMountSeqNumber)
    {
        TTestBasicRuntime runtime;

        auto env = TTestEnvBuilder(runtime)
            .With(DiskAgentConfig({"MemoryDevice1"}))
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        const TVector<TString> uuids{
            "MemoryDevice1"
        };

        {
            auto response = diskAgent.AcquireDevices(
                uuids,
                "client-id",
                NProto::VOLUME_ACCESS_READ_WRITE
            );
            UNIT_ASSERT(!HasError(response->Record));
        }

        {
            diskAgent.SendAcquireDevicesRequest(
                uuids,
                "client-id2",
                NProto::VOLUME_ACCESS_READ_WRITE,
                1
            );
            auto response = diskAgent.RecvAcquireDevicesResponse();
            UNIT_ASSERT_VALUES_EQUAL(S_OK, response->GetStatus());
        }

        {
            diskAgent.SendAcquireDevicesRequest(
                uuids,
                "client-id3",
                NProto::VOLUME_ACCESS_READ_WRITE,
                2
            );
            auto response = diskAgent.RecvAcquireDevicesResponse();
            UNIT_ASSERT_VALUES_EQUAL(S_OK, response->GetStatus());
        }

        {
            diskAgent.SendAcquireDevicesRequest(
                uuids,
                "client-id4",
                NProto::VOLUME_ACCESS_READ_WRITE,
                2
            );
            auto response = diskAgent.RecvAcquireDevicesResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_BS_INVALID_SESSION, response->GetStatus());
        }
    }

    Y_UNIT_TEST(ShouldAcquireDevicesOnlyOnceInGroup)
    {
        TTestBasicRuntime runtime;

        auto env = TTestEnvBuilder(runtime)
            .With(DiskAgentConfig({"MemoryDevice1", "MemoryDevice2"}))
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        UNIT_ASSERT_VALUES_EQUAL(env.DiskRegistryState->Devices.size(), 2);
        UNIT_ASSERT(env.DiskRegistryState->Devices.contains("MemoryDevice1"));
        UNIT_ASSERT(env.DiskRegistryState->Devices.contains("MemoryDevice2"));

        {
            const TVector<TString> uuids{
                "MemoryDevice1"
            };

            auto response = diskAgent.AcquireDevices(
                uuids,
                "client-id",
                NProto::VOLUME_ACCESS_READ_WRITE
            );
            UNIT_ASSERT(!HasError(response->Record));
        }

        {
            const TVector<TString> uuids{
                "MemoryDevice1",
                "MemoryDevice2"
            };

            diskAgent.SendAcquireDevicesRequest(
                uuids,
                "client-id2",
                NProto::VOLUME_ACCESS_READ_WRITE
            );
            auto response = diskAgent.RecvAcquireDevicesResponse();
            UNIT_ASSERT_VALUES_EQUAL(response->GetStatus(), E_BS_INVALID_SESSION);
            UNIT_ASSERT(response->GetErrorReason().Contains("already acquired"));
            UNIT_ASSERT(response->GetErrorReason().Contains(
                "MemoryDevice1"
            ));
        }

        // reacquire with same client id is ok
        {
            const TVector<TString> uuids{
                "MemoryDevice1"
            };

            auto response = diskAgent.AcquireDevices(
                uuids,
                "client-id",
                NProto::VOLUME_ACCESS_READ_WRITE
            );
            UNIT_ASSERT(!HasError(response->Record));
        }
    }

    Y_UNIT_TEST(ShouldRejectAcquireNonexistentDevice)
    {
        TTestBasicRuntime runtime;

        auto env = TTestEnvBuilder(runtime)
            .With(DiskAgentConfig({"MemoryDevice1"}))
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        const TVector<TString> uuids{
            "MemoryDevice1",
            "nonexistent uuid"
        };

        diskAgent.SendAcquireDevicesRequest(
            uuids,
            "client-id",
            NProto::VOLUME_ACCESS_READ_WRITE
        );
        auto response = diskAgent.RecvAcquireDevicesResponse();
        UNIT_ASSERT_VALUES_EQUAL(response->GetStatus(), E_NOT_FOUND);
        UNIT_ASSERT(response->GetErrorReason().Contains(uuids.back()));
    }

    Y_UNIT_TEST(ShouldPerformIo)
    {
        TTestBasicRuntime runtime;

        auto env = TTestEnvBuilder(runtime)
            .With(DiskAgentConfig({
                "MemoryDevice1",
                "MemoryDevice2",
                "MemoryDevice3",
            }))
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        const TVector<TString> uuids{
            "MemoryDevice1",
            "MemoryDevice2"
        };

        const TString clientId = "client-1";

        diskAgent.AcquireDevices(
            uuids,
            clientId,
            NProto::VOLUME_ACCESS_READ_WRITE
        );

        const auto blocksCount = 10;

        {
            const auto response = ReadDeviceBlocks(
                runtime, diskAgent, uuids[0], 0, blocksCount, clientId);

            const auto& record = response->Record;

            UNIT_ASSERT(record.HasBlocks());

            const auto& iov = record.GetBlocks();
            UNIT_ASSERT_VALUES_EQUAL(iov.BuffersSize(), blocksCount);
            for (auto& buffer : iov.GetBuffers()) {
                UNIT_ASSERT_VALUES_EQUAL(buffer.size(), DefaultBlockSize);
            }
        }

        {
            TVector<TString> blocks;
            auto sglist = ResizeBlocks(
                blocks,
                blocksCount,
                TString(DefaultBlockSize, 'X'));

            WriteDeviceBlocks(runtime, diskAgent, uuids[0], 0, sglist, clientId);
            ZeroDeviceBlocks(runtime, diskAgent, uuids[0], 0, 10, clientId);
        }

        {
            auto request = std::make_unique<TEvDiskAgent::TEvWriteDeviceBlocksRequest>();
            request->Record.MutableHeaders()->SetClientId(clientId);
            request->Record.SetDeviceUUID(uuids[0]);
            request->Record.SetStartIndex(0);
            request->Record.SetBlockSize(DefaultBlockSize);

            auto sgList = ResizeIOVector(*request->Record.MutableBlocks(), 1, 1_MB);

            for (auto& buffer: sgList) {
                memset(const_cast<char*>(buffer.Data()), 'Y', buffer.Size());
            }

            diskAgent.SendRequest(MakeDiskAgentServiceId(), std::move(request));
            runtime.DispatchEvents(NActors::TDispatchOptions());
            auto response = diskAgent.RecvWriteDeviceBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(response->GetStatus(), S_OK);
        }
    }

    Y_UNIT_TEST(ShouldDenyOverlappedRequests)
    {
        TTestBasicRuntime runtime;

        auto env = TTestEnvBuilder(runtime)
                       .With(DiskAgentConfig({
                           "MemoryDevice1",
                           "MemoryDevice2",
                           "MemoryDevice3",
                       }))
                       .With([] {
                           NProto::TStorageServiceConfig config;
                           config.SetRejectLateRequestsAtDiskAgentEnabled(true);
                           return config;
                       }())
                       .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        const TVector<TString> uuids{"MemoryDevice1", "MemoryDevice2"};

        const TString clientId = "client-1";

        diskAgent.AcquireDevices(
            uuids,
            clientId,
            NProto::VOLUME_ACCESS_READ_WRITE);

        auto writeRequest = [&](ui64 volumeRequestId,
                                ui64 blockStart,
                                ui32 blockCount,
                                EWellKnownResultCodes expected) {
            auto request =
                std::make_unique<TEvDiskAgent::TEvWriteDeviceBlocksRequest>();
            request->Record.MutableHeaders()->SetClientId(clientId);
            request->Record.SetDeviceUUID(uuids[0]);
            request->Record.SetStartIndex(blockStart);
            request->Record.SetBlockSize(DefaultBlockSize);
            request->Record.SetVolumeRequestId(volumeRequestId);

            auto sgList =
                ResizeIOVector(*request->Record.MutableBlocks(), 1, blockCount * DefaultBlockSize);

            for (auto& buffer : sgList) {
                memset(const_cast<char*>(buffer.Data()), 'Y', buffer.Size());
            }

            diskAgent.SendRequest(MakeDiskAgentServiceId(), std::move(request));
            runtime.DispatchEvents(NActors::TDispatchOptions());
            auto response = diskAgent.RecvWriteDeviceBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(expected, response->GetStatus());
        };

        auto zeroRequest = [&](ui64 volumeRequestId,
                               ui64 blockStart,
                               ui32 blockCount,
                               EWellKnownResultCodes expected) {
            auto request =
                std::make_unique<TEvDiskAgent::TEvZeroDeviceBlocksRequest>();
            request->Record.MutableHeaders()->SetClientId(clientId);
            request->Record.SetDeviceUUID(uuids[0]);
            request->Record.SetStartIndex(blockStart);
            request->Record.SetBlockSize(DefaultBlockSize);
            request->Record.SetBlocksCount(blockCount);
            request->Record.SetVolumeRequestId(volumeRequestId);

            diskAgent.SendRequest(MakeDiskAgentServiceId(), std::move(request));
            runtime.DispatchEvents(NActors::TDispatchOptions());
            auto response = diskAgent.RecvZeroDeviceBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(expected, response->GetStatus());
        };
        {
            auto requestId = TCompositeId::FromGeneration(2);
            writeRequest(requestId.GetValue(), 1024, 10, S_OK);

            // Same requestId rejected.
            writeRequest(requestId.GetValue(), 1024, 10, E_REJECTED);

            // Next requestId accepted.
            writeRequest(requestId.Advance(), 1024, 10, S_OK);

            // Fill zeros block started at 64k
            zeroRequest(requestId.Advance(), 2048, 1024, S_OK);
        }

        {
            // requestId from past (previous generation) with full overlap should return S_ALREADY.
            auto requestId = TCompositeId::FromGeneration(1);
            writeRequest(requestId.Advance(), 1024, 10, S_ALREADY);
            writeRequest(requestId.Advance(), 2048, 10, S_ALREADY);

            zeroRequest(requestId.Advance(), 1024, 8, S_ALREADY);
            zeroRequest(requestId.Advance(), 2048, 8, S_ALREADY);

            // partial overlapped request should return E_REJECTED.
            writeRequest(requestId.Advance(), 1000, 30, E_REJECTED);
            zeroRequest(requestId.Advance(), 2000, 50, E_REJECTED);
        }
    }

    Y_UNIT_TEST(ShouldDelayOverlappedRequests)
    {
        TTestBasicRuntime runtime;

        auto env = TTestEnvBuilder(runtime)
                       .With(DiskAgentConfig({
                           "MemoryDevice1",
                           "MemoryDevice2",
                           "MemoryDevice3",
                       }))
                       .With([] {
                           NProto::TStorageServiceConfig config;
                           config.SetRejectLateRequestsAtDiskAgentEnabled(true);
                           return config;
                       }())
                       .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        const TVector<TString> uuids{"MemoryDevice1", "MemoryDevice2"};

        const TString clientId = "client-1";

        diskAgent.AcquireDevices(
            uuids,
            clientId,
            NProto::VOLUME_ACCESS_READ_WRITE);

        // Steal TEvWriteOrZeroCompleted message.
        // We will return it later to start the execution of the next overlapped
        // request.
        std::vector<std::unique_ptr<IEventHandle>> stolenWriteCompletedRequests;
        auto stealFirstDeviceRequest = [&](TAutoPtr<IEventHandle>& event) {
            if (event->GetTypeRewrite() ==
                TEvDiskAgentPrivate::EvWriteOrZeroCompleted)
            {
                stolenWriteCompletedRequests.push_back(
                    std::unique_ptr<IEventHandle>{event.Release()});
                return TTestActorRuntime::EEventAction::DROP;
            }
            return TTestActorRuntime::DefaultObserverFunc(event);
        };
        auto oldObserverFunc = runtime.SetObserverFunc(stealFirstDeviceRequest);

        auto sendWriteRequest = [&](ui64 volumeRequestId,
                                    ui64 blockStart,
                                    ui32 blockCount) {
            auto request =
                std::make_unique<TEvDiskAgent::TEvWriteDeviceBlocksRequest>();
            request->Record.MutableHeaders()->SetClientId(clientId);
            request->Record.SetDeviceUUID(uuids[0]);
            request->Record.SetStartIndex(blockStart);
            request->Record.SetBlockSize(DefaultBlockSize);
            request->Record.SetVolumeRequestId(volumeRequestId);

            auto sgList = ResizeIOVector(
                *request->Record.MutableBlocks(),
                1,
                blockCount * DefaultBlockSize);

            for (auto& buffer : sgList) {
                memset(const_cast<char*>(buffer.Data()), 'Y', buffer.Size());
            }

            diskAgent.SendRequest(MakeDiskAgentServiceId(), std::move(request));
        };

        auto sendZeroRequest = [&](ui64 volumeRequestId,
                                   ui64 blockStart,
                                   ui32 blockCount) {
            auto request =
                std::make_unique<TEvDiskAgent::TEvZeroDeviceBlocksRequest>();
            request->Record.MutableHeaders()->SetClientId(clientId);
            request->Record.SetDeviceUUID(uuids[0]);
            request->Record.SetStartIndex(blockStart);
            request->Record.SetBlockSize(DefaultBlockSize);
            request->Record.SetBlocksCount(blockCount);
            request->Record.SetVolumeRequestId(volumeRequestId);

            diskAgent.SendRequest(MakeDiskAgentServiceId(), std::move(request));
        };

        // Send write and zero requests. Their messages TWriteOrZeroCompleted will be stolen.
        sendWriteRequest(100, 1024, 10);
        sendZeroRequest(101, 2048, 10);

        runtime.DispatchEvents({}, TDuration::Seconds(1));
        UNIT_ASSERT(!stolenWriteCompletedRequests.empty());
        // N.B. We are delaying the internal TEvWriteOrZeroCompleted request.
        // The response to the client was not delayed, so here we receive write and zero responses.
        UNIT_ASSERT_VALUES_EQUAL(
            S_OK,
            diskAgent.RecvZeroDeviceBlocksResponse()->GetStatus());
        UNIT_ASSERT_VALUES_EQUAL(
            S_OK,
            diskAgent.RecvWriteDeviceBlocksResponse()->GetStatus());

        // Turning off the theft of TWriteOrZeroCompleted messages.
        runtime.SetObserverFunc(oldObserverFunc);

        {
            // Send new write request (from past) with range NOT overlapped with
            // delayed request. New request is executed immediately.
            sendWriteRequest(90, 3072, 10);
            UNIT_ASSERT_VALUES_EQUAL(
                S_OK,
                diskAgent.RecvWriteDeviceBlocksResponse()->GetStatus());
        }
        {
            // Send new zero request (from past) with range NOT overlapped with
            // delayed request. New request is executed immediately.
            sendZeroRequest(91, 4096, 10);
            UNIT_ASSERT_VALUES_EQUAL(
                S_OK,
                diskAgent.RecvZeroDeviceBlocksResponse()->GetStatus());
        }
        {
            // Send new write request (from past) with range overlapped with
            // delayed request. New request will be delayed untill overlapped
            // request completed. Request will be completed with E_ALREADY as it
            // fits into the already completed request.
            sendWriteRequest(98, 1024, 5);
            TAutoPtr<IEventHandle> handle;
            runtime.GrabEdgeEventRethrow<
                TEvDiskAgent::TEvWriteDeviceBlocksResponse>(
                handle,
                TDuration::Seconds(1));
            UNIT_ASSERT_EQUAL(nullptr, handle);
        }
        {
            // Send new zero request (from past) with range partial overlapped
            // with delayed request. New request will be delayed untill
            // overlapped request completed and comleted with E_REJECTED since
            // it partially intersects with the already completed request.
            sendZeroRequest(99, 2048, 15);
            TAutoPtr<IEventHandle> handle;
            runtime.GrabEdgeEventRethrow<
                TEvDiskAgent::TEvZeroDeviceBlocksResponse>(
                handle,
                TDuration::Seconds(1));
            UNIT_ASSERT_EQUAL(nullptr, handle);
        }
        {
            // Send new write request (from future) with range overlapped with
            // delayed request. New request is executed immediately.
            sendWriteRequest(110, 1024, 5);
            auto response =
                diskAgent.RecvWriteDeviceBlocksResponse(TDuration::Seconds(1));
            UNIT_ASSERT_VALUES_EQUAL(S_OK, response->GetStatus());
        }
        {
            // Send new zero request (from future) with range overlapped with
            // delayed request. New request is executed immediately.
            sendWriteRequest(111, 2048, 3);
            auto response =
                diskAgent.RecvWriteDeviceBlocksResponse(TDuration::Seconds(1));
            UNIT_ASSERT_VALUES_EQUAL(S_OK, response->GetStatus());
        }

        // Return all stolen request - write/zero request will be completed
        // after this
        for (auto& request : stolenWriteCompletedRequests) {
            runtime.Send(request.release());
        }

        {
            // Get reponse for delayed write request
            auto response =
                diskAgent.RecvWriteDeviceBlocksResponse(TDuration::Seconds(1));
            UNIT_ASSERT_VALUES_EQUAL(S_ALREADY, response->GetStatus());
        }
        {
            // Get reponse for delayed zero request
            auto response =
                diskAgent.RecvZeroDeviceBlocksResponse(TDuration::Seconds(1));
            UNIT_ASSERT_VALUES_EQUAL(E_REJECTED, response->GetStatus());
        }
    }

    Y_UNIT_TEST(ShouldRejectMultideviceOverlappedRequests)
    {
        TTestBasicRuntime runtime;

        auto env = TTestEnvBuilder(runtime)
                       .With(DiskAgentConfig({
                           "MemoryDevice1",
                           "MemoryDevice2",
                           "MemoryDevice3",
                       }))
                       .With([] {
                           NProto::TStorageServiceConfig config;
                           config.SetRejectLateRequestsAtDiskAgentEnabled(true);
                           return config;
                       }())
                       .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        const TVector<TString> uuids{"MemoryDevice1", "MemoryDevice2"};

        const TString clientId = "client-1";

        diskAgent.AcquireDevices(
            uuids,
            clientId,
            NProto::VOLUME_ACCESS_READ_WRITE);

        // Steal TEvWriteOrZeroCompleted request.
        // We will return it later to start the execution of the next overlapped
        // request.
        std::vector<std::unique_ptr<IEventHandle>> stolenWriteCompletedRequests;
        auto stealFirstDeviceRequest = [&](TAutoPtr<IEventHandle>& event) {
            if (event->GetTypeRewrite() ==
                TEvDiskAgentPrivate::EvWriteOrZeroCompleted)
            {
                stolenWriteCompletedRequests.push_back(
                    std::unique_ptr<IEventHandle>{event.Release()});
                return TTestActorRuntime::EEventAction::DROP;
            }
            return TTestActorRuntime::DefaultObserverFunc(event);
        };
        auto oldObserverFunc = runtime.SetObserverFunc(stealFirstDeviceRequest);

        auto sendZeroRequest = [&](ui64 volumeRequestId,
                                   ui64 blockStart,
                                   ui32 blockCount) {
            auto request =
                std::make_unique<TEvDiskAgent::TEvZeroDeviceBlocksRequest>();
            request->Record.MutableHeaders()->SetClientId(clientId);
            request->Record.SetDeviceUUID(uuids[0]);
            request->Record.SetStartIndex(blockStart);
            request->Record.SetBlockSize(DefaultBlockSize);
            request->Record.SetBlocksCount(blockCount);
            request->Record.SetVolumeRequestId(volumeRequestId);
            request->Record.SetMultideviceRequest(
                true); // This will lead to E_REJECT

            diskAgent.SendRequest(MakeDiskAgentServiceId(), std::move(request));
        };

        // Send zero request. The TWriteOrZeroCompleted message from this request will
        // be stolen.
        sendZeroRequest(100, 2048, 16);
        runtime.DispatchEvents({}, TDuration::Seconds(1));
        UNIT_ASSERT(!stolenWriteCompletedRequests.empty());
        // N.B. We are delaying the internal TEvWriteOrZeroCompleted request.
        // The response to the client was not delayed, so here we receive a
        // ZeroDeviceBlocksResponse.
        UNIT_ASSERT_VALUES_EQUAL(
            S_OK,
            diskAgent.RecvZeroDeviceBlocksResponse()->GetStatus());

        // Turning off the theft of responses.
        runtime.SetObserverFunc(oldObserverFunc);

        {
            // Send new zero request (from past) with range coverred by delayed
            // request. This request will be delayed untill overlapped request
            // completed.
            sendZeroRequest(98, 2048, 8);
            TAutoPtr<IEventHandle> handle;
            runtime.GrabEdgeEventRethrow<
                TEvDiskAgent::TEvWriteDeviceBlocksResponse>(
                handle,
                TDuration::Seconds(1));
            UNIT_ASSERT_EQUAL(nullptr, handle);
        }

        // Return all stolen request - write/zero request will be completed
        // after this
        for (auto& request : stolenWriteCompletedRequests) {
            runtime.Send(request.release());
        }
        {
            // Get reponse for delayed zero request. It will rejected
            auto response =
                diskAgent.RecvZeroDeviceBlocksResponse(TDuration::Seconds(1));
            UNIT_ASSERT_VALUES_EQUAL(E_REJECTED, response->GetStatus());
        }
    }

    Y_UNIT_TEST(ShouldNotMessDifferentDevicesRequests)
    {
        TTestBasicRuntime runtime;

        auto env = TTestEnvBuilder(runtime)
                       .With(DiskAgentConfig({
                           "MemoryDevice1",
                           "MemoryDevice2",
                       }))
                       .With([] {
                           NProto::TStorageServiceConfig config;
                           config.SetRejectLateRequestsAtDiskAgentEnabled(true);
                           return config;
                       }())
                       .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        const TVector<TString> uuids{"MemoryDevice1", "MemoryDevice2"};

        const TString clientId = "client-1";

        diskAgent.AcquireDevices(
            uuids,
            clientId,
            NProto::VOLUME_ACCESS_READ_WRITE);

        // Steal TEvWriteOrZeroCompleted request.
        // We will return it later. This should not delay the request of another device.
        std::vector<std::unique_ptr<IEventHandle>> stolenWriteCompletedRequests;
        auto stealFirstDeviceRequest = [&](TAutoPtr<IEventHandle>& event) {
            if (event->GetTypeRewrite() ==
                TEvDiskAgentPrivate::EvWriteOrZeroCompleted)
            {
                stolenWriteCompletedRequests.push_back(
                    std::unique_ptr<IEventHandle>{event.Release()});
                return TTestActorRuntime::EEventAction::DROP;
            }
            return TTestActorRuntime::DefaultObserverFunc(event);
        };
        auto oldObserverFunc = runtime.SetObserverFunc(stealFirstDeviceRequest);

        auto sendZeroRequest = [&](ui64 volumeRequestId,
                                   ui64 blockStart,
                                   ui32 blockCount,
                                   const TString& deviceUUID) {
            auto request =
                std::make_unique<TEvDiskAgent::TEvZeroDeviceBlocksRequest>();
            request->Record.MutableHeaders()->SetClientId(clientId);
            request->Record.SetDeviceUUID(deviceUUID);
            request->Record.SetStartIndex(blockStart);
            request->Record.SetBlockSize(DefaultBlockSize);
            request->Record.SetBlocksCount(blockCount);
            request->Record.SetVolumeRequestId(volumeRequestId);

            diskAgent.SendRequest(MakeDiskAgentServiceId(), std::move(request));
        };

        // Send zero request. The TWriteOrZeroCompleted message from this request will
        // be stolen.
        sendZeroRequest(100, 2048, 16, uuids[0]);
        runtime.DispatchEvents({}, TDuration::Seconds(1));
        UNIT_ASSERT(!stolenWriteCompletedRequests.empty());
        // N.B. We are delaying the internal TEvWriteOrZeroCompleted request.
        // The response to the client was not delayed, so here we receive a
        // ZeroDeviceBlocksResponse.
        UNIT_ASSERT_VALUES_EQUAL(
            S_OK,
            diskAgent.RecvZeroDeviceBlocksResponse()->GetStatus());

        // Turning off the theft of responses.
        runtime.SetObserverFunc(oldObserverFunc);

        {
            // Send new zero request (from past) with range coverred by delayed
            // request. This request will not be delayed as it relates to another device.
            sendZeroRequest(98, 2048, 8, uuids[1]);
            auto response =
                diskAgent.RecvZeroDeviceBlocksResponse(TDuration::Seconds(1));
            UNIT_ASSERT_VALUES_EQUAL(S_OK, response->GetStatus());
        }
    }

    void DoShouldHandleSameRequestId(bool monitoringMode)
    {
        TTestBasicRuntime runtime;
        NMonitoring::TDynamicCountersPtr counters =
            new NMonitoring::TDynamicCounters();
        InitCriticalEventsCounter(counters);

        auto env = TTestEnvBuilder(runtime)
                       .With(DiskAgentConfig({
                           "MemoryDevice1",
                           "MemoryDevice2",
                       }))
                       .With(
                           [monitoringMode]()
                           {
                               NProto::TStorageServiceConfig config;
                               config.SetRejectLateRequestsAtDiskAgentEnabled(
                                   !monitoringMode);
                               return config;
                           }())
                       .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        const TVector<TString> uuids{"MemoryDevice1", "MemoryDevice2"};

        const TString clientId = "client-1";

        diskAgent.AcquireDevices(
            uuids,
            clientId,
            NProto::VOLUME_ACCESS_READ_WRITE);

        auto sendZeroRequest = [&](ui64 volumeRequestId,
                                   ui64 blockStart,
                                   ui32 blockCount,
                                   const TString& deviceUUID)
        {
            auto request =
                std::make_unique<TEvDiskAgent::TEvZeroDeviceBlocksRequest>();
            request->Record.MutableHeaders()->SetClientId(clientId);
            request->Record.SetDeviceUUID(deviceUUID);
            request->Record.SetStartIndex(blockStart);
            request->Record.SetBlockSize(DefaultBlockSize);
            request->Record.SetBlocksCount(blockCount);
            request->Record.SetVolumeRequestId(volumeRequestId);

            diskAgent.SendRequest(MakeDiskAgentServiceId(), std::move(request));
        };

        {
            sendZeroRequest(100, 2048, 16, uuids[0]);
            UNIT_ASSERT_VALUES_EQUAL(
                S_OK,
                diskAgent.RecvZeroDeviceBlocksResponse()->GetStatus());
        }
        {
            // Send first request once again with same requestId.
            sendZeroRequest(100, 2048, 8, uuids[0]);
            auto response =
                diskAgent.RecvZeroDeviceBlocksResponse(TDuration::Seconds(1));
            UNIT_ASSERT_VALUES_EQUAL(
                monitoringMode ? S_OK : E_REJECTED,
                response->GetStatus());
        }
        {
            // Send request to another device.
            sendZeroRequest(100, 2048, 8, uuids[1]);
            auto response =
                diskAgent.RecvZeroDeviceBlocksResponse(TDuration::Seconds(1));
            UNIT_ASSERT_VALUES_EQUAL(S_OK, response->GetStatus());
        }


        auto unexpectedIdentifierRepetition = counters->GetCounter(
            "AppCriticalEvents/UnexpectedIdentifierRepetition",
            true);
        UNIT_ASSERT_VALUES_EQUAL(
            monitoringMode ? 2 : 1,
            unexpectedIdentifierRepetition->Val());
    }

    Y_UNIT_TEST(ShouldHandleSameRequestIdMonitoringOn)
    {
        DoShouldHandleSameRequestId(true);
    }

    Y_UNIT_TEST(ShouldHandleSameRequestIdMonitoringOff)
    {
        DoShouldHandleSameRequestId(false);
    }

    Y_UNIT_TEST(ShouldSupportReadOnlyClients)
    {
        TTestBasicRuntime runtime;

        auto env = TTestEnvBuilder(runtime)
            .With(DiskAgentConfig({
                "MemoryDevice1",
                "MemoryDevice2",
                "MemoryDevice3",
            }))
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        const TVector<TString> uuids{
            "MemoryDevice1",
            "MemoryDevice2"
        };

        const TString writerClientId = "writer-1";
        const TString readerClientId1 = "reader-1";
        const TString readerClientId2 = "reader-2";

        diskAgent.AcquireDevices(
            uuids,
            writerClientId,
            NProto::VOLUME_ACCESS_READ_WRITE
        );

        diskAgent.AcquireDevices(
            uuids,
            readerClientId1,
            NProto::VOLUME_ACCESS_READ_ONLY
        );

        diskAgent.AcquireDevices(
            uuids,
            readerClientId2,
            NProto::VOLUME_ACCESS_READ_ONLY
        );

        const auto blocksCount = 10;

        TVector<TString> blocks;
        auto sglist = ResizeBlocks(
            blocks,
            blocksCount,
            TString(DefaultBlockSize, 'X'));

        WriteDeviceBlocks(runtime, diskAgent, uuids[0], 0, sglist, writerClientId);
        // diskAgent.ZeroDeviceBlocks(uuids[0], 0, blocksCount / 2, writerClientId);

        for (auto sid: {readerClientId1, readerClientId2}) {
            diskAgent.SendWriteDeviceBlocksRequest(
                uuids[0],
                0,
                sglist,
                sid
            );

            auto response = diskAgent.RecvWriteDeviceBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(response->GetStatus(), E_BS_INVALID_SESSION);
        }

        for (auto sid: {readerClientId1, readerClientId2}) {
            diskAgent.SendZeroDeviceBlocksRequest(
                uuids[0],
                0,
                blocksCount / 2,
                sid
            );

            auto response = diskAgent.RecvZeroDeviceBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(response->GetStatus(), E_BS_INVALID_SESSION);
        }

        for (auto sid: {writerClientId, readerClientId1, readerClientId2}) {
            const auto response = ReadDeviceBlocks(
                runtime,
                diskAgent,
                uuids[0],
                0,
                blocksCount,
                sid);

            const auto& record = response->Record;

            UNIT_ASSERT(record.HasBlocks());

            const auto& iov = record.GetBlocks();
            UNIT_ASSERT_VALUES_EQUAL(iov.BuffersSize(), blocksCount);

            for (auto& buffer : iov.GetBuffers()) {
                UNIT_ASSERT_VALUES_EQUAL(buffer.size(), DefaultBlockSize);
            }

            /*
            ui32 i = 0;

            while (i < blocksCount / 2) {
                UNIT_ASSERT_VALUES_EQUAL(
                    TString(10, 0),
                    iov.GetBuffers(i).substr(0, 10)
                );

                ++i;
            }

            while (i < blocksCount) {
                UNIT_ASSERT_VALUES_EQUAL(
                    TString(10, 'X'),
                    iov.GetBuffers(i).substr(0, 10)
                );

                ++i;
            }
            */
        }
    }

    Y_UNIT_TEST(ShouldPerformIoUnsafe)
    {
        TTestBasicRuntime runtime;

        auto env = TTestEnvBuilder(runtime)
            .With(DiskAgentConfig(
                { "MemoryDevice1", "MemoryDevice2", "MemoryDevice3" },
                false))
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        const TVector<TString> uuids{
            "MemoryDevice1",
            "MemoryDevice2"
        };

        const auto blocksCount = 10;

        {
            const auto response = ReadDeviceBlocks(
                runtime, diskAgent, uuids[0], 0, blocksCount, "");

            const auto& record = response->Record;

            UNIT_ASSERT(record.HasBlocks());

            const auto& iov = record.GetBlocks();
            UNIT_ASSERT_VALUES_EQUAL(iov.BuffersSize(), blocksCount);
            for (auto& buffer : iov.GetBuffers()) {
                UNIT_ASSERT_VALUES_EQUAL(buffer.size(), DefaultBlockSize);
            }
        }

        TVector<TString> blocks;
        auto sglist = ResizeBlocks(
            blocks,
            blocksCount,
            TString(DefaultBlockSize, 'X'));

        WriteDeviceBlocks(runtime, diskAgent, uuids[0], 0, sglist, "");
        ZeroDeviceBlocks(runtime, diskAgent, uuids[0], 0, 10, "");
    }

    Y_UNIT_TEST(ShouldSecureEraseDevice)
    {
        TTestBasicRuntime runtime;

        auto env = TTestEnvBuilder(runtime)
            .With(DiskAgentConfig({
                "MemoryDevice1",
                "MemoryDevice2",
                "MemoryDevice3",
            }))
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        diskAgent.SecureEraseDevice("MemoryDevice1");
    }

    Y_UNIT_TEST(ShouldUpdateStats)
    {
        auto const workingDir = TryGetRamDrivePath();

        TTestBasicRuntime runtime;

        const TString name1 = workingDir / "error";
        const TString name2 = "name2";

        auto env = TTestEnvBuilder(runtime)
            .With([&] {
                NProto::TDiskAgentConfig agentConfig;
                agentConfig.SetBackend(NProto::DISK_AGENT_BACKEND_AIO);
                agentConfig.SetAcquireRequired(true);
                agentConfig.SetEnabled(true);
                // healthcheck generates extra requests whose presence in stats
                // makes expected values in this test harder to calculate
                agentConfig.SetDeviceHealthCheckDisabled(true);

                *agentConfig.AddFileDevices() = PrepareFileDevice(
                    workingDir / "error",
                    "dev1");

                auto* device = agentConfig.MutableMemoryDevices()->Add();
                device->SetName(name2);
                device->SetBlocksCount(1024);
                device->SetBlockSize(DefaultBlockSize);
                device->SetDeviceId("dev2");

                return agentConfig;
            }())
            .Build();

        UNIT_ASSERT(env.DiskRegistryState->Stats.empty());

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        diskAgent.AcquireDevices(
            TVector<TString> {"dev1", "dev2"},
            "client-1",
            NProto::VOLUME_ACCESS_READ_WRITE
        );

        auto waitForStats = [&] (auto cb) {
            runtime.AdvanceCurrentTime(TDuration::Seconds(15));

            TDispatchOptions options;
            options.FinalEvents = {
                TDispatchOptions::TFinalEventCondition(
                    TEvDiskRegistry::EvUpdateAgentStatsRequest)
            };

            runtime.DispatchEvents(options);

            const auto& stats = env.DiskRegistryState->Stats;
            UNIT_ASSERT_EQUAL(1, stats.size());

            auto agentStats = stats.begin()->second;
            SortBy(*agentStats.MutableDeviceStats(), [] (auto& x) {
                return x.GetDeviceUUID();
            });

            cb(agentStats);
        };

        auto read = [&] (const auto& uuid) {
            return ReadDeviceBlocks(
                runtime,
                diskAgent,
                uuid,
                0,
                1,
                "client-1")->GetStatus();
        };

        waitForStats([&] (auto agentStats) {
            UNIT_ASSERT_EQUAL(2, agentStats.DeviceStatsSize());

            auto& dev0 = agentStats.GetDeviceStats(0);
            UNIT_ASSERT_VALUES_EQUAL("dev1", dev0.GetDeviceUUID());
            UNIT_ASSERT_VALUES_EQUAL(name1, dev0.GetDeviceName());
            UNIT_ASSERT_VALUES_EQUAL(0, dev0.GetErrors());
            UNIT_ASSERT_VALUES_EQUAL(0, dev0.GetNumReadOps());
            UNIT_ASSERT_VALUES_EQUAL(0, dev0.GetBytesRead());

            auto& dev1 = agentStats.GetDeviceStats(1);
            UNIT_ASSERT_VALUES_EQUAL("dev2", dev1.GetDeviceUUID());
            UNIT_ASSERT_VALUES_EQUAL(name2, dev1.GetDeviceName());
            UNIT_ASSERT_VALUES_EQUAL(0, dev1.GetErrors());
            UNIT_ASSERT_VALUES_EQUAL(0, dev1.GetNumReadOps());
            UNIT_ASSERT_VALUES_EQUAL(0, dev1.GetBytesRead());
        });

        UNIT_ASSERT_VALUES_EQUAL(E_IO, read("dev1"));
        UNIT_ASSERT_VALUES_EQUAL(E_IO, read("dev1"));
        UNIT_ASSERT_VALUES_EQUAL(S_OK, read("dev2"));
        UNIT_ASSERT_VALUES_EQUAL(S_OK, read("dev2"));

        waitForStats([&] (auto agentStats) {
            UNIT_ASSERT_EQUAL(2, agentStats.DeviceStatsSize());

            auto& dev0 = agentStats.GetDeviceStats(0);
            UNIT_ASSERT_VALUES_EQUAL("dev1", dev0.GetDeviceUUID());
            UNIT_ASSERT_VALUES_EQUAL(name1, dev0.GetDeviceName());
            UNIT_ASSERT_VALUES_EQUAL(2, dev0.GetErrors());
            UNIT_ASSERT_VALUES_EQUAL(2, dev0.GetNumReadOps());
            UNIT_ASSERT_VALUES_EQUAL(0, dev0.GetBytesRead());

            auto& dev1 = agentStats.GetDeviceStats(1);
            UNIT_ASSERT_VALUES_EQUAL("dev2", dev1.GetDeviceUUID());
            UNIT_ASSERT_VALUES_EQUAL(name2, dev1.GetDeviceName());
            UNIT_ASSERT_VALUES_EQUAL(0, dev1.GetErrors());
            UNIT_ASSERT_VALUES_EQUAL(2, dev1.GetNumReadOps());
            UNIT_ASSERT_VALUES_EQUAL(2*DefaultBlockSize, dev1.GetBytesRead());
        });

        UNIT_ASSERT_EQUAL(E_IO, read("dev1"));
        UNIT_ASSERT_EQUAL(S_OK, read("dev2"));
        UNIT_ASSERT_EQUAL(S_OK, read("dev2"));
        UNIT_ASSERT_EQUAL(S_OK, read("dev2"));

        waitForStats([&] (auto agentStats) {
            UNIT_ASSERT_EQUAL(2, agentStats.DeviceStatsSize());

            auto& dev0 = agentStats.GetDeviceStats(0);
            UNIT_ASSERT_VALUES_EQUAL("dev1", dev0.GetDeviceUUID());
            UNIT_ASSERT_VALUES_EQUAL(name1, dev0.GetDeviceName());
            UNIT_ASSERT_VALUES_EQUAL(1, dev0.GetErrors());
            UNIT_ASSERT_VALUES_EQUAL(1, dev0.GetNumReadOps());
            UNIT_ASSERT_VALUES_EQUAL(0, dev0.GetBytesRead());

            auto& dev1 = agentStats.GetDeviceStats(1);
            UNIT_ASSERT_VALUES_EQUAL("dev2", dev1.GetDeviceUUID());
            UNIT_ASSERT_VALUES_EQUAL(name2, dev1.GetDeviceName());
            UNIT_ASSERT_VALUES_EQUAL(0, dev1.GetErrors());
            UNIT_ASSERT_VALUES_EQUAL(3, dev1.GetNumReadOps());
            UNIT_ASSERT_VALUES_EQUAL(3*DefaultBlockSize, dev1.GetBytesRead());
        });
    }

    Y_UNIT_TEST(ShouldCollectStats)
    {
        TTestBasicRuntime runtime;

        auto env = TTestEnvBuilder(runtime)
            .With(DiskAgentConfig({
                "MemoryDevice1",
                "MemoryDevice2",
                "MemoryDevice3",
            }))
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        auto response = diskAgent.CollectStats();
        auto& stats = response->Stats;

        UNIT_ASSERT_VALUES_EQUAL(0, response->Stats.GetInitErrorsCount());
        UNIT_ASSERT_VALUES_EQUAL(3, stats.DeviceStatsSize());

        SortBy(*stats.MutableDeviceStats(), [] (auto& x) {
            return x.GetDeviceUUID();
        });

        UNIT_ASSERT_VALUES_EQUAL(
            "MemoryDevice1",
            stats.GetDeviceStats(0).GetDeviceUUID());
        UNIT_ASSERT_VALUES_EQUAL(
            "MemoryDevice2",
            stats.GetDeviceStats(1).GetDeviceUUID());
        UNIT_ASSERT_VALUES_EQUAL(
            "MemoryDevice3",
            stats.GetDeviceStats(2).GetDeviceUUID());
    }

    Y_UNIT_TEST(ShouldPreserveCookie)
    {
        TTestBasicRuntime runtime;

        auto env = TTestEnvBuilder(runtime)
            .With(DiskAgentConfig({
                "MemoryDevice1",
                "MemoryDevice2",
                "MemoryDevice3",
            }))
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        const TVector<TString> uuids { "MemoryDevice1" };

        diskAgent.AcquireDevices(
            uuids,
            "client-id",
            NProto::VOLUME_ACCESS_READ_WRITE
        );

        {
            const ui32 blocksCount = 10;

            TVector<TString> blocks;
            auto sglist = ResizeBlocks(
                blocks,
                blocksCount,
                TString(DefaultBlockSize, 'X'));

            auto request = diskAgent.CreateWriteDeviceBlocksRequest(
                uuids[0], 0, sglist, "client-id");

            const ui64 cookie = 42;

            diskAgent.SendRequest(MakeDiskAgentServiceId(), std::move(request), cookie);

            TAutoPtr<IEventHandle> handle;
            runtime.GrabEdgeEventRethrow<TEvDiskAgent::TEvWriteDeviceBlocksResponse>(
                handle, WaitTimeout);

            UNIT_ASSERT(handle);
            UNIT_ASSERT_VALUES_EQUAL(cookie, handle->Cookie);
        }

        {
            auto request = diskAgent.CreateSecureEraseDeviceRequest(
                "MemoryDevice2");

            const ui64 cookie = 42;

            diskAgent.SendRequest(MakeDiskAgentServiceId(), std::move(request), cookie);

            TAutoPtr<IEventHandle> handle;
            runtime.GrabEdgeEventRethrow<TEvDiskAgent::TEvSecureEraseDeviceResponse>(
                handle, WaitTimeout);

            UNIT_ASSERT(handle);
            UNIT_ASSERT_VALUES_EQUAL(cookie, handle->Cookie);
        }

        {
            auto request = diskAgent.CreateAcquireDevicesRequest(
                {"MemoryDevice2"},
                "client-id",
                NProto::VOLUME_ACCESS_READ_WRITE
            );

            const ui64 cookie = 42;

            diskAgent.SendRequest(MakeDiskAgentServiceId(), std::move(request), cookie);

            TAutoPtr<IEventHandle> handle;
            runtime.GrabEdgeEventRethrow<TEvDiskAgent::TEvAcquireDevicesResponse>(
                handle, WaitTimeout);

            UNIT_ASSERT(handle);
            UNIT_ASSERT_VALUES_EQUAL(cookie, handle->Cookie);
        }
    }

    Y_UNIT_TEST(ShouldPerformIoWithoutSpdk)
    {
        NProto::TDiskAgentConfig agentConfig;
        agentConfig.SetBackend(NProto::DISK_AGENT_BACKEND_AIO);
        agentConfig.SetAcquireRequired(true);
        agentConfig.SetEnabled(true);

        auto const workingDir = TryGetRamDrivePath();

        *agentConfig.AddFileDevices() = PrepareFileDevice(
            workingDir / "test",
            "FileDevice-1");

        TTestBasicRuntime runtime;

        auto env = TTestEnvBuilder(runtime)
            .With(agentConfig)
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        TVector<TString> uuids { "FileDevice-1" };

        const TString clientId = "client-1";

        diskAgent.AcquireDevices(
            uuids,
            clientId,
            NProto::VOLUME_ACCESS_READ_WRITE
        );

        {
            auto response = diskAgent.CollectStats();

            UNIT_ASSERT_VALUES_EQUAL(0, response->Stats.GetInitErrorsCount());
            UNIT_ASSERT_VALUES_EQUAL(1, response->Stats.GetDeviceStats().size());

            const auto& stats = response->Stats.GetDeviceStats(0);

            UNIT_ASSERT_VALUES_EQUAL("FileDevice-1", stats.GetDeviceUUID());
            UNIT_ASSERT_VALUES_EQUAL(0, stats.GetBytesRead());
            UNIT_ASSERT_VALUES_EQUAL(0, stats.GetNumReadOps());
            UNIT_ASSERT_VALUES_EQUAL(0, stats.GetBytesWritten());
            UNIT_ASSERT_VALUES_EQUAL(0, stats.GetNumWriteOps());
        }

        const size_t startIndex = 512;
        const size_t blocksCount = 2;

        {
            TVector<TString> blocks;
            auto sglist = ResizeBlocks(
                blocks,
                blocksCount,
                TString(DefaultBlockSize, 'A'));

            diskAgent.SendWriteDeviceBlocksRequest(
                uuids[0],
                startIndex,
                sglist,
                clientId);

            runtime.DispatchEvents(TDispatchOptions());

            auto response = diskAgent.RecvWriteDeviceBlocksResponse();

            UNIT_ASSERT(!HasError(response->Record));
        }

        {
            auto response = diskAgent.CollectStats();

            UNIT_ASSERT_VALUES_EQUAL(0, response->Stats.GetInitErrorsCount());
            UNIT_ASSERT_VALUES_EQUAL(1, response->Stats.GetDeviceStats().size());

            const auto& stats = response->Stats.GetDeviceStats(0);

            UNIT_ASSERT_VALUES_EQUAL("FileDevice-1", stats.GetDeviceUUID());
            UNIT_ASSERT_VALUES_EQUAL(0, stats.GetBytesRead());
            UNIT_ASSERT_VALUES_EQUAL(0, stats.GetNumReadOps());
            UNIT_ASSERT_VALUES_EQUAL(
                DefaultBlockSize * blocksCount,
                stats.GetBytesWritten());
            UNIT_ASSERT_VALUES_EQUAL(1, stats.GetNumWriteOps());
        }

        {
            const auto response = ReadDeviceBlocks(
                runtime,
                diskAgent,
                uuids[0],
                startIndex,
                blocksCount,
                clientId);

            const auto& record = response->Record;

            UNIT_ASSERT(!HasError(record));

            auto sglist = ConvertToSgList(record.GetBlocks(), DefaultBlockSize);

            UNIT_ASSERT_VALUES_EQUAL(sglist.size(), blocksCount);

            for (const auto& buffer: sglist) {
                const char* ptr = buffer.Data();
                for (size_t i = 0; i < buffer.Size(); ++i) {
                    UNIT_ASSERT(ptr[i] == 'A');
                }
            }
        }

        {
            auto response = diskAgent.CollectStats();

            UNIT_ASSERT_VALUES_EQUAL(0, response->Stats.GetInitErrorsCount());
            UNIT_ASSERT_VALUES_EQUAL(1, response->Stats.GetDeviceStats().size());

            const auto& stats = response->Stats.GetDeviceStats(0);

            UNIT_ASSERT_VALUES_EQUAL("FileDevice-1", stats.GetDeviceUUID());
            UNIT_ASSERT_VALUES_EQUAL(
                DefaultBlockSize * blocksCount,
                stats.GetBytesRead());
            UNIT_ASSERT_VALUES_EQUAL(1, stats.GetNumReadOps());
            UNIT_ASSERT_VALUES_EQUAL(
                DefaultBlockSize * blocksCount,
                stats.GetBytesWritten());
            UNIT_ASSERT_VALUES_EQUAL(1, stats.GetNumWriteOps());
        }

        ZeroDeviceBlocks(
            runtime,
            diskAgent,
            uuids[0],
            startIndex,
            blocksCount,
            clientId);

        {
            const auto response = ReadDeviceBlocks(
                runtime, diskAgent, uuids[0], startIndex, blocksCount, clientId);

            const auto& record = response->Record;

            UNIT_ASSERT(!HasError(record));

            auto sglist = ConvertToSgList(record.GetBlocks(), DefaultBlockSize);

            UNIT_ASSERT_VALUES_EQUAL(sglist.size(), blocksCount);

            for (const auto& buffer: sglist) {
                const char* ptr = buffer.Data();
                for (size_t i = 0; i < buffer.Size(); ++i) {
                    UNIT_ASSERT(ptr[i] == 0);
                }
            }
        }

        {
            auto response = diskAgent.CollectStats();

            UNIT_ASSERT_VALUES_EQUAL(0, response->Stats.GetInitErrorsCount());
            UNIT_ASSERT_VALUES_EQUAL(1, response->Stats.GetDeviceStats().size());

            const auto& stats = response->Stats.GetDeviceStats(0);

            UNIT_ASSERT_VALUES_EQUAL("FileDevice-1", stats.GetDeviceUUID());
            UNIT_ASSERT_VALUES_EQUAL(
                2 * DefaultBlockSize * blocksCount,
                stats.GetBytesRead());
            UNIT_ASSERT_VALUES_EQUAL(2, stats.GetNumReadOps());
            UNIT_ASSERT_VALUES_EQUAL(
                DefaultBlockSize * blocksCount,
                stats.GetBytesWritten());
            UNIT_ASSERT_VALUES_EQUAL(1, stats.GetNumWriteOps());
        }
    }

    Y_UNIT_TEST(ShouldSupportOffsetAndFileSizeInFileDevices)
    {
        NProto::TDiskAgentConfig agentConfig;
        agentConfig.SetBackend(NProto::DISK_AGENT_BACKEND_AIO);
        agentConfig.SetAcquireRequired(true);
        agentConfig.SetEnabled(true);

        const auto workingDir = TryGetRamDrivePath();
        const auto filePath = workingDir / "test";

        {
            TFile fileData(filePath, EOpenModeFlag::CreateAlways);
            fileData.Resize(16_MB);
        }

        auto prepareFileDevice = [&] (const TString& deviceName) {
            NProto::TFileDeviceArgs device;
            device.SetPath(filePath);
            device.SetBlockSize(DefaultDeviceBlockSize);
            device.SetDeviceId(deviceName);
            return device;
        };

        {
            auto* d = agentConfig.AddFileDevices();
            *d = prepareFileDevice("FileDevice-1");

            d->SetOffset(1_MB);
            d->SetFileSize(4_MB);

            d = agentConfig.AddFileDevices();
            *d = prepareFileDevice("FileDevice-2");

            d->SetOffset(5_MB);
            d->SetFileSize(4_MB);
        }

        TTestBasicRuntime runtime;

        NProto::TAgentConfig registeredAgent;

        runtime.SetObserverFunc([&] (TAutoPtr<IEventHandle>& event) {
                switch (event->GetTypeRewrite()) {
                    case TEvDiskRegistry::EvRegisterAgentRequest: {
                        auto& msg = *event->Get<TEvDiskRegistry::TEvRegisterAgentRequest>();

                        registeredAgent = msg.Record.GetAgentConfig();
                    }
                    break;
                }

                return TTestActorRuntime::DefaultObserverFunc(event);
            });

        auto env = TTestEnvBuilder(runtime)
            .With(agentConfig)
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        UNIT_ASSERT_VALUES_EQUAL(2, registeredAgent.DevicesSize());
        UNIT_ASSERT_VALUES_EQUAL(
            1_MB,
            registeredAgent.GetDevices(0).GetPhysicalOffset());
        UNIT_ASSERT_VALUES_EQUAL(
            5_MB,
            registeredAgent.GetDevices(1).GetPhysicalOffset());

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        TVector<TString> uuids { "FileDevice-1", "FileDevice-2" };

        const TString clientId = "client-1";

        diskAgent.AcquireDevices(
            uuids,
            clientId,
            NProto::VOLUME_ACCESS_READ_WRITE
        );

        auto writeBlocks = [&] (
            char c,
            ui64 startIndex,
            ui32 blockCount,
            ui32 devIdx)
        {
            TVector<TString> blocks;
            auto sglist = ResizeBlocks(
                blocks,
                blockCount,
                TString(DefaultBlockSize, c));

            diskAgent.SendWriteDeviceBlocksRequest(
                uuids[devIdx],
                startIndex,
                sglist,
                clientId);

            runtime.DispatchEvents(TDispatchOptions());

            auto response = diskAgent.RecvWriteDeviceBlocksResponse();

            UNIT_ASSERT_VALUES_EQUAL_C(
                S_OK,
                response->Record.GetError().GetCode(),
                response->Record.GetError().GetMessage());
        };

        auto readBlocks = [&] (
            char c,
            ui64 startIndex,
            ui32 blockCount,
            ui32 devIdx,
            ui32 line)
        {
            const auto response = ReadDeviceBlocks(
                runtime,
                diskAgent,
                uuids[devIdx],
                startIndex,
                blockCount,
                clientId);

            const auto& record = response->Record;

            UNIT_ASSERT_VALUES_EQUAL_C(
                S_OK,
                record.GetError().GetCode(),
                record.GetError().GetMessage());

            auto sglist = ConvertToSgList(record.GetBlocks(), DefaultBlockSize);

            UNIT_ASSERT_VALUES_EQUAL(sglist.size(), blockCount);

            for (const auto& buffer: sglist) {
                UNIT_ASSERT_VALUES_EQUAL_C(
                    TString(DefaultBlockSize, c),
                    TString(buffer.AsStringBuf()),
                    line);
            }
        };

        auto zeroBlocks = [&] (ui64 startIndex, ui32 blockCount, ui32 devIdx)
        {
            ZeroDeviceBlocks(
                runtime,
                diskAgent,
                uuids[devIdx],
                startIndex,
                blockCount,
                clientId);
        };

        writeBlocks('A', 512, 2, 0);
        readBlocks('A', 512, 2, 0, __LINE__);
        readBlocks(0, 512, 2, 1, __LINE__);
        zeroBlocks(512, 2, 0);
        readBlocks(0, 512, 2, 0, __LINE__);

        {
            auto response = diskAgent.CollectStats();

            UNIT_ASSERT_VALUES_EQUAL(0, response->Stats.GetInitErrorsCount());
            UNIT_ASSERT_VALUES_EQUAL(2, response->Stats.GetDeviceStats().size());

            const auto& stats = response->Stats.GetDeviceStats();

            UNIT_ASSERT_VALUES_EQUAL("FileDevice-1", stats[0].GetDeviceUUID());
            UNIT_ASSERT_VALUES_EQUAL(
                4 * DefaultBlockSize,
                stats[0].GetBytesRead());
            UNIT_ASSERT_VALUES_EQUAL(2, stats[0].GetNumReadOps());
            UNIT_ASSERT_VALUES_EQUAL(
                2 * DefaultBlockSize,
                stats[0].GetBytesWritten());
            UNIT_ASSERT_VALUES_EQUAL(1, stats[0].GetNumWriteOps());
            UNIT_ASSERT_VALUES_EQUAL("FileDevice-2", stats[1].GetDeviceUUID());
            UNIT_ASSERT_VALUES_EQUAL(
                2 * DefaultBlockSize,
                stats[1].GetBytesRead());
            UNIT_ASSERT_VALUES_EQUAL(1, stats[1].GetNumReadOps());
            UNIT_ASSERT_VALUES_EQUAL(0, stats[1].GetBytesWritten());
            UNIT_ASSERT_VALUES_EQUAL(0, stats[1].GetNumWriteOps());
        }

        writeBlocks('A', 512, 2, 1);
        readBlocks('A', 512, 2, 1, __LINE__);
        readBlocks(0, 512, 2, 0, __LINE__);
        zeroBlocks(512, 2, 1);
        readBlocks(0, 512, 2, 1, __LINE__);

        {
            auto response = diskAgent.CollectStats();

            UNIT_ASSERT_VALUES_EQUAL(0, response->Stats.GetInitErrorsCount());
            UNIT_ASSERT_VALUES_EQUAL(2, response->Stats.GetDeviceStats().size());

            const auto& stats = response->Stats.GetDeviceStats();

            UNIT_ASSERT_VALUES_EQUAL("FileDevice-1", stats[0].GetDeviceUUID());
            UNIT_ASSERT_VALUES_EQUAL(
                6 * DefaultBlockSize,
                stats[0].GetBytesRead());
            UNIT_ASSERT_VALUES_EQUAL(3, stats[0].GetNumReadOps());
            UNIT_ASSERT_VALUES_EQUAL(
                2 * DefaultBlockSize,
                stats[0].GetBytesWritten());
            UNIT_ASSERT_VALUES_EQUAL(1, stats[0].GetNumWriteOps());
            UNIT_ASSERT_VALUES_EQUAL("FileDevice-2", stats[1].GetDeviceUUID());
            UNIT_ASSERT_VALUES_EQUAL(
                6 * DefaultBlockSize,
                stats[1].GetBytesRead());
            UNIT_ASSERT_VALUES_EQUAL(3, stats[1].GetNumReadOps());
            UNIT_ASSERT_VALUES_EQUAL(
                2 * DefaultBlockSize,
                stats[1].GetBytesWritten());
            UNIT_ASSERT_VALUES_EQUAL(1, stats[1].GetNumWriteOps());
        }

        writeBlocks('B', 512, 2, 0);
        writeBlocks('C', 512, 2, 1);

        TFile f(filePath, EOpenModeFlag::RdOnly);
        TString buffer(2 * DefaultBlockSize, 0);
        size_t bytes = f.Pread(
            buffer.begin(),
            2 * DefaultBlockSize,
            1_MB + 512 * DefaultBlockSize);
        UNIT_ASSERT_VALUES_EQUAL(2 * DefaultBlockSize, bytes);
        UNIT_ASSERT_VALUES_EQUAL(TString(2 * DefaultBlockSize, 'B'), buffer);
        bytes = f.Pread(
            buffer.begin(),
            2 * DefaultBlockSize,
            5_MB + 512 * DefaultBlockSize);
        UNIT_ASSERT_VALUES_EQUAL(2 * DefaultBlockSize, bytes);
        UNIT_ASSERT_VALUES_EQUAL(TString(2 * DefaultBlockSize, 'C'), buffer);

        diskAgent.ReleaseDevices(uuids, clientId);

        diskAgent.SecureEraseDevice("FileDevice-1");
        bytes = f.Pread(
            buffer.begin(),
            2 * DefaultBlockSize,
            1_MB + 512 * DefaultBlockSize);
        UNIT_ASSERT_VALUES_EQUAL(2 * DefaultBlockSize, bytes);
        UNIT_ASSERT_VALUES_EQUAL(TString(2 * DefaultBlockSize, 0), buffer);
        bytes = f.Pread(
            buffer.begin(),
            2 * DefaultBlockSize,
            5_MB + 512 * DefaultBlockSize);
        UNIT_ASSERT_VALUES_EQUAL(2 * DefaultBlockSize, bytes);
        UNIT_ASSERT_VALUES_EQUAL(TString(2 * DefaultBlockSize, 'C'), buffer);

        diskAgent.SecureEraseDevice("FileDevice-2");
        bytes = f.Pread(
            buffer.begin(),
            2 * DefaultBlockSize,
            5_MB + 512 * DefaultBlockSize);
        UNIT_ASSERT_VALUES_EQUAL(2 * DefaultBlockSize, bytes);
        UNIT_ASSERT_VALUES_EQUAL(TString(2 * DefaultBlockSize, 0), buffer);
    }

    Y_UNIT_TEST(ShouldHandleDeviceInitError)
    {
        NProto::TDiskAgentConfig agentConfig;
        agentConfig.SetBackend(NProto::DISK_AGENT_BACKEND_AIO);
        agentConfig.SetAgentId("agent-id");
        agentConfig.SetEnabled(true);

        auto const workingDir = TryGetRamDrivePath();

        *agentConfig.AddFileDevices() = PrepareFileDevice(
            workingDir / "test-1",
            "FileDevice-1");

        {
            NProto::TFileDeviceArgs& device = *agentConfig.AddFileDevices();

            device.SetPath(workingDir / "not-exists");
            device.SetBlockSize(DefaultDeviceBlockSize);
            device.SetDeviceId("FileDevice-2");
        }

        *agentConfig.AddFileDevices() = PrepareFileDevice(
            workingDir / "broken",
            "FileDevice-3");

        *agentConfig.AddFileDevices() = PrepareFileDevice(
            workingDir / "test-4",
            "FileDevice-4");

        TTestBasicRuntime runtime;

        NProto::TAgentConfig registeredAgent;

        runtime.SetObserverFunc([&] (TAutoPtr<IEventHandle>& event) {
                switch (event->GetTypeRewrite()) {
                    case TEvDiskRegistry::EvRegisterAgentRequest: {
                        auto& msg = *event->Get<TEvDiskRegistry::TEvRegisterAgentRequest>();

                        registeredAgent = msg.Record.GetAgentConfig();
                    }
                    break;
                }

                return TTestActorRuntime::DefaultObserverFunc(event);
            });

        auto env = TTestEnvBuilder(runtime)
            .With(agentConfig)
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        UNIT_ASSERT_VALUES_EQUAL("agent-id", registeredAgent.GetAgentId());
        UNIT_ASSERT_VALUES_EQUAL(4, registeredAgent.DevicesSize());

        auto& devices = *registeredAgent.MutableDevices();
        SortBy(devices.begin(), devices.end(), [] (const auto& device) {
            return device.GetDeviceUUID();
        });

        auto expectedBlocksCount =
            DefaultBlocksCount * DefaultBlockSize / DefaultDeviceBlockSize;

        UNIT_ASSERT_VALUES_EQUAL("FileDevice-1", devices[0].GetDeviceUUID());
        UNIT_ASSERT_VALUES_EQUAL("the-rack", devices[0].GetRack());
        UNIT_ASSERT_VALUES_EQUAL(
            expectedBlocksCount,
            devices[0].GetBlocksCount()
        );
        UNIT_ASSERT_VALUES_EQUAL(
            static_cast<int>(NProto::DEVICE_STATE_ONLINE),
            static_cast<int>(devices[0].GetState())
        );

        UNIT_ASSERT_VALUES_EQUAL("FileDevice-2", devices[1].GetDeviceUUID());
        UNIT_ASSERT_VALUES_EQUAL("the-rack", devices[1].GetRack());
        UNIT_ASSERT_VALUES_EQUAL(1, devices[1].GetBlocksCount());
        UNIT_ASSERT_VALUES_EQUAL(
            static_cast<int>(NProto::DEVICE_STATE_ERROR),
            static_cast<int>(devices[1].GetState())
        );

        UNIT_ASSERT_VALUES_EQUAL("FileDevice-3", devices[2].GetDeviceUUID());
        UNIT_ASSERT_VALUES_EQUAL("the-rack", devices[2].GetRack());
        UNIT_ASSERT_VALUES_EQUAL(
            expectedBlocksCount,
            devices[2].GetBlocksCount()
        );
        UNIT_ASSERT_VALUES_EQUAL(
            static_cast<int>(NProto::DEVICE_STATE_ERROR),
            static_cast<int>(devices[2].GetState())
        );

        UNIT_ASSERT_VALUES_EQUAL("FileDevice-4", devices[3].GetDeviceUUID());
        UNIT_ASSERT_VALUES_EQUAL("the-rack", devices[3].GetRack());
        UNIT_ASSERT_VALUES_EQUAL(
            expectedBlocksCount,
            devices[3].GetBlocksCount()
        );
        UNIT_ASSERT_VALUES_EQUAL(
            static_cast<int>(NProto::DEVICE_STATE_ONLINE),
            static_cast<int>(devices[3].GetState())
        );

        auto response = diskAgent.CollectStats();
        const auto& stats = response->Stats;
        UNIT_ASSERT_VALUES_EQUAL(2, stats.GetInitErrorsCount());
    }

    Y_UNIT_TEST(ShouldHandleSpdkInitError)
    {
        TVector<NProto::TDeviceConfig> devices;

        NProto::TDiskAgentConfig agentConfig;
        agentConfig.SetEnabled(true);
        agentConfig.SetAgentId("agent");
        agentConfig.SetBackend(NProto::DISK_AGENT_BACKEND_SPDK);

        for (size_t i = 0; i < 10; i++) {
            auto& device = devices.emplace_back();
            device.SetDeviceName(Sprintf("%s%zu", (i & 1 ? "broken" : "file"), i));
            device.SetDeviceUUID(CreateGuidAsString());

            auto* config = agentConfig.MutableFileDevices()->Add();
            config->SetPath(device.GetDeviceName());
            config->SetDeviceId(device.GetDeviceUUID());
            config->SetBlockSize(4096);
        }

        auto nvmeConfig = agentConfig.MutableNvmeDevices()->Add();
        nvmeConfig->SetBaseName("broken");

        for (size_t i = 0; i < 10; i++) {
            auto& device = devices.emplace_back();
            device.SetDeviceName(Sprintf("agent:broken:n%zu", i));
            device.SetDeviceUUID(CreateGuidAsString());

            *nvmeConfig->MutableDeviceIds()->Add() = device.GetDeviceUUID();
        }

        TTestBasicRuntime runtime;
        TTestEnv env = TTestEnvBuilder(runtime)
            .With(agentConfig)
            .With(std::make_shared<TTestSpdkEnv>(devices))
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        UNIT_ASSERT_VALUES_EQUAL(devices.size(), env.DiskRegistryState->Devices.size());

        for (const auto& expected: devices) {
            const auto& deviceName = expected.GetDeviceName();
            const auto& device = env.DiskRegistryState->Devices.at(deviceName);

            UNIT_ASSERT_VALUES_EQUAL(device.GetDeviceUUID(), expected.GetDeviceUUID());

            if (deviceName.Contains("broken")) {
                UNIT_ASSERT_EQUAL(device.GetState(), NProto::DEVICE_STATE_ERROR);
            } else {
                UNIT_ASSERT_EQUAL(device.GetState(), NProto::DEVICE_STATE_ONLINE);
            }
        }
    }

    Y_UNIT_TEST(ShouldSecureEraseAioDevice)
    {
        NProto::TDiskAgentConfig agentConfig;
        agentConfig.SetBackend(NProto::DISK_AGENT_BACKEND_AIO);
        agentConfig.SetDeviceEraseMethod(NProto::DEVICE_ERASE_METHOD_CRYPTO_ERASE);
        agentConfig.SetAgentId("agent-id");
        agentConfig.SetEnabled(true);

        auto const workingDir = TryGetRamDrivePath();

        *agentConfig.AddFileDevices() = PrepareFileDevice(
            workingDir / "test-1",
            "FileDevice-1");

        TTestBasicRuntime runtime;

        auto env = TTestEnvBuilder(runtime)
            .With(agentConfig)
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();
        diskAgent.SecureEraseDevice("FileDevice-1");
    }

    Y_UNIT_TEST(ShouldZeroFillDevice)
    {
        NProto::TDiskAgentConfig agentConfig;
        agentConfig.SetBackend(NProto::DISK_AGENT_BACKEND_AIO);
        agentConfig.SetDeviceEraseMethod(NProto::DEVICE_ERASE_METHOD_ZERO_FILL);
        agentConfig.SetAgentId("agent-id");
        agentConfig.SetEnabled(true);
        agentConfig.SetAcquireRequired(true);

        auto const workingDir = TryGetRamDrivePath();

        *agentConfig.AddFileDevices() = PrepareFileDevice(
            workingDir / "test-1",
            "FileDevice-1");

        TTestBasicRuntime runtime;

        auto env = TTestEnvBuilder(runtime)
            .With(agentConfig)
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        const TString clientId = "client-1";
        const TVector<TString> uuids { "FileDevice-1" };

        diskAgent.AcquireDevices(
            uuids,
            clientId,
            NProto::VOLUME_ACCESS_READ_WRITE
        );

        const ui64 startIndex = 10;
        const ui32 blockCount = 16_KB / DefaultBlockSize;
        const TString content(DefaultBlockSize, 'A');

        TVector<TString> blocks;
        auto sglist = ResizeBlocks(
            blocks,
            blockCount,
            content);

        WriteDeviceBlocks(
            runtime, diskAgent, uuids[0], startIndex, sglist, clientId);

        {
            auto response = ReadDeviceBlocks(
                runtime,
                diskAgent,
                uuids[0],
                startIndex,
                blockCount,
                clientId);

            auto data = ConvertToSgList(
                response->Record.GetBlocks(),
                DefaultBlockSize);

            UNIT_ASSERT_VALUES_EQUAL(blockCount, data.size());
            for (TBlockDataRef dr: data) {
                UNIT_ASSERT_STRINGS_EQUAL(content, dr.AsStringBuf());
            }
        }

        diskAgent.ReleaseDevices(uuids, clientId);

        diskAgent.SecureEraseDevice("FileDevice-1");

        diskAgent.AcquireDevices(
            uuids,
            clientId,
            NProto::VOLUME_ACCESS_READ_WRITE
        );

        {
            auto response = ReadDeviceBlocks(
                runtime,
                diskAgent,
                uuids[0],
                startIndex,
                blockCount,
                clientId);

            auto data = ConvertToSgList(
                response->Record.GetBlocks(),
                DefaultBlockSize);

            UNIT_ASSERT_VALUES_EQUAL(blockCount, data.size());

            for (TBlockDataRef dr: data) {
                for (char c: dr.AsStringBuf()) {
                    UNIT_ASSERT_VALUES_EQUAL(0, c);
                }
            }
        }

        diskAgent.ReleaseDevices(uuids, clientId);
    }

    Y_UNIT_TEST(ShouldInjectExceptions)
    {
        NLWTrace::TManager traceManager(
            *Singleton<NLWTrace::TProbeRegistry>(),
            true);  // allowDestructiveActions

        traceManager.RegisterCustomAction(
            "ServiceErrorAction", &CreateServiceErrorActionExecutor);

        // read errors for MemoryDevice1 & zero errors for MemoryDevice2
        traceManager.New("env", QueryFromString(R"(
            Blocks {
                ProbeDesc {
                    Name: "FaultInjection"
                    Provider: "BLOCKSTORE_DISK_AGENT_PROVIDER"
                }
                Action {
                    StatementAction {
                        Type: ST_INC
                        Argument { Variable: "counter1" }
                    }
                }
                Action {
                    StatementAction {
                        Type: ST_MOD
                        Argument { Variable: "counter1" }
                        Argument { Variable: "counter1" }
                        Argument { Value: "100" }
                    }
                }
                Predicate {
                    Operators {
                        Type: OT_EQ
                        Argument { Param: "name" }
                        Argument { Value: "ReadDeviceBlocks" }
                    }
                    Operators {
                        Type: OT_EQ
                        Argument { Param: "deviceId" }
                        Argument { Value: "MemoryDevice1" }
                    }
                }
            }
            Blocks {
                ProbeDesc {
                    Name: "FaultInjection"
                    Provider: "BLOCKSTORE_DISK_AGENT_PROVIDER"
                }
                Action {
                    CustomAction {
                        Name: "ServiceErrorAction"
                        Opts: "E_IO"
                        Opts: "Io Error"
                    }
                }
                Predicate {
                    Operators {
                        Type: OT_EQ
                        Argument { Variable: "counter1" }
                        Argument { Value: "99" }
                    }
                    Operators {
                        Type: OT_EQ
                        Argument { Param: "name" }
                        Argument { Value: "ReadDeviceBlocks" }
                    }
                    Operators {
                        Type: OT_EQ
                        Argument { Param: "deviceId" }
                        Argument { Value: "MemoryDevice1" }
                    }
                }
            }

            Blocks {
                ProbeDesc {
                    Name: "FaultInjection"
                    Provider: "BLOCKSTORE_DISK_AGENT_PROVIDER"
                }
                Action {
                    StatementAction {
                        Type: ST_INC
                        Argument { Variable: "counter2" }
                    }
                }
                Action {
                    StatementAction {
                        Type: ST_MOD
                        Argument { Variable: "counter2" }
                        Argument { Variable: "counter2" }
                        Argument { Value: "100" }
                    }
                }
                Predicate {
                    Operators {
                        Type: OT_EQ
                        Argument { Param: "name" }
                        Argument { Value: "ZeroDeviceBlocks" }
                    }
                    Operators {
                        Type: OT_EQ
                        Argument { Param: "deviceId" }
                        Argument { Value: "MemoryDevice2" }
                    }
                }
            }
            Blocks {
                ProbeDesc {
                    Name: "FaultInjection"
                    Provider: "BLOCKSTORE_DISK_AGENT_PROVIDER"
                }
                Action {
                    CustomAction {
                        Name: "ServiceErrorAction"
                        Opts: "E_IO"
                        Opts: "Io Error"
                    }
                }
                Predicate {
                    Operators {
                        Type: OT_EQ
                        Argument { Variable: "counter2" }
                        Argument { Value: "99" }
                    }
                    Operators {
                        Type: OT_EQ
                        Argument { Param: "name" }
                        Argument { Value: "ZeroDeviceBlocks" }
                    }
                    Operators {
                        Type: OT_EQ
                        Argument { Param: "deviceId" }
                        Argument { Value: "MemoryDevice2" }
                    }
                }
            }
        )"));

        TTestBasicRuntime runtime;

        auto env = TTestEnvBuilder(runtime)
            .With(DiskAgentConfig({
                "MemoryDevice1",
                "MemoryDevice2",
                "MemoryDevice3",
            }))
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        const TVector<TString> uuids{
            "MemoryDevice1",
            "MemoryDevice2"
        };

        const TString clientId = "client-1";

        diskAgent.AcquireDevices(
            uuids,
            clientId,
            NProto::VOLUME_ACCESS_READ_WRITE);

        auto read = [&] (int index) {
            const auto response = ReadDeviceBlocks(
                runtime, diskAgent, uuids[index], 0, 1, clientId);

            return response->GetStatus() == E_IO;
        };

        TString data(DefaultBlockSize, 'X');
        TSgList sglist = {{ data.data(), data.size() }};

        auto write = [&] (int index) {
            diskAgent.SendWriteDeviceBlocksRequest(uuids[index], 0, sglist, clientId);
            runtime.DispatchEvents(TDispatchOptions());
            auto response = diskAgent.RecvWriteDeviceBlocksResponse();

            return response->GetStatus() == E_IO;
        };

        auto zero = [&] (int index) {
            diskAgent.SendZeroDeviceBlocksRequest(uuids[index], 0, 1, clientId);
            runtime.DispatchEvents(TDispatchOptions());
            const auto response = diskAgent.RecvZeroDeviceBlocksResponse();

            return response->GetStatus() == E_IO;
        };

        {
            int errors = 0;

            for (int i = 0; i != 1000; ++i) {
                errors += read(0);
            }

            UNIT_ASSERT_VALUES_EQUAL(1000 / 100, errors);
        }

        for (int i = 0; i != 2000; ++i) {
            UNIT_ASSERT(!read(1));

            UNIT_ASSERT(!write(0));
            UNIT_ASSERT(!write(1));

            UNIT_ASSERT(!zero(0));
        }

        {
            int errors = 0;

            for (int i = 0; i != 1000; ++i) {
                errors += zero(1);
            }

            UNIT_ASSERT_VALUES_EQUAL(1000 / 100, errors);
        }
    }

    Y_UNIT_TEST(ShouldRegisterAfterDisconnect)
    {
        int registrationCount = 0;

        TTestBasicRuntime runtime;

        runtime.SetObserverFunc([&] (TAutoPtr<IEventHandle>& event) {
                switch (event->GetTypeRewrite()) {
                    case TEvDiskRegistry::EvRegisterAgentRequest:
                        ++registrationCount;
                    break;
                }

                return TTestActorRuntime::DefaultObserverFunc(event);
            });

        auto env = TTestEnvBuilder(runtime)
            .With(DiskAgentConfig({"MemoryDevice1"}))
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        UNIT_ASSERT_VALUES_EQUAL(1, registrationCount);

        diskAgent.SendRequest(
            MakeDiskAgentServiceId(),
            std::make_unique<TEvDiskRegistryProxy::TEvConnectionLost>());

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        UNIT_ASSERT_VALUES_EQUAL(2, registrationCount);
    }

    Y_UNIT_TEST(ShouldSecureEraseDeviceWithExpiredClient)
    {
        TTestBasicRuntime runtime;

        const TVector<TString> uuids {
            "MemoryDevice1",
            "MemoryDevice2",
            "MemoryDevice3"
        };

        auto env = TTestEnvBuilder(runtime)
            .With(DiskAgentConfig(uuids))
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        diskAgent.AcquireDevices(
            uuids,
            "client-1",
            NProto::VOLUME_ACCESS_READ_WRITE
        );

        auto secureErase = [&] (auto expectedErrorCode) {
            for (const auto& uuid: uuids) {
                diskAgent.SendSecureEraseDeviceRequest(uuid);

                auto response = diskAgent.RecvSecureEraseDeviceResponse();
                UNIT_ASSERT_VALUES_EQUAL(
                    expectedErrorCode,
                    response->Record.GetError().GetCode()
                );
            }
        };

        secureErase(E_INVALID_STATE);

        runtime.AdvanceCurrentTime(TDuration::Seconds(10));

        secureErase(S_OK);
    }

    Y_UNIT_TEST(ShouldRegisterDevices)
    {
        TVector<NProto::TDeviceConfig> devices;
        devices.reserve(15);
        for (size_t i = 0; i != 15; ++i) {
            auto& device = devices.emplace_back();

            device.SetDeviceName(Sprintf("/dev/disk/by-partlabel/NVMENBS%02lu", i + 1));
            device.SetBlockSize(4096);
            device.SetDeviceUUID(CreateGuidAsString());
            if (i != 11) {
                device.SetBlocksCount(24151552);
            } else {
                device.SetBlocksCount(24169728);
            }
        }

        TTestBasicRuntime runtime;

        TTestEnv env = TTestEnvBuilder(runtime)
            .With([&] {
                NProto::TDiskAgentConfig agentConfig;
                agentConfig.SetEnabled(true);
                for (const auto& device: devices) {
                    auto* config = agentConfig.MutableFileDevices()->Add();
                    config->SetPath(device.GetDeviceName());
                    config->SetBlockSize(device.GetBlockSize());
                    config->SetDeviceId(device.GetDeviceUUID());
                }

                return agentConfig;
            }())
            .With(std::make_shared<TTestSpdkEnv>(devices))
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        UNIT_ASSERT_VALUES_EQUAL(
            devices.size(),
            env.DiskRegistryState->Devices.size()
        );

        for (const auto& expected: devices) {
            const auto& device = env.DiskRegistryState->Devices.at(
                expected.GetDeviceName()
            );

            UNIT_ASSERT_VALUES_EQUAL(
                expected.GetDeviceUUID(),
                device.GetDeviceUUID()
            );
            UNIT_ASSERT_VALUES_EQUAL(
                expected.GetDeviceName(),
                device.GetDeviceName()
            );
            UNIT_ASSERT_VALUES_EQUAL(
                expected.GetBlockSize(),
                device.GetBlockSize()
            );
            UNIT_ASSERT_VALUES_EQUAL(
                expected.GetBlocksCount(),
                device.GetBlocksCount()
            );
        }
    }

    Y_UNIT_TEST(ShouldLimitSecureEraseRequests)
    {
        TTestBasicRuntime runtime;

        auto config = DiskAgentConfig({"foo", "bar"});
        config.SetDeviceEraseMethod(NProto::DEVICE_ERASE_METHOD_USER_DATA_ERASE);

        TVector<NProto::TDeviceConfig> devices;

        for (auto& md: config.GetMemoryDevices()) {
            auto& device = devices.emplace_back();
            device.SetDeviceName(md.GetName());
            device.SetDeviceUUID(md.GetDeviceId());
            device.SetBlocksCount(md.GetBlocksCount());
            device.SetBlockSize(md.GetBlockSize());
        }

        auto spdk = std::make_shared<TTestSpdkEnv>(devices);

        auto env = TTestEnvBuilder(runtime)
            .With(config)
            .With(spdk)
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        auto foo = NewPromise<NProto::TError>();
        auto bar = NewPromise<NProto::TError>();

        spdk->SecureEraseResult = foo;

        for (int i = 0; i != 100; ++i) {
            diskAgent.SendSecureEraseDeviceRequest("foo");
        }

        runtime.DispatchEvents(TDispatchOptions(), TDuration::MilliSeconds(10));
        UNIT_ASSERT_VALUES_EQUAL(1, AtomicGet(spdk->SecureEraseCount));

        for (int i = 0; i != 100; ++i) {
            diskAgent.SendSecureEraseDeviceRequest("bar");
        }

        runtime.DispatchEvents(TDispatchOptions(), TDuration::MilliSeconds(10));
        UNIT_ASSERT_VALUES_EQUAL(1, AtomicGet(spdk->SecureEraseCount));

        spdk->SecureEraseResult = bar;
        foo.SetValue(NProto::TError());

        for (int i = 0; i != 100; ++i) {
            auto response = diskAgent.RecvSecureEraseDeviceResponse();
            UNIT_ASSERT_VALUES_EQUAL(
                S_OK,
                response->Record.GetError().GetCode()
            );
        }

        bar.SetValue(NProto::TError());

        for (int i = 0; i != 100; ++i) {
            auto response = diskAgent.RecvSecureEraseDeviceResponse();
            UNIT_ASSERT_VALUES_EQUAL(
                S_OK,
                response->Record.GetError().GetCode()
            );
        }

        UNIT_ASSERT_VALUES_EQUAL(2, AtomicGet(spdk->SecureEraseCount));
    }

    Y_UNIT_TEST(ShouldRejectSecureEraseRequestsOnPoisonPill)
    {
        TTestBasicRuntime runtime;

        NProto::TDeviceConfig device;

        device.SetDeviceName("uuid");
        device.SetDeviceUUID("uuid");
        device.SetBlocksCount(1024);
        device.SetBlockSize(DefaultBlockSize);

        auto spdk = std::make_shared<TTestSpdkEnv>(TVector{device});

        auto env = TTestEnvBuilder(runtime)
            .With([]{
                auto config = DiskAgentConfig({"uuid"});
                config.SetDeviceEraseMethod(NProto::DEVICE_ERASE_METHOD_USER_DATA_ERASE);
                return config;
            }())
            .With(spdk)
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        spdk->SecureEraseResult = NewPromise<NProto::TError>();

        for (int i = 0; i != 100; ++i) {
            diskAgent.SendSecureEraseDeviceRequest("uuid");
        }

        diskAgent.SendRequest(
            MakeDiskAgentServiceId(),
            std::make_unique<TEvents::TEvPoisonPill>());

        for (int i = 0; i != 100; ++i) {
            auto response = diskAgent.RecvSecureEraseDeviceResponse();
            UNIT_ASSERT_VALUES_EQUAL(
                E_REJECTED,
                response->Record.GetError().GetCode()
            );
        }

        UNIT_ASSERT_VALUES_EQUAL(1, AtomicGet(spdk->SecureEraseCount));
    }

    Y_UNIT_TEST(ShouldRejectIORequestsDuringSecureErase)
    {
        TTestBasicRuntime runtime;

        NProto::TDeviceConfig device;

        device.SetDeviceName("uuid");
        device.SetDeviceUUID("uuid");
        device.SetBlocksCount(1024);
        device.SetBlockSize(DefaultBlockSize);

        auto spdk = std::make_shared<TTestSpdkEnv>(TVector{device});

        auto env = TTestEnvBuilder(runtime)
            .With([]{
                auto config = DiskAgentConfig({"uuid"}, false);
                config.SetDeviceEraseMethod(NProto::DEVICE_ERASE_METHOD_USER_DATA_ERASE);
                return config;
            }())
            .With(spdk)
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        spdk->SecureEraseResult = NewPromise<NProto::TError>();

        diskAgent.SendSecureEraseDeviceRequest("uuid");

        auto io = [&] (auto expectedErrorCode) {
            UNIT_ASSERT_VALUES_EQUAL(
                expectedErrorCode,
                ReadDeviceBlocks(runtime, diskAgent, "uuid", 0, 1, "")
                    ->Record.GetError().GetCode());

            UNIT_ASSERT_VALUES_EQUAL(
                expectedErrorCode,
                ZeroDeviceBlocks(runtime, diskAgent, "uuid", 0, 1, "")
                    ->Record.GetError().GetCode());

            {
                TVector<TString> blocks;
                auto sglist = ResizeBlocks(
                    blocks,
                    1,
                    TString(DefaultBlockSize, 'X'));

                UNIT_ASSERT_VALUES_EQUAL(
                    expectedErrorCode,
                    WriteDeviceBlocks(runtime, diskAgent, "uuid", 0, sglist, "")
                        ->Record.GetError().GetCode());
            }
        };

        io(E_REJECTED);

        spdk->SecureEraseResult.SetValue(NProto::TError());
        runtime.DispatchEvents(TDispatchOptions(), TDuration::MilliSeconds(10));

        io(S_OK);

        {
            auto response = diskAgent.RecvSecureEraseDeviceResponse();
            UNIT_ASSERT_VALUES_EQUAL(
                S_OK,
                response->Record.GetError().GetCode()
            );
        }

        UNIT_ASSERT_VALUES_EQUAL(1, AtomicGet(spdk->SecureEraseCount));
    }

    Y_UNIT_TEST(ShouldChecksumDeviceBlocks)
    {
        TTestBasicRuntime runtime;

        const auto blockSize = DefaultBlockSize;
        const auto blocksCount = 10;

        auto env = TTestEnvBuilder(runtime)
            .With([&] {
                NProto::TDiskAgentConfig config;
                config.SetBackend(NProto::DISK_AGENT_BACKEND_AIO);
                config.SetAcquireRequired(true);
                config.SetEnabled(true);

                *config.AddMemoryDevices() = PrepareMemoryDevice(
                    "MemoryDevice1",
                    blockSize,
                    blocksCount * blockSize);

                return config;
            }())
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        const TVector<TString> uuids{
            "MemoryDevice1",
        };

        const TString clientId = "client-1";

        diskAgent.AcquireDevices(
            uuids,
            clientId,
            NProto::VOLUME_ACCESS_READ_WRITE
        );

        TBlockChecksum checksum;

        {
            TVector<TString> blocks;
            auto sglist = ResizeBlocks(
                blocks,
                blocksCount,
                TString(blockSize, 'X'));

            for (const auto& block : blocks) {
                checksum.Extend(block.Data(), block.Size());
            }

            WriteDeviceBlocks(runtime, diskAgent, uuids[0], 0, sglist, clientId);
        }

        {
            const auto response = ChecksumDeviceBlocks(
                runtime, diskAgent, uuids[0], 0, blocksCount, clientId);

            const auto& record = response->Record;

            UNIT_ASSERT_VALUES_EQUAL(checksum.GetValue(), record.GetChecksum());
        }
    }

    Y_UNIT_TEST(ShouldDisableAndEnableAgentDevice)
    {
        TTestBasicRuntime runtime;

        const auto blockSize = DefaultBlockSize;
        const auto blocksCount = 10;

        auto env = TTestEnvBuilder(runtime)
            .With([&] {
                NProto::TDiskAgentConfig config;
                config.SetBackend(NProto::DISK_AGENT_BACKEND_AIO);
                config.SetAcquireRequired(true);
                config.SetEnabled(true);

                *config.AddMemoryDevices() = PrepareMemoryDevice(
                    "MemoryDevice1",
                    blockSize,
                    blocksCount * blockSize);
                *config.AddMemoryDevices() = PrepareMemoryDevice(
                    "MemoryDevice2",
                    blockSize,
                    blocksCount * blockSize);
                *config.AddMemoryDevices() = PrepareMemoryDevice(
                    "MemoryDevice3",
                    blockSize,
                    blocksCount * blockSize);

                return config;
            }())
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        const TVector<TString> uuids{
            "MemoryDevice1",
            "MemoryDevice2",
            "MemoryDevice3",
        };

        const TString clientId = "client-1";

        diskAgent.AcquireDevices(
            uuids,
            clientId,
            NProto::VOLUME_ACCESS_READ_WRITE
        );

        // Disable device 0 and 1
        diskAgent.DisableConcreteAgent(TVector<TString>{uuids[0], uuids[1]});

        {
            TVector<TString> blocks;
            auto sglist = ResizeBlocks(
                blocks,
                blocksCount,
                TString(blockSize, 'X'));

            TAutoPtr<NActors::IEventHandle> handle;

            // Device 0 should genereate IO error.
            diskAgent.SendWriteDeviceBlocksRequest(uuids[0], 0, sglist, clientId);
            auto response = diskAgent.RecvWriteDeviceBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(
                E_IO,
                response->Record.GetError().GetCode());

            // Device 1 should genereate IO error.
            diskAgent.SendWriteDeviceBlocksRequest(uuids[1], 0, sglist, clientId);
            response = diskAgent.RecvWriteDeviceBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(
                E_IO,
                response->Record.GetError().GetCode());

            // Device 2 has not been disabled and should be able to handle requests
            response =
                diskAgent.WriteDeviceBlocks(uuids[2], 0, sglist, clientId);
            UNIT_ASSERT_VALUES_EQUAL(
                S_OK,
                response->Record.GetError().GetCode());
        }

        {
            auto response = diskAgent.CollectStats();
            auto& stats = response->Stats;

            UNIT_ASSERT_VALUES_EQUAL(0, response->Stats.GetInitErrorsCount());
            UNIT_ASSERT_VALUES_EQUAL(3, stats.DeviceStatsSize());

            SortBy(*stats.MutableDeviceStats(), [] (auto& x) {
                return x.GetDeviceUUID();
            });

            // Here we get two erros for disabled devices.
            // First for DisableConcreteAgent, second for failed request.
            UNIT_ASSERT_VALUES_EQUAL(2, stats.GetDeviceStats(0).GetErrors());
            UNIT_ASSERT_VALUES_EQUAL(2, stats.GetDeviceStats(1).GetErrors());
            UNIT_ASSERT_VALUES_EQUAL(0, stats.GetDeviceStats(2).GetErrors());
        }

        // Enable device 1 back
        diskAgent.EnableAgentDevice(uuids[1]);

        {
            TVector<TString> blocks;
            auto sglist = ResizeBlocks(
                blocks,
                blocksCount,
                TString(blockSize, 'X'));

            TAutoPtr<NActors::IEventHandle> handle;

            // Device 0 should genereate IO error.
            diskAgent.SendWriteDeviceBlocksRequest(uuids[0], 0, sglist, clientId);
            auto response = diskAgent.RecvWriteDeviceBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(
                E_IO,
                response->Record.GetError().GetCode());

            // Device 1 has been enabled and should be able to handle requests
            response =
                diskAgent.WriteDeviceBlocks(uuids[1], 0, sglist, clientId);
            UNIT_ASSERT_VALUES_EQUAL(
                S_OK,
                response->Record.GetError().GetCode());

            // Device 2 has not been disabled and should be able to handle requests
            response =
                diskAgent.WriteDeviceBlocks(uuids[2], 0, sglist, clientId);
            UNIT_ASSERT_VALUES_EQUAL(
                S_OK,
                response->Record.GetError().GetCode());
        }
    }

    Y_UNIT_TEST(ShouldReceiveIOErrorFromBrokenDevice)
    {
        NProto::TDiskAgentConfig agentConfig;
        agentConfig.SetBackend(NProto::DISK_AGENT_BACKEND_AIO);
        agentConfig.SetAgentId("agent-id");
        agentConfig.SetEnabled(true);

        const TVector<TString> uuids {"FileDevice-1"};

        *agentConfig.AddFileDevices() = PrepareFileDevice("broken", uuids[0]);

        TTestBasicRuntime runtime;

        auto env = TTestEnvBuilder(runtime)
            .With(agentConfig)
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        const TString clientId = "client-1";

        diskAgent.AcquireDevices(
            uuids,
            clientId,
            NProto::VOLUME_ACCESS_READ_WRITE
        );

        const auto response = ReadDeviceBlocks(
            runtime, diskAgent, uuids[0], 0, 1024, clientId);

        const auto& error = response->GetError();

        UNIT_ASSERT_VALUES_EQUAL_C(E_IO, error.GetCode(), error.GetMessage());
    }

    auto MakeReadResponse(EWellKnownResultCodes code)
    {
        NProto::TReadBlocksLocalResponse response;
        *response.MutableError() = MakeError(code, "");
        return response;
    };

    Y_UNIT_TEST(ShouldCheckDevicesHealthInBackground)
    {
        using ::testing::Return;
        using ::testing::_;

        struct TMockStorage: public IStorage
        {
            MOCK_METHOD(
                NThreading::TFuture<NProto::TZeroBlocksResponse>,
                ZeroBlocks,
                (
                    TCallContextPtr,
                    std::shared_ptr<NProto::TZeroBlocksRequest>),
                (override));
            MOCK_METHOD(
                NThreading::TFuture<NProto::TReadBlocksLocalResponse>,
                ReadBlocksLocal,
                (
                    TCallContextPtr,
                    std::shared_ptr<NProto::TReadBlocksLocalRequest>),
                (override));
            MOCK_METHOD(
                NThreading::TFuture<NProto::TWriteBlocksLocalResponse>,
                WriteBlocksLocal,
                (
                    TCallContextPtr,
                    std::shared_ptr<NProto::TWriteBlocksLocalRequest>),
                (override));
            MOCK_METHOD(
                NThreading::TFuture<NProto::TError>,
                EraseDevice,
                (NProto::EDeviceEraseMethod),
                (override));
            MOCK_METHOD(TStorageBuffer, AllocateBuffer, (size_t), (override));
            MOCK_METHOD(void, ReportIOError, (), (override));
        };

        struct TMockStorageProvider: public IStorageProvider
        {
            IStoragePtr Storage;

            TMockStorageProvider()
                : Storage(std::make_shared<TMockStorage>())
            {
                ON_CALL((*GetStorage()), ReadBlocksLocal(_, _))
                    .WillByDefault(Return(MakeFuture(MakeReadResponse(S_OK))));
            }

            NThreading::TFuture<IStoragePtr> CreateStorage(
                const NProto::TVolume&,
                const TString&,
                NProto::EVolumeAccessMode) override
            {
                return MakeFuture(Storage);
            }

            TMockStorage* GetStorage()
            {
                return static_cast<TMockStorage*>(Storage.get());
            }
        };

        const auto workingDir = TryGetRamDrivePath();
        TTestBasicRuntime runtime;
        auto mockSp = new TMockStorageProvider();
        std::shared_ptr<IStorageProvider> sp(mockSp);

        auto env = TTestEnvBuilder(runtime)
            .With([&] {
                NProto::TDiskAgentConfig agentConfig;
                agentConfig.SetBackend(NProto::DISK_AGENT_BACKEND_AIO);
                agentConfig.SetAcquireRequired(true);
                agentConfig.SetEnabled(true);
                *agentConfig.AddFileDevices() =
                    PrepareFileDevice(workingDir / "dev1", "dev1");
                *agentConfig.AddFileDevices() =
                    PrepareFileDevice(workingDir / "dev2", "dev2");
                return agentConfig;
            }())
            .With(sp)
            .Build();

        UNIT_ASSERT(env.DiskRegistryState->Stats.empty());

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        diskAgent.AcquireDevices(
            TVector<TString> {"dev1", "dev2"},
            "client-1",
            NProto::VOLUME_ACCESS_READ_WRITE
        );

        auto jumpInFuture = [&](ui32 secs) {
            runtime.AdvanceCurrentTime(TDuration::Seconds(secs));
            TDispatchOptions options;
            options.FinalEvents = {
                TDispatchOptions::TFinalEventCondition(
                    TEvDiskRegistry::EvUpdateAgentStatsRequest)};
            runtime.DispatchEvents(options);
        };

        auto getStats = [&]() -> THashMap<TString, NProto::TDeviceStats> {
            jumpInFuture(15);
            UNIT_ASSERT_VALUES_EQUAL(1, env.DiskRegistryState->Stats.size());
            THashMap<TString, NProto::TDeviceStats> stats;
            auto& agentStats = env.DiskRegistryState->Stats.begin()->second;
            for (const auto& deviceStats: agentStats.GetDeviceStats()) {
                stats[deviceStats.GetDeviceUUID()] = deviceStats;
            }
            return stats;
        };

        auto read = [&] (const auto& uuid) {
            return ReadDeviceBlocks(
                runtime, diskAgent, uuid, 0, 1, "client-1"
            )->GetStatus();
        };

        {
            auto stats = getStats();
            UNIT_ASSERT_VALUES_EQUAL(0, stats["dev1"].GetErrors());
            UNIT_ASSERT_VALUES_EQUAL(0, stats["dev1"].GetErrors());
        }

        UNIT_ASSERT_VALUES_EQUAL(S_OK, read("dev1"));
        UNIT_ASSERT_VALUES_EQUAL(S_OK, read("dev2"));

        {
            auto stats = getStats();
            UNIT_ASSERT_GT(stats["dev1"].GetNumReadOps(), 0);
            UNIT_ASSERT_VALUES_EQUAL(0, stats["dev1"].GetErrors());
            UNIT_ASSERT_GT(stats["dev2"].GetNumReadOps(), 0);
            UNIT_ASSERT_VALUES_EQUAL(0, stats["dev2"].GetErrors());
        }

        ON_CALL((*mockSp->GetStorage()), ReadBlocksLocal(_, _))
            .WillByDefault(Return(MakeFuture(MakeReadResponse(E_IO))));
        jumpInFuture(15);

        {
            auto stats = getStats();
            UNIT_ASSERT_GT(stats["dev1"].GetErrors(), 0);
            UNIT_ASSERT_GT(stats["dev2"].GetErrors(), 0);
        }
    }

    Y_UNIT_TEST(ShouldFindDevices)
    {
        TTestBasicRuntime runtime;

        TTempDir tempDir;
        const TFsPath rootDir = tempDir.Path() / "dev/disk/by-partlabel";

        rootDir.MkDirs();

        auto prepareFile = [&] (TFsPath name, auto size) {
            TFile {rootDir / name, EOpenModeFlag::CreateAlways}
            .Resize(size);
        };

        // local
        prepareFile("NVMECOMPUTE01", 1024_KB);
        prepareFile("NVMECOMPUTE02", 1024_KB);
        prepareFile("NVMECOMPUTE03", 2000_KB);
        prepareFile("NVMECOMPUTE04", 2000_KB);
        prepareFile("NVMECOMPUTE05", 1025_KB);

        // default
        prepareFile("NVMENBS01", 1024_KB);
        prepareFile("NVMENBS02", 1024_KB);
        prepareFile("NVMENBS03", 2000_KB);
        prepareFile("NVMENBS04", 2000_KB);
        prepareFile("NVMENBS05", 1025_KB);
        prepareFile("NVMENBS06", 10000_KB);
        prepareFile("NVMENBS07", 10000_KB);

         // rot
        prepareFile("ROTNBS01", 2024_KB);
        prepareFile("ROTNBS02", 2024_KB);
        prepareFile("ROTNBS03", 2024_KB);
        prepareFile("ROTNBS04", 2024_KB);
        prepareFile("ROTNBS05", 2025_KB);

        auto config = DiskAgentConfig();
        auto& discovery = *config.MutableStorageDiscoveryConfig();

        {
            auto& path = *discovery.AddPathConfigs();
            path.SetPathRegExp(rootDir / "NVMECOMPUTE([0-9]{2})");

            auto& local = *path.AddPoolConfigs();
            local.SetPoolName("local");
            local.SetHashSuffix("-local");
            local.SetMinSize(1024_KB);
            local.SetMaxSize(2000_KB);
        }

        {
            auto& path = *discovery.AddPathConfigs();
            path.SetPathRegExp(rootDir / "NVMENBS([0-9]{2})");

            auto& def = *path.AddPoolConfigs();
            def.SetMinSize(1024_KB);
            def.SetMaxSize(2000_KB);

            auto& defLarge = *path.AddPoolConfigs();
            defLarge.SetMinSize(9000_KB);
            defLarge.SetMaxSize(15000_KB);
        }

        {
            auto& path = *discovery.AddPathConfigs();
            path.SetPathRegExp(rootDir / "ROTNBS([0-9]{2})");

            auto& local = *path.AddPoolConfigs();
            local.SetPoolName("rot");
            local.SetHashSuffix("-rot");
            local.SetMinSize(2024_KB);
            local.SetMaxSize(2200_KB);
        }

        auto env = TTestEnvBuilder(runtime)
            .With(config
                | WithBackend(NProto::DISK_AGENT_BACKEND_AIO))
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        TVector<NProto::TDeviceConfig> devices;
        for (auto& [_, config]: env.DiskRegistryState->Devices) {
            devices.push_back(config);
        }
        SortBy(devices, [] (const auto& d) {
            return std::tie(d.GetPoolName(), d.GetDeviceName());
        });

        UNIT_ASSERT_VALUES_EQUAL(17, devices.size());

        for (auto& d: devices) {
            UNIT_ASSERT_VALUES_UNEQUAL("", d.GetDeviceUUID());
        }

        for (int i = 1; i <= 7; ++i) {
            const auto& d = devices[i - 1];
            const auto path = rootDir / (TStringBuilder() << "NVMENBS0" << i);
            UNIT_ASSERT_VALUES_EQUAL(path, d.GetDeviceName());
            UNIT_ASSERT_VALUES_EQUAL("", d.GetPoolName());
        }

        for (int i = 1; i <= 5; ++i) {
            const auto& d = devices[7 + i - 1];
            const auto path = rootDir / (TStringBuilder() << "NVMECOMPUTE0" << i);
            UNIT_ASSERT_VALUES_EQUAL(path, d.GetDeviceName());
            UNIT_ASSERT_VALUES_EQUAL("local", d.GetPoolName());
        }

        for (int i = 1; i <= 5; ++i) {
            const auto& d = devices[12 + i - 1];
            const auto path = rootDir / (TStringBuilder() << "ROTNBS0" << i);
            UNIT_ASSERT_VALUES_EQUAL(path, d.GetDeviceName());
            UNIT_ASSERT_VALUES_EQUAL("rot", d.GetPoolName());
        }
    }

    Y_UNIT_TEST(ShouldDetectConfigMismatch)
    {
        TTestBasicRuntime runtime;

        TTempDir tempDir;
        const TFsPath rootDir = tempDir.Path() / "dev/disk/by-partlabel";

        rootDir.MkDirs();

        auto prepareFile = [&] (TFsPath name, auto size) {
            TFile {rootDir / name, EOpenModeFlag::CreateAlways}
            .Resize(size);
        };

        prepareFile("NVMECOMPUTE01", 1024_KB);    // local
        prepareFile("NVMECOMPUTE02", 1000_KB);

        auto config = DiskAgentConfig();
        auto& discovery = *config.MutableStorageDiscoveryConfig();

        {
            auto& path = *discovery.AddPathConfigs();
            path.SetPathRegExp(rootDir / "NVMECOMPUTE([0-9]{2})");

            auto& local = *path.AddPoolConfigs();
            local.SetPoolName("local");
            local.SetHashSuffix("-local");
            local.SetMinSize(1024_KB);
            local.SetMaxSize(2000_KB);
        }

        auto counters = MakeIntrusive<NMonitoring::TDynamicCounters>();
        InitCriticalEventsCounter(counters);

        auto mismatch = counters->GetCounter(
            "AppCriticalEvents/DiskAgentConfigMismatch",
            true);

        UNIT_ASSERT_VALUES_EQUAL(0, mismatch->Val());

        auto env = TTestEnvBuilder(runtime)
            .With(config
                | WithBackend(NProto::DISK_AGENT_BACKEND_AIO))
            .Build();

        TDiskAgentClient diskAgent(runtime);
        diskAgent.WaitReady();

        UNIT_ASSERT_VALUES_EQUAL(0, env.DiskRegistryState->Devices.size());
        UNIT_ASSERT_VALUES_EQUAL(1, mismatch->Val());
    }
}

}   // namespace NCloud::NBlockStore::NStorage
