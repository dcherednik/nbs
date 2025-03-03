#include "part_nonrepl_rdma.h"
#include "part_nonrepl_rdma_actor.h"
#include "ut_env.h"

#include <cloud/blockstore/libs/common/block_checksum.h>
#include <cloud/blockstore/libs/common/sglist_test.h>
#include <cloud/blockstore/libs/rdma_test/client_test.h>
#include <cloud/blockstore/libs/service_local/rdma_protocol.h>
#include <cloud/blockstore/libs/storage/api/disk_agent.h>
#include <cloud/blockstore/libs/storage/api/stats_service.h>
#include <cloud/blockstore/libs/storage/api/volume.h>
#include <cloud/blockstore/libs/storage/core/config.h>
#include <cloud/blockstore/libs/storage/protos/disk.pb.h>
#include <cloud/blockstore/libs/storage/testlib/disk_agent_mock.h>

#include <contrib/ydb/core/testlib/basics/runtime.h>
#include <contrib/ydb/core/testlib/tablet_helpers.h>

#include <library/cpp/testing/unittest/registar.h>

#include <util/datetime/base.h>
#include <util/generic/size_literals.h>

namespace NCloud::NBlockStore::NStorage {

using namespace NActors;
using namespace NKikimr;

namespace {

////////////////////////////////////////////////////////////////////////////////

struct TTestEnv
{
    TTestActorRuntime& Runtime;
    TActorId ActorId;
    TActorId VolumeActorId;
    TStorageStatsServiceStatePtr StorageStatsServiceState;
    TDiskAgentStatePtr DiskAgentState;
    NRdma::IClientPtr RdmaClient;

    static void AddDevice(
        ui32 nodeId,
        ui32 blockCount,
        TString name,
        TDevices& devices)
    {
        const auto k = DefaultBlockSize / DefaultDeviceBlockSize;

        auto& device = *devices.Add();
        device.SetNodeId(nodeId);
        device.SetBlocksCount(blockCount * k);
        device.SetDeviceUUID(name);
        device.SetBlockSize(DefaultDeviceBlockSize);
        device.SetAgentId(Sprintf("agent-%u", nodeId));
    }

    static TDevices DefaultDevices(ui64 nodeId)
    {
        TDevices devices;
        AddDevice(nodeId, 2048, "vasya", devices);
        AddDevice(nodeId, 3072, "petya", devices);
        AddDevice(0, 1024, "", devices);

        return devices;
    }

    explicit TTestEnv(TTestActorRuntime& runtime)
        : TTestEnv(runtime, NProto::VOLUME_IO_OK)
    {}

    explicit TTestEnv(
            TTestActorRuntime& runtime,
            NProto::EVolumeIOMode ioMode)
        : TTestEnv(
            runtime,
            ioMode,
            DefaultDevices(runtime.GetNodeId(0))
        )
    {}

    explicit TTestEnv(
            TTestActorRuntime& runtime,
            NProto::EVolumeIOMode ioMode,
            TDevices devices)
        : Runtime(runtime)
        , ActorId(0, "YYY")
        , VolumeActorId(0, "VVV")
        , StorageStatsServiceState(MakeIntrusive<TStorageStatsServiceState>())
        , DiskAgentState(std::make_shared<TDiskAgentState>())
        , RdmaClient(std::make_shared<TRdmaClientTest>())
    {
        SetupLogging();

        NProto::TStorageServiceConfig storageConfig;
        storageConfig.SetMaxTimedOutDeviceStateDuration(20'000);
        storageConfig.SetNonReplicatedMinRequestTimeoutSSD(1'000);
        storageConfig.SetNonReplicatedMaxRequestTimeoutSSD(5'000);
        storageConfig.SetAssignIdToWriteAndZeroRequestsEnabled(true);

        auto config = std::make_shared<TStorageConfig>(
            std::move(storageConfig),
            std::make_shared<TFeaturesConfig>(NProto::TFeaturesConfig())
        );

        auto nodeId = Runtime.GetNodeId(0);

        Runtime.AddLocalService(
            MakeDiskAgentServiceId(nodeId),
            TActorSetupCmd(
                new TDiskAgentMock(devices, DiskAgentState),
                TMailboxType::Simple,
                0
            )
        );

        auto partConfig = std::make_shared<TNonreplicatedPartitionConfig>(
            ToLogicalBlocks(devices, DefaultBlockSize),
            ioMode,
            "test",
            DefaultBlockSize,
            TNonreplicatedPartitionConfig::TVolumeInfo{
                Now(),
                // only SSD/HDD distinction matters
                NProto::STORAGE_MEDIA_SSD_NONREPLICATED},
            VolumeActorId,
            false, // muteIOErrors
            false, // markBlocksUsed
            THashSet<TString>(), // freshDeviceIds
            TDuration::Zero(), // maxTimedOutDeviceStateDuration
            false, // maxTimedOutDeviceStateDurationOverridden
            false // useSimpleMigrationBandwidthLimiter
        );

        auto part = std::make_unique<TNonreplicatedPartitionRdmaActor>(
            std::move(config),
            std::move(partConfig),
            RdmaClient,
            VolumeActorId
        );

        Runtime.AddLocalService(
            ActorId,
            TActorSetupCmd(part.release(), TMailboxType::Simple, 0)
        );

        auto dummy = std::make_unique<TDummyActor>();

        Runtime.AddLocalService(
            VolumeActorId,
            TActorSetupCmd(dummy.release(), TMailboxType::Simple, 0)
        );

        Runtime.AddLocalService(
            MakeStorageStatsServiceId(),
            TActorSetupCmd(
                new TStorageStatsServiceMock(StorageStatsServiceState),
                TMailboxType::Simple,
                0
            )
        );

        SetupTabletServices(Runtime);
    }

    void SetupLogging()
    {
        Runtime.AppendToLogSettings(
            TBlockStoreComponents::START,
            TBlockStoreComponents::END,
            GetComponentName);

        // for (ui32 i = TBlockStoreComponents::START; i < TBlockStoreComponents::END; ++i) {
        //    Runtime.SetLogPriority(i, NLog::PRI_DEBUG);
        // }
        // Runtime.SetLogPriority(NLog::InvalidComponent, NLog::PRI_DEBUG);
    }

    void KillDiskAgent()
    {
        auto sender = Runtime.AllocateEdgeActor();
        auto nodeId = Runtime.GetNodeId(0);

        auto request = std::make_unique<TEvents::TEvPoisonPill>();

        Runtime.Send(new IEventHandle(
            MakeDiskAgentServiceId(nodeId),
            sender,
            request.release()));

        Runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));
    }

    TRdmaClientTest& Rdma()
    {
        return static_cast<TRdmaClientTest&>(*RdmaClient);
    }
};

}   // namespace

////////////////////////////////////////////////////////////////////////////////

Y_UNIT_TEST_SUITE(TNonreplicatedPartitionRdmaTest)
{
    Y_UNIT_TEST(ShouldLocalReadWrite)
    {
        TTestBasicRuntime runtime;

        TTestEnv env(runtime);
        TPartitionClient client(runtime, env.ActorId);

        const auto blockRange1 = TBlockRange64::WithLength(1024, 3072);
        client.SendWriteBlocksLocalRequest(
            blockRange1,
            TString(DefaultBlockSize, 'A'));
        {
            auto response = client.RecvWriteBlocksLocalResponse();
            UNIT_ASSERT_VALUES_EQUAL_C(
                E_REJECTED,
                response->GetStatus(),
                response->GetErrorReason());
        }

        env.Rdma().InitAllEndpoints();

        client.WriteBlocksLocal(blockRange1, TString(DefaultBlockSize, 'A'));

        {
            TVector<TString> blocks;

            client.ReadBlocksLocal(
                blockRange1,
                TGuardedSgList(ResizeBlocks(
                    blocks,
                    blockRange1.Size(),
                    TString(DefaultBlockSize, '\0')
                )));

            for (ui32 i = 0; i < blocks.size(); ++i) {
                UNIT_ASSERT_VALUES_EQUAL_C(
                    TString(4096, 'A'),
                    blocks[i],
                    TStringBuilder() << "block " << i);
            }
        }

        const auto blockRange2 = TBlockRange64::MakeClosedInterval(5000, 5199);
        client.WriteBlocksLocal(blockRange2, TString(DefaultBlockSize, 'B'));

        const auto blockRange3 = TBlockRange64::MakeClosedInterval(5000, 5150);

        {
            TVector<TString> blocks;

            client.ReadBlocksLocal(
                blockRange3,
                TGuardedSgList(ResizeBlocks(
                    blocks,
                    blockRange3.Size(),
                    TString(DefaultBlockSize, '\0')
                )));

            for (ui32 i = 0; i < 120; ++i) {
                UNIT_ASSERT_VALUES_EQUAL_C(
                    TString(4096, 'B'),
                    blocks[i],
                    TStringBuilder() << "block " << i);
            }

            for (ui32 i = 120; i < blockRange3.Size(); ++i) {
                UNIT_ASSERT_VALUES_EQUAL_C(
                    TString(4096, 0),
                    blocks[i],
                    TStringBuilder() << "block " << i);
            }
        }

        client.ZeroBlocks(blockRange3);

        {
            TVector<TString> blocks;

            client.ReadBlocksLocal(
                blockRange3,
                TGuardedSgList(ResizeBlocks(
                    blocks,
                    blockRange3.Size(),
                    TString(DefaultBlockSize, '\0')
                )));

            for (ui32 i = 0; i < blockRange3.Size(); ++i) {
                UNIT_ASSERT_VALUES_EQUAL_C(
                    TString(4096, 0),
                    blocks[i],
                    TStringBuilder() << "block " << i);
            }
        }

        client.SendRequest(
            env.ActorId,
            std::make_unique<TEvNonreplPartitionPrivate::TEvUpdateCounters>()
        );

        runtime.DispatchEvents({}, TDuration::Seconds(1));

        auto& counters = env.StorageStatsServiceState->Counters.RequestCounters;
        UNIT_ASSERT_VALUES_EQUAL(3, counters.ReadBlocks.Count);
        UNIT_ASSERT_VALUES_EQUAL(
            DefaultBlockSize * (
                blockRange1.Size() + 2 * blockRange3.Size()
            ),
            counters.ReadBlocks.RequestBytes
        );
        UNIT_ASSERT_VALUES_EQUAL(2, counters.WriteBlocks.Count);
        UNIT_ASSERT_VALUES_EQUAL(
            DefaultBlockSize * (
                blockRange1.Size() + blockRange2.Size()
            ),
            counters.WriteBlocks.RequestBytes
        );

        UNIT_ASSERT_VALUES_EQUAL(
            0,
            env.StorageStatsServiceState->Counters.Simple.IORequestsInFlight.Value
        );
    }

    Y_UNIT_TEST(ShouldRemoteReadWrite)
    {
        TTestBasicRuntime runtime;

        TTestEnv env(runtime);
        env.Rdma().InitAllEndpoints();

        TPartitionClient client(runtime, env.ActorId);

        const auto blockRange1 = TBlockRange64::WithLength(1024, 3072);
        client.WriteBlocks(blockRange1, 'A');

        {
            auto response = client.ReadBlocks(blockRange1);
            const auto& blocks = response->Record.GetBlocks().GetBuffers();

            for (int i = 0; i < blocks.size(); ++i) {
                UNIT_ASSERT_VALUES_EQUAL_C(
                    TString(4096, 'A'),
                    blocks[i],
                    TStringBuilder() << "block " << i);
            }
        }

        const auto blockRange2 = TBlockRange64::MakeClosedInterval(5000, 5199);
        client.WriteBlocks(blockRange2, 'B');

        const auto blockRange3 = TBlockRange64::MakeClosedInterval(5000, 5150);

        {
            auto response = client.ReadBlocks(blockRange3);
            const auto& blocks = response->Record.GetBlocks().GetBuffers();

            for (ui32 i = 0; i < 120; ++i) {
                UNIT_ASSERT_VALUES_EQUAL_C(
                    TString(4096, 'B'),
                    blocks[i],
                    TStringBuilder() << "block " << i);
            }

            for (ui32 i = 120; i < blockRange3.Size(); ++i) {
                UNIT_ASSERT_VALUES_EQUAL_C(
                    TString(4096, 0),
                    blocks[i],
                    TStringBuilder() << "block " << i);
            }
        }

        client.SendRequest(
            env.ActorId,
            std::make_unique<TEvNonreplPartitionPrivate::TEvUpdateCounters>()
        );

        runtime.DispatchEvents({}, TDuration::Seconds(1));

        auto& counters = env.StorageStatsServiceState->Counters.RequestCounters;
        UNIT_ASSERT_VALUES_EQUAL(2, counters.ReadBlocks.Count);
        UNIT_ASSERT_VALUES_EQUAL(
            DefaultBlockSize * (
                blockRange1.Size() + blockRange3.Size()
            ),
            counters.ReadBlocks.RequestBytes
        );
        UNIT_ASSERT_VALUES_EQUAL(2, counters.WriteBlocks.Count);
        UNIT_ASSERT_VALUES_EQUAL(
            DefaultBlockSize * (
                blockRange1.Size() + blockRange2.Size()
            ),
            counters.WriteBlocks.RequestBytes
        );

        UNIT_ASSERT_VALUES_EQUAL(
            0,
            env.StorageStatsServiceState->Counters.Simple.IORequestsInFlight.Value
        );
    }

#define WRITE_BLOCKS_E(error) {                                                \
            TString data(DefaultBlockSize, 'A');                               \
            client.SendWriteBlocksLocalRequest(blockRange1, data);             \
            {                                                                  \
                auto response = client.RecvWriteBlocksLocalResponse();         \
                UNIT_ASSERT_VALUES_EQUAL_C(                                    \
                    error.GetCode(),                                           \
                    response->GetStatus(),                                     \
                    response->GetErrorReason());                               \
            }                                                                  \
        }                                                                      \
// WRITE_BLOCKS_E

#define ZERO_BLOCKS_E(error) {                                                 \
            client.SendZeroBlocksRequest(blockRange1);                         \
            {                                                                  \
                auto response = client.RecvZeroBlocksResponse();               \
                UNIT_ASSERT_VALUES_EQUAL_C(                                    \
                    error.GetCode(),                                           \
                    response->GetStatus(),                                     \
                    response->GetErrorReason());                               \
            }                                                                  \
        }                                                                      \
// ZERO_BLOCKS_E

#define READ_BLOCKS_E(error, c) {                                              \
            TVector<TString> blocks;                                           \
                                                                               \
            client.SendReadBlocksLocalRequest(                                 \
                blockRange1,                                                   \
                TGuardedSgList(ResizeBlocks(                                   \
                    blocks,                                                    \
                    blockRange1.Size(),                                        \
                    TString(DefaultBlockSize, '\0')                            \
                )));                                                           \
                                                                               \
            auto response = client.RecvReadBlocksLocalResponse();              \
            UNIT_ASSERT_VALUES_EQUAL_C(                                        \
                error.GetCode(),                                               \
                response->GetStatus(),                                         \
                response->GetErrorReason());                                   \
                                                                               \
            if (!HasError(error)) {                                            \
                for (ui32 i = 0; i < blocks.size(); ++i) {                     \
                    UNIT_ASSERT_VALUES_EQUAL_C(                                \
                        TString(4096, c),                                      \
                        blocks[i],                                             \
                        TStringBuilder() << "block " << i);                    \
                }                                                              \
            }                                                                  \
        }                                                                      \
// READ_BLOCKS_E

    Y_UNIT_TEST(ShouldLocalReadWriteWithErrors)
    {
        TTestBasicRuntime runtime;

        TTestEnv env(runtime);
        TPartitionClient client(runtime, env.ActorId);

        env.Rdma().InitAllEndpoints();

        const auto allocationError = MakeError(E_FAIL, "allocation error");
        const auto rdmaResponseError =
            MakeError(E_REJECTED, "rdma response error");
        const auto responseError = MakeError(E_IO, "response error");

        const auto blockRange1 = TBlockRange64::WithLength(1024, 3072);

        env.Rdma().InjectErrors(allocationError, {}, {});

        WRITE_BLOCKS_E(allocationError);
        ZERO_BLOCKS_E(allocationError);
        READ_BLOCKS_E(allocationError, 0);

        env.Rdma().InjectErrors({}, rdmaResponseError, {});

        WRITE_BLOCKS_E(rdmaResponseError);
        ZERO_BLOCKS_E(rdmaResponseError);
        READ_BLOCKS_E(rdmaResponseError, 0);

        env.Rdma().InjectErrors({}, {}, responseError);

        WRITE_BLOCKS_E(responseError);
        ZERO_BLOCKS_E(responseError);
        READ_BLOCKS_E(responseError, 0);

        env.Rdma().InjectErrors({}, {}, {});

        READ_BLOCKS_E(NProto::TError{}, 0);
        WRITE_BLOCKS_E(NProto::TError{});
        READ_BLOCKS_E(NProto::TError{}, 'A');
        ZERO_BLOCKS_E(NProto::TError{});
        READ_BLOCKS_E(NProto::TError{}, 0);
    }

    Y_UNIT_TEST(ShouldHandleEndpointInitializationFailure)
    {
        TTestBasicRuntime runtime;

        TTestEnv env(runtime);
        TPartitionClient client(runtime, env.ActorId);

        TActorId notifiedActor;
        ui32 notificationCount = 0;

        runtime.SetObserverFunc([&] (TAutoPtr<IEventHandle>& event) {
                switch (event->GetTypeRewrite()) {
                    case TEvVolume::EvRdmaUnavailable: {
                        notifiedActor = event->Recipient;
                        ++notificationCount;

                        break;
                    }
                }

                return TTestActorRuntime::DefaultObserverFunc(event);
            }
        );

        env.Rdma().InitAllEndpointsWithError();

        const auto error = MakeError(E_REJECTED, "");

        const auto blockRange1 = TBlockRange64::WithLength(1024, 3072);

        WRITE_BLOCKS_E(error);
        ZERO_BLOCKS_E(error);
        READ_BLOCKS_E(error, 0);

        UNIT_ASSERT_VALUES_EQUAL(env.VolumeActorId, notifiedActor);
        UNIT_ASSERT_VALUES_EQUAL(1, notificationCount);
    }

    Y_UNIT_TEST(ShouldUpdateStats)
    {
        TTestBasicRuntime runtime;

        runtime.SetRegistrationObserverFunc(
            [] (auto& runtime, const auto& parentId, const auto& actorId)
        {
            Y_UNUSED(parentId);
            runtime.EnableScheduleForActor(actorId);
        });

        TTestEnv env(runtime);

        auto& counters = env.StorageStatsServiceState->Counters.Cumulative;

        TPartitionClient client(runtime, env.ActorId);

        env.Rdma().InitAllEndpoints();

        client.WriteBlocks(TBlockRange64::WithLength(0, 1024), 1);
        client.ReadBlocks(TBlockRange64::WithLength(0, 512));
        client.ZeroBlocks(TBlockRange64::WithLength(0, 1024));

        client.SendRequest(
            env.ActorId,
            std::make_unique<TEvNonreplPartitionPrivate::TEvUpdateCounters>()
        );

        runtime.DispatchEvents({}, TDuration::Seconds(1));

        UNIT_ASSERT_VALUES_EQUAL(2048 * 4096, counters.BytesWritten.Value);
        UNIT_ASSERT_VALUES_EQUAL(512 * 4096, counters.BytesRead.Value);
    }

    Y_UNIT_TEST(ShouldHandleInvalidSessionError)
    {
        TTestBasicRuntime runtime;

        TTestEnv env(runtime);

        env.Rdma().InitAllEndpoints();

        const auto blockRange1 = TBlockRange64::WithLength(1024, 3072);
        const auto invalidSession =
            MakeError(E_BS_INVALID_SESSION, "invalid session");
        const auto replacementError =
            MakeError(E_REJECTED, "invalid session");

        env.Rdma().InjectErrors({}, {}, invalidSession);

        TPartitionClient client(runtime, env.ActorId);

        TActorId reacquireDiskRecipient;
        runtime.SetObserverFunc([&] (TAutoPtr<IEventHandle>& event) {
                switch (event->GetTypeRewrite()) {
                    case TEvVolume::EvReacquireDisk: {
                        reacquireDiskRecipient = event->Recipient;

                        return TTestActorRuntime::EEventAction::DROP;
                    }
                }

                return TTestActorRuntime::DefaultObserverFunc(event);
            }
        );

        {
            client.SendReadBlocksRequest(TBlockRange64::WithLength(0, 1024));
            auto response = client.RecvReadBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_REJECTED, response->GetStatus());
        }

        UNIT_ASSERT_VALUES_EQUAL(env.VolumeActorId, reacquireDiskRecipient);

        reacquireDiskRecipient = {};

        {
            client.SendWriteBlocksRequest(
                TBlockRange64::WithLength(0, 1024),
                1);
            auto response = client.RecvWriteBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_REJECTED, response->GetStatus());
        }

        UNIT_ASSERT_VALUES_EQUAL(env.VolumeActorId, reacquireDiskRecipient);

        reacquireDiskRecipient = {};
        WRITE_BLOCKS_E(replacementError);
        UNIT_ASSERT_VALUES_EQUAL(env.VolumeActorId, reacquireDiskRecipient);

        reacquireDiskRecipient = {};
        ZERO_BLOCKS_E(replacementError);
        UNIT_ASSERT_VALUES_EQUAL(env.VolumeActorId, reacquireDiskRecipient);

        reacquireDiskRecipient = {};
        READ_BLOCKS_E(replacementError, 0);
        UNIT_ASSERT_VALUES_EQUAL(env.VolumeActorId, reacquireDiskRecipient);
    }

#undef WRITE_BLOCKS_E
#undef ZERO_BLOCKS_E
#undef READ_BLOCKS_E

    Y_UNIT_TEST(ShouldChecksumBlocks)
    {
        TTestBasicRuntime runtime;

        TTestEnv env(runtime);

        env.Rdma().InitAllEndpoints();

        TPartitionClient client(runtime, env.ActorId);

        const auto blockRange = TBlockRange64::WithLength(1024, 3072);
        client.WriteBlocksLocal(blockRange, TString(DefaultBlockSize, 'A'));

        TString data(blockRange.Size() * DefaultBlockSize, 'A');
        TBlockChecksum checksum;
        checksum.Extend(data.Data(), data.Size());

        {
            auto response = client.ChecksumBlocks(blockRange);
            UNIT_ASSERT_VALUES_EQUAL(
                checksum.GetValue(),
                response->Record.GetChecksum());
        }

        client.SendRequest(
            env.ActorId,
            std::make_unique<TEvNonreplPartitionPrivate::TEvUpdateCounters>()
        );

        runtime.DispatchEvents({}, TDuration::Seconds(1));

        auto& counters = env.StorageStatsServiceState->Counters.RequestCounters;
        UNIT_ASSERT_VALUES_EQUAL(1, counters.ChecksumBlocks.Count);
        UNIT_ASSERT_VALUES_EQUAL(
            DefaultBlockSize * blockRange.Size(),
            counters.ChecksumBlocks.RequestBytes
        );
    }

    Y_UNIT_TEST(ShouldFillRequestIdInDeviceBlocksRequest)
    {
        TTestBasicRuntime runtime;

        TTestEnv env(runtime);

        env.Rdma().InitAllEndpoints();

        TPartitionClient client(runtime, env.ActorId);

        ui64 writeRequestId = 0;
        ui64 zeroRequestId = 0;
        auto observer =
            [&](NRdma::TProtoMessageSerializer::TParseResult& parseResut)
        {
            if (parseResut.MsgId ==
                TBlockStoreProtocol::WriteDeviceBlocksRequest)
            {
                auto* request = static_cast<NProto::TWriteDeviceBlocksRequest*>(
                    parseResut.Proto.get());
                writeRequestId = request->GetVolumeRequestId();
            }
            if (parseResut.MsgId ==
                TBlockStoreProtocol::ZeroDeviceBlocksRequest)
            {
                auto* request = static_cast<NProto::TZeroDeviceBlocksRequest*>(
                    parseResut.Proto.get());
                zeroRequestId = request->GetVolumeRequestId();
            }
        };
        env.Rdma().SetMessageObserver(std::move(observer));

        const auto blockRange = TBlockRange64::WithLength(1024, 3072);

        {
            TString data(DefaultBlockSize, 'A');
            client.SendRequest(
                client.GetActorId(),
                client.CreateWriteBlocksLocalRequest(blockRange, data),
                10);
            runtime.DispatchEvents({}, TDuration::Seconds(1));
            auto response =
                client.RecvResponse<TEvService::TEvWriteBlocksLocalResponse>();
            UNIT_ASSERT_C(
                SUCCEEDED(response->GetStatus()),
                response->GetErrorReason());
            UNIT_ASSERT_VALUES_EQUAL(10, writeRequestId);
        }

        {
            client.SendRequest(
                client.GetActorId(),
                client.CreateWriteBlocksRequest(blockRange, 'A'),
                20);
            runtime.DispatchEvents({}, TDuration::Seconds(1));
            auto response =
                client.RecvResponse<TEvService::TEvWriteBlocksResponse>();
            UNIT_ASSERT_C(
                SUCCEEDED(response->GetStatus()),
                response->GetErrorReason());
            UNIT_ASSERT_VALUES_EQUAL(20, writeRequestId);
        }

        {
            client.SendRequest(
                client.GetActorId(),
                client.CreateZeroBlocksRequest(blockRange),
                30);
            runtime.DispatchEvents({}, TDuration::Seconds(1));
            auto response =
                client.RecvResponse<TEvService::TEvZeroBlocksResponse>();
            UNIT_ASSERT_C(
                SUCCEEDED(response->GetStatus()),
                response->GetErrorReason());
            UNIT_ASSERT_VALUES_EQUAL(30, zeroRequestId);
        }
    }
}

}   // namespace NCloud::NBlockStore::NStorage
