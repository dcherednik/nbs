#include "part_nonrepl.h"
#include "part_nonrepl_actor.h"
#include "ut_env.h"

#include <cloud/blockstore/libs/common/block_checksum.h>
#include <cloud/blockstore/libs/common/sglist_test.h>
#include <cloud/blockstore/libs/storage/api/disk_agent.h>
#include <cloud/blockstore/libs/storage/api/volume.h>
#include <cloud/blockstore/libs/storage/core/config.h>
#include <cloud/blockstore/libs/storage/protos/disk.pb.h>
#include <cloud/blockstore/libs/storage/testlib/disk_agent_mock.h>

#include <cloud/blockstore/libs/storage/api/stats_service.h>

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
            false,
            DefaultDevices(runtime.GetNodeId(0)),
            NProto::STORAGE_MEDIA_SSD_NONREPLICATED)
    {}

    explicit TTestEnv(
            TTestActorRuntime& runtime,
            NProto::EVolumeIOMode ioMode,
            TDevices devices)
        : TTestEnv(
            runtime,
            ioMode,
            false,
            std::move(devices),
            NProto::STORAGE_MEDIA_SSD_NONREPLICATED)
    {}

    explicit TTestEnv(
            TTestActorRuntime& runtime,
            NProto::EVolumeIOMode ioMode,
            bool markBlocksUsed)
        : TTestEnv(
            runtime,
            ioMode,
            markBlocksUsed,
            DefaultDevices(runtime.GetNodeId(0)),
            NProto::STORAGE_MEDIA_SSD_NONREPLICATED)
    {}

    explicit TTestEnv(
            TTestActorRuntime& runtime,
            NProto::EVolumeIOMode ioMode,
            bool markBlocksUsed,
            TDevices devices)
        : TTestEnv(
            runtime,
            ioMode,
            markBlocksUsed,
            std::move(devices),
            NProto::STORAGE_MEDIA_SSD_NONREPLICATED)
    {}

    explicit TTestEnv(
            TTestActorRuntime& runtime,
            NProto::EVolumeIOMode ioMode,
            bool markBlocksUsed,
            TDevices devices,
            NProto::EStorageMediaKind mediaKind)
        : Runtime(runtime)
        , ActorId(0, "YYY")
        , VolumeActorId(0, "VVV")
        , StorageStatsServiceState(MakeIntrusive<TStorageStatsServiceState>())
        , DiskAgentState(std::make_shared<TDiskAgentState>())
    {
        SetupLogging();

        NProto::TStorageServiceConfig storageConfig;
        storageConfig.SetMaxTimedOutDeviceStateDuration(20'000);
        if (mediaKind == NProto::STORAGE_MEDIA_HDD_NONREPLICATED) {
            storageConfig.SetNonReplicatedMinRequestTimeoutSSD(60'000);
            storageConfig.SetNonReplicatedMaxRequestTimeoutSSD(60'000);
            storageConfig.SetNonReplicatedMinRequestTimeoutHDD(1'000);
            storageConfig.SetNonReplicatedMaxRequestTimeoutHDD(5'000);
        } else {
            storageConfig.SetNonReplicatedMinRequestTimeoutSSD(1'000);
            storageConfig.SetNonReplicatedMaxRequestTimeoutSSD(5'000);
            storageConfig.SetNonReplicatedMinRequestTimeoutHDD(60'000);
            storageConfig.SetNonReplicatedMaxRequestTimeoutHDD(60'000);
        }
        storageConfig.SetExpectedClientBackoffIncrement(500);
        storageConfig.SetNonReplicatedAgentMaxTimeout(300'000);

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
            TNonreplicatedPartitionConfig::TVolumeInfo{Now(), mediaKind},
            VolumeActorId,
            false, // muteIOErrors
            markBlocksUsed,
            THashSet<TString>(), // freshDeviceIds
            TDuration::Zero(), // maxTimedOutDeviceStateDuration
            false, // maxTimedOutDeviceStateDurationOverridden
            false // useSimpleMigrationBandwidthLimiter
        );

        auto part = std::make_unique<TNonreplicatedPartitionActor>(
            std::move(config),
            std::move(partConfig),
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
};

}   // namespace

////////////////////////////////////////////////////////////////////////////////

Y_UNIT_TEST_SUITE(TNonreplicatedPartitionTest)
{
    Y_UNIT_TEST(ShouldReadWriteZero)
    {
        TTestBasicRuntime runtime;

        TTestEnv env(runtime);

        TPartitionClient client(runtime, env.ActorId);

        {
            auto response = client.ReadBlocks(
                TBlockRange64::WithLength(1024, 3072));
            const auto& blocks = response->Record.GetBlocks();

            UNIT_ASSERT_VALUES_EQUAL(3072, blocks.BuffersSize());
            UNIT_ASSERT_VALUES_EQUAL(DefaultBlockSize, blocks.GetBuffers(0).size());
            UNIT_ASSERT_VALUES_EQUAL(
                TString(DefaultBlockSize, 0),
                blocks.GetBuffers(0)
            );

            UNIT_ASSERT_VALUES_EQUAL(DefaultBlockSize, blocks.GetBuffers(3071).size());
            UNIT_ASSERT_VALUES_EQUAL(
                TString(DefaultBlockSize, 0),
                blocks.GetBuffers(3071)
            );
        }

        client.WriteBlocks(TBlockRange64::WithLength(1024, 3072), 1);
        client.WriteBlocks(TBlockRange64::MakeClosedInterval(1024, 4023), 2);

        {
            auto response =
                client.ReadBlocks(TBlockRange64::WithLength(1024, 3072));
            const auto& blocks = response->Record.GetBlocks();
            UNIT_ASSERT_VALUES_EQUAL(3072, blocks.BuffersSize());
            UNIT_ASSERT_VALUES_EQUAL(DefaultBlockSize, blocks.GetBuffers(0).size());
            UNIT_ASSERT_VALUES_EQUAL(DefaultBlockSize, blocks.GetBuffers(2999).size());
            UNIT_ASSERT_VALUES_EQUAL(DefaultBlockSize, blocks.GetBuffers(3000).size());
            UNIT_ASSERT_VALUES_EQUAL(DefaultBlockSize, blocks.GetBuffers(3071).size());

            UNIT_ASSERT_VALUES_EQUAL(
                TString(DefaultBlockSize, 2),
                blocks.GetBuffers(0)
            );
            UNIT_ASSERT_VALUES_EQUAL(
                TString(DefaultBlockSize, 2),
                blocks.GetBuffers(2999)
            );
            UNIT_ASSERT_VALUES_EQUAL(
                TString(DefaultBlockSize, 1),
                blocks.GetBuffers(3000)
            );
            UNIT_ASSERT_VALUES_EQUAL(
                TString(DefaultBlockSize, 1),
                blocks.GetBuffers(3071)
            );
        }

        client.ZeroBlocks(TBlockRange64::MakeClosedInterval(2024, 3023));

        {
            auto response =
                client.ReadBlocks(TBlockRange64::WithLength(1024, 3072));
            const auto& blocks = response->Record.GetBlocks();
            UNIT_ASSERT_VALUES_EQUAL(3072, blocks.BuffersSize());
            UNIT_ASSERT_VALUES_EQUAL(
                TString(DefaultBlockSize, 2),
                blocks.GetBuffers(0)
            );
            UNIT_ASSERT_VALUES_EQUAL(
                TString(DefaultBlockSize, 2),
                blocks.GetBuffers(999)
            );
            UNIT_ASSERT_VALUES_EQUAL(
                TString(DefaultBlockSize, 0),
                blocks.GetBuffers(1000)
            );
            UNIT_ASSERT_VALUES_EQUAL(
                TString(DefaultBlockSize, 0),
                blocks.GetBuffers(1999)
            );
            UNIT_ASSERT_VALUES_EQUAL(
                TString(DefaultBlockSize, 2),
                blocks.GetBuffers(2000)
            );
            UNIT_ASSERT_VALUES_EQUAL(
                TString(DefaultBlockSize, 2),
                blocks.GetBuffers(2999)
            );
            UNIT_ASSERT_VALUES_EQUAL(
                TString(DefaultBlockSize, 1),
                blocks.GetBuffers(3000)
            );
            UNIT_ASSERT_VALUES_EQUAL(
                TString(DefaultBlockSize, 1),
                blocks.GetBuffers(3071)
            );
        }

        client.WriteBlocks(TBlockRange64::WithLength(5000, 200), 3);
        client.ZeroBlocks(TBlockRange64::MakeClosedInterval(5050, 5150));

        {
            auto response = client.ReadBlocks(
                TBlockRange64::WithLength(5000, 200));
            const auto& blocks = response->Record.GetBlocks();
            UNIT_ASSERT_VALUES_EQUAL(200, blocks.BuffersSize());
            for (ui32 i = 0; i < 50; ++i) {
                UNIT_ASSERT_VALUES_EQUAL(
                    TString(DefaultBlockSize, 3),
                    blocks.GetBuffers(i)
                );
            }

            for (ui32 i = 51; i < 120; ++i) {
                UNIT_ASSERT_VALUES_EQUAL(
                    TString(DefaultBlockSize, 0),
                    blocks.GetBuffers(i)
                );
            }

            for (ui32 i = 120; i < 200; ++i) {
                UNIT_ASSERT_VALUES_EQUAL(
                    TString(),
                    blocks.GetBuffers(i)
                );
            }
        }

        client.SendRequest(
            env.ActorId,
            std::make_unique<TEvNonreplPartitionPrivate::TEvUpdateCounters>()
        );

        runtime.DispatchEvents({}, TDuration::Seconds(1));

        auto& counters = env.StorageStatsServiceState->Counters.RequestCounters;
        UNIT_ASSERT_VALUES_EQUAL(4, counters.ReadBlocks.Count);
        UNIT_ASSERT_VALUES_EQUAL(
            DefaultBlockSize * 9336,
            counters.ReadBlocks.RequestBytes
        );
        UNIT_ASSERT_VALUES_EQUAL(3, counters.WriteBlocks.Count);
        UNIT_ASSERT_VALUES_EQUAL(
            DefaultBlockSize * 6192,
            counters.WriteBlocks.RequestBytes
        );
        UNIT_ASSERT_VALUES_EQUAL(2, counters.ZeroBlocks.Count);
        UNIT_ASSERT_VALUES_EQUAL(
            DefaultBlockSize * 1070,
            counters.ZeroBlocks.RequestBytes
        );
    }

    Y_UNIT_TEST(ShouldLocalReadWrite)
    {
        TTestBasicRuntime runtime;

        TTestEnv env(runtime);

        TPartitionClient client(runtime, env.ActorId);

        const auto diskRange = TBlockRange64::WithLength(0, 5120);

        const auto blockRange1 = TBlockRange64::WithLength(1024, 3072);
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

            for (const auto& block: blocks) {
                for (auto c: block) {
                    UNIT_ASSERT_VALUES_EQUAL('A', c);
                }
            }
        }

        const auto blockRange2 = TBlockRange64::WithLength(5000, 200);
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
                const auto& block = blocks[i];
                for (auto c: block) {
                    UNIT_ASSERT_VALUES_EQUAL('B', c);
                }
            }

            for (ui32 i = 120; i < blockRange3.Size(); ++i) {
                const auto& block = blocks[i];
                for (auto c: block) {
                    UNIT_ASSERT_VALUES_EQUAL(0, c);
                }
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
                blockRange1.Size() + blockRange3.Intersect(diskRange).Size()
            ),
            counters.ReadBlocks.RequestBytes
        );
        UNIT_ASSERT_VALUES_EQUAL(2, counters.WriteBlocks.Count);
        UNIT_ASSERT_VALUES_EQUAL(
            DefaultBlockSize * (
                blockRange1.Size() + blockRange2.Intersect(diskRange).Size()
            ),
            counters.WriteBlocks.RequestBytes
        );
    }

    Y_UNIT_TEST(ShouldWriteLargeBuffer)
    {
        TTestBasicRuntime runtime;

        TTestEnv env(runtime);

        TPartitionClient client(runtime, env.ActorId);

        client.WriteBlocks(TBlockRange64::WithLength(1024, 3072), 1, 2048);

        {
            auto response =
                client.ReadBlocks(TBlockRange64::WithLength(0, 5120));
            const auto& blocks = response->Record.GetBlocks();
            UNIT_ASSERT_VALUES_EQUAL(5120, blocks.BuffersSize());

            UNIT_ASSERT_VALUES_EQUAL(
                TString(DefaultBlockSize, 0),
                blocks.GetBuffers(0)
            );
            UNIT_ASSERT_VALUES_EQUAL(
                TString(DefaultBlockSize, 0),
                blocks.GetBuffers(1023)
            );
            UNIT_ASSERT_VALUES_EQUAL(
                TString(DefaultBlockSize, 1),
                blocks.GetBuffers(1024)
            );
            UNIT_ASSERT_VALUES_EQUAL(
                TString(DefaultBlockSize, 1),
                blocks.GetBuffers(4095)
            );
            UNIT_ASSERT_VALUES_EQUAL(
                TString(DefaultBlockSize, 0),
                blocks.GetBuffers(4096)
            );
            UNIT_ASSERT_VALUES_EQUAL(
                TString(DefaultBlockSize, 0),
                blocks.GetBuffers(5119)
            );
        }

        client.SendRequest(
            env.ActorId,
            std::make_unique<TEvNonreplPartitionPrivate::TEvUpdateCounters>()
        );

        runtime.DispatchEvents({}, TDuration::Seconds(1));

        auto& counters = env.StorageStatsServiceState->Counters.RequestCounters;
        UNIT_ASSERT_VALUES_EQUAL(1, counters.WriteBlocks.Count);
        UNIT_ASSERT_VALUES_EQUAL(
            DefaultBlockSize * 3072,
            counters.WriteBlocks.RequestBytes);
    }

    Y_UNIT_TEST(ShouldReadWriteZeroWithMonsterDisk)
    {
        TTestBasicRuntime runtime;

        TDevices devices;
        const ui64 blocksPerDevice = 93_GB / DefaultBlockSize;
        for (ui32 i = 0; i < 1024; ++i) {
            TTestEnv::AddDevice(
                runtime.GetNodeId(0),
                blocksPerDevice,
                Sprintf("vasya%u", i),
                devices
            );
        }

        TTestEnv env(runtime, NProto::VOLUME_IO_OK, std::move(devices));

        TPartitionClient client(runtime, env.ActorId);

        auto range1 = TBlockRange64::WithLength(0, 1024);
        auto range2 = TBlockRange64::WithLength(blocksPerDevice * 511, 1024);
        auto range3 = TBlockRange64::WithLength(blocksPerDevice * 1023, 1024);

#define TEST_READ(range, data) {                                               \
            auto response = client.ReadBlocks(range);                          \
            const auto& blocks = response->Record.GetBlocks();                 \
                                                                               \
            UNIT_ASSERT_VALUES_EQUAL(1024, blocks.BuffersSize());              \
            UNIT_ASSERT_VALUES_EQUAL(                                          \
                DefaultBlockSize,                                              \
                blocks.GetBuffers(0).size()                                    \
            );                                                                 \
            UNIT_ASSERT_VALUES_EQUAL(                                          \
                data,                                                          \
                blocks.GetBuffers(0)                                           \
            );                                                                 \
                                                                               \
            UNIT_ASSERT_VALUES_EQUAL(                                          \
                DefaultBlockSize,                                              \
                blocks.GetBuffers(1023).size()                                 \
            );                                                                 \
            UNIT_ASSERT_VALUES_EQUAL(                                          \
                data,                                                          \
                blocks.GetBuffers(1023)                                        \
            );                                                                 \
        }

        TEST_READ(range1, TString(DefaultBlockSize, 0));
        TEST_READ(range2, TString(DefaultBlockSize, 0));
        TEST_READ(range3, TString(DefaultBlockSize, 0));

        client.WriteBlocks(range1, 1);
        client.WriteBlocks(range2, 2);
        client.WriteBlocks(range3, 3);

        TEST_READ(range1, TString(DefaultBlockSize, 1));
        TEST_READ(range2, TString(DefaultBlockSize, 2));
        TEST_READ(range3, TString(DefaultBlockSize, 3));

        client.ZeroBlocks(range2);

        TEST_READ(range1, TString(DefaultBlockSize, 1));
        TEST_READ(range2, TString(DefaultBlockSize, 0));
        TEST_READ(range3, TString(DefaultBlockSize, 3));
    }

    Y_UNIT_TEST(ShouldChecksumBlocks)
    {
        TTestBasicRuntime runtime;

        TTestEnv env(runtime);

        TPartitionClient client(runtime, env.ActorId);

        const auto blockRange = TBlockRange64::WithLength(1024, 3072);
        client.WriteBlocksLocal(blockRange, TString(DefaultBlockSize, 'A'));

        TString data(blockRange.Size() * DefaultBlockSize, 'A');
        TBlockChecksum checksum;
        checksum.Extend(data.Data(), data.Size());

        {
            auto response = client.ChecksumBlocks(blockRange);
            UNIT_ASSERT_VALUES_EQUAL(checksum.GetValue(), response->Record.GetChecksum());
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

    Y_UNIT_TEST(ShouldHandleUndeliveredIO)
    {
        TTestBasicRuntime runtime;

        runtime.SetRegistrationObserverFunc(
            [] (auto& runtime, const auto& parentId, const auto& actorId)
        {
            Y_UNUSED(parentId);
            runtime.EnableScheduleForActor(actorId);
        });

        TTestEnv env(runtime);

        TPartitionClient client(runtime, env.ActorId);

        env.KillDiskAgent();

        {
            client.SendReadBlocksRequest(
                TBlockRange64::WithLength(1024, 3072));
            runtime.AdvanceCurrentTime(TDuration::Seconds(1));
            runtime.DispatchEvents({}, TDuration::MilliSeconds(10));

            auto response = client.RecvReadBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_TIMEOUT, response->GetStatus());
            UNIT_ASSERT(response->GetErrorReason().Contains("request timed out"));
        }

        {
            client.SendWriteBlocksRequest(
                TBlockRange64::WithLength(1024, 3072),
                1);
            runtime.AdvanceCurrentTime(TDuration::Seconds(1));
            runtime.DispatchEvents({}, TDuration::MilliSeconds(10));
            auto response = client.RecvWriteBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_TIMEOUT, response->GetStatus());
            UNIT_ASSERT(response->GetErrorReason().Contains("request timed out"));
        }

        {
            client.SendZeroBlocksRequest(
                TBlockRange64::WithLength(1024, 3072));
            runtime.AdvanceCurrentTime(TDuration::Seconds(1));
            runtime.DispatchEvents({}, TDuration::MilliSeconds(10));
            auto response = client.RecvZeroBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_TIMEOUT, response->GetStatus());
            UNIT_ASSERT(response->GetErrorReason().Contains("request timed out"));
        }
    }

    void DoTestShouldHandleTimedoutIO(NProto::EStorageMediaKind mediaKind)
    {
        TTestBasicRuntime runtime;

        runtime.SetRegistrationObserverFunc(
            [] (auto& runtime, const auto& parentId, const auto& actorId)
        {
            Y_UNUSED(parentId);
            runtime.EnableScheduleForActor(actorId);
        });

        TTestEnv env(
            runtime,
            NProto::VOLUME_IO_OK,
            false,
            TTestEnv::DefaultDevices(runtime.GetNodeId(0)),
            mediaKind);
        env.DiskAgentState->ResponseDelay = TDuration::Max();

        auto& counters = env.StorageStatsServiceState->Counters.Simple;

        TPartitionClient client(runtime, env.ActorId);

        // timeout = 1s
        {
            client.SendReadBlocksRequest(
                TBlockRange64::WithLength(1024, 3072));
            runtime.AdvanceCurrentTime(TDuration::Minutes(10));
            runtime.DispatchEvents();
            auto response = client.RecvReadBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_TIMEOUT, response->GetStatus());
            UNIT_ASSERT(response->GetErrorReason().Contains("timed out"));
        }

        // backoff = 0.5s
        // cumulative = 1.5s
        // timeout = 2s
        {
            client.SendWriteBlocksRequest(
                TBlockRange64::WithLength(1024, 3072),
                1);
            runtime.AdvanceCurrentTime(TDuration::Minutes(10));
            runtime.DispatchEvents();
            auto response = client.RecvWriteBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_TIMEOUT, response->GetStatus());
            UNIT_ASSERT(response->GetErrorReason().Contains("timed out"));
        }

        // backoff = 1s
        // cumulative = 4.5s
        // timeout = 4s
        {
            client.SendZeroBlocksRequest(
                TBlockRange64::WithLength(1024, 3072));
            runtime.AdvanceCurrentTime(TDuration::Minutes(10));
            runtime.DispatchEvents();
            auto response = client.RecvZeroBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_TIMEOUT, response->GetStatus());
            UNIT_ASSERT(response->GetErrorReason().Contains("timed out"));
        }

        // backoff = 1.5s
        // cumulative = 10s
        // timeout = 5s
        {
            client.SendReadBlocksRequest(
                TBlockRange64::WithLength(1024, 3072));
            runtime.AdvanceCurrentTime(TDuration::Minutes(10));
            runtime.DispatchEvents();
            auto response = client.RecvReadBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_TIMEOUT, response->GetStatus());
        }

        // backoff = 2s
        // cumulative = 17s
        // timeout = 5s
        {
            client.SendReadBlocksRequest(
                TBlockRange64::WithLength(1024, 3072));
            runtime.AdvanceCurrentTime(TDuration::Minutes(10));
            runtime.DispatchEvents();
            auto response = client.RecvReadBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_TIMEOUT, response->GetStatus());
        }

        client.SendRequest(
            env.ActorId,
            std::make_unique<TEvNonreplPartitionPrivate::TEvUpdateCounters>()
        );

        runtime.DispatchEvents({}, TDuration::Seconds(1));

        UNIT_ASSERT_VALUES_EQUAL(0, counters.HasBrokenDevice.Value);
        UNIT_ASSERT_VALUES_EQUAL(0, counters.HasBrokenDeviceSilent.Value);

        // the following attempts should get E_IO / E_IO_SILENT
        // backoff = 2.5s
        // cumulative = 24.5s
        // timeout = 5s
        {
            client.SendReadBlocksRequest(
                TBlockRange64::WithLength(1024, 3072));
            runtime.DispatchEvents();
            auto response = client.RecvReadBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_IO_SILENT, response->GetStatus());
        }

        client.SendRequest(
            env.ActorId,
            std::make_unique<TEvNonreplPartitionPrivate::TEvUpdateCounters>()
        );

        runtime.DispatchEvents({}, TDuration::Seconds(1));

        UNIT_ASSERT_VALUES_EQUAL(0, counters.HasBrokenDevice.Value);
        UNIT_ASSERT_VALUES_EQUAL(1, counters.HasBrokenDeviceSilent.Value);

        // after a cooldown the error shouldn't be silent anymore
        {
            runtime.AdvanceCurrentTime(TDuration::Minutes(5));
            client.SendReadBlocksRequest(
                TBlockRange64::WithLength(1024, 3072));
            runtime.DispatchEvents();
            auto response = client.RecvReadBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_IO, response->GetStatus());
        }

        client.SendRequest(
            env.ActorId,
            std::make_unique<TEvNonreplPartitionPrivate::TEvUpdateCounters>()
        );

        runtime.DispatchEvents({}, TDuration::Seconds(1));

        UNIT_ASSERT_VALUES_EQUAL(1, counters.HasBrokenDevice.Value);
        UNIT_ASSERT_VALUES_EQUAL(1, counters.HasBrokenDeviceSilent.Value);

        {
            client.SendWriteBlocksRequest(
                TBlockRange64::WithLength(1024, 3072),
                1);
            runtime.DispatchEvents();
            auto response = client.RecvWriteBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_IO, response->GetStatus());
        }

        {
            client.SendZeroBlocksRequest(
                TBlockRange64::WithLength(1024, 3072));
            runtime.DispatchEvents();
            auto response = client.RecvZeroBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_IO, response->GetStatus());
        }

        client.SendRequest(
            env.ActorId,
            std::make_unique<TEvNonreplPartitionPrivate::TEvUpdateCounters>()
        );

        runtime.DispatchEvents({}, TDuration::Seconds(1));

        UNIT_ASSERT_VALUES_EQUAL(1, counters.HasBrokenDevice.Value);
        UNIT_ASSERT_VALUES_EQUAL(1, counters.HasBrokenDeviceSilent.Value);
    }

    Y_UNIT_TEST(ShouldHandleTimedoutIOSSD)
    {
        DoTestShouldHandleTimedoutIO(NProto::STORAGE_MEDIA_SSD_NONREPLICATED);
    }

    Y_UNIT_TEST(ShouldHandleTimedoutIOHDD)
    {
        DoTestShouldHandleTimedoutIO(NProto::STORAGE_MEDIA_HDD_NONREPLICATED);
    }

    Y_UNIT_TEST(ShouldNotReturnIOErrorUponTimeoutForBackgroundRequests)
    {
        TTestBasicRuntime runtime;

        runtime.SetRegistrationObserverFunc(
            [] (auto& runtime, const auto& parentId, const auto& actorId)
        {
            Y_UNUSED(parentId);
            runtime.EnableScheduleForActor(actorId);
        });

        TTestEnv env(runtime);
        env.DiskAgentState->ResponseDelay = TDuration::Max();

        auto& counters = env.StorageStatsServiceState->Counters.Simple;

        TPartitionClient client(runtime, env.ActorId);

        for (ui32 i = 0; i < 10; ++i) {
            auto request = client.CreateReadBlocksRequest(
                TBlockRange64::WithLength(1024, 3072));
            request->Record.MutableHeaders()->SetIsBackgroundRequest(true);
            client.SendRequest(client.GetActorId(), std::move(request));
            runtime.AdvanceCurrentTime(TDuration::Minutes(10));
            runtime.DispatchEvents();
            auto response = client.RecvReadBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_TIMEOUT, response->GetStatus());
            UNIT_ASSERT(response->GetErrorReason().Contains("timed out"));
        }

        client.SendRequest(
            env.ActorId,
            std::make_unique<TEvNonreplPartitionPrivate::TEvUpdateCounters>()
        );

        runtime.DispatchEvents({}, TDuration::Seconds(1));

        UNIT_ASSERT_VALUES_EQUAL(0, counters.HasBrokenDevice.Value);
        UNIT_ASSERT_VALUES_EQUAL(0, counters.HasBrokenDeviceSilent.Value);

        {
            client.SendReadBlocksRequest(
                TBlockRange64::WithLength(1024, 3072));
            runtime.DispatchEvents();
            auto response = client.RecvReadBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_IO_SILENT, response->GetStatus());
        }

        client.SendRequest(
            env.ActorId,
            std::make_unique<TEvNonreplPartitionPrivate::TEvUpdateCounters>()
        );

        runtime.DispatchEvents({}, TDuration::Seconds(1));

        UNIT_ASSERT_VALUES_EQUAL(0, counters.HasBrokenDevice.Value);
        UNIT_ASSERT_VALUES_EQUAL(1, counters.HasBrokenDeviceSilent.Value);
    }

    Y_UNIT_TEST(ShouldRecoverFromShortTimeoutStreak)
    {
        TTestBasicRuntime runtime;

        runtime.SetRegistrationObserverFunc(
            [] (auto& runtime, const auto& parentId, const auto& actorId)
        {
            Y_UNUSED(parentId);
            runtime.EnableScheduleForActor(actorId);
        });

        TTestEnv env(runtime);
        env.DiskAgentState->ResponseDelay = TDuration::Max();

        TPartitionClient client(runtime, env.ActorId);

        // timeout = 1s
        {
            client.SendReadBlocksRequest(
                TBlockRange64::WithLength(1024, 3072));
            runtime.AdvanceCurrentTime(TDuration::Minutes(10));
            runtime.DispatchEvents();
            auto response = client.RecvReadBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_TIMEOUT, response->GetStatus());
            UNIT_ASSERT(response->GetErrorReason().Contains("timed out"));
        }

        // backoff = 0.5s
        // cumulative = 1.5s
        // timeout = 2s
        {
            client.SendWriteBlocksRequest(
                TBlockRange64::WithLength(1024, 3072),
                1);
            runtime.AdvanceCurrentTime(TDuration::Minutes(10));
            runtime.DispatchEvents();
            auto response = client.RecvWriteBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_TIMEOUT, response->GetStatus());
            UNIT_ASSERT(response->GetErrorReason().Contains("timed out"));
        }

        // backoff = 1s
        // cumulative = 4.5s
        // timeout = 4s
        {
            client.SendZeroBlocksRequest(
                TBlockRange64::WithLength(1024, 3072));
            runtime.AdvanceCurrentTime(TDuration::Minutes(10));
            runtime.DispatchEvents();
            auto response = client.RecvZeroBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_TIMEOUT, response->GetStatus());
            UNIT_ASSERT(response->GetErrorReason().Contains("timed out"));
        }

        // backoff = 1.5s
        // cumulative = 10s
        // timeout = 5s
        {
            client.SendReadBlocksRequest(
                TBlockRange64::WithLength(1024, 3072));
            runtime.AdvanceCurrentTime(TDuration::Minutes(10));
            runtime.DispatchEvents();
            auto response = client.RecvReadBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_TIMEOUT, response->GetStatus());
        }

        env.DiskAgentState->ResponseDelay = TDuration::Zero();
        // backoff = 2s
        // cumulative = 17s
        // timeout = 0s
        {
            client.SendReadBlocksRequest(
                TBlockRange64::WithLength(1024, 3072));
            runtime.DispatchEvents();
            auto response = client.RecvReadBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(S_OK, response->GetStatus());
        }
        env.DiskAgentState->ResponseDelay = TDuration::Max();

        // timeout = 1s
        {
            client.SendReadBlocksRequest(
                TBlockRange64::WithLength(1024, 3072));
            runtime.AdvanceCurrentTime(TDuration::Minutes(10));
            runtime.DispatchEvents();
            auto response = client.RecvReadBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_TIMEOUT, response->GetStatus());
            UNIT_ASSERT(response->GetErrorReason().Contains("timed out"));
        }

        // backoff = 0.5s
        // cumulative = 1.5s
        // timeout = 2s
        {
            client.SendWriteBlocksRequest(
                TBlockRange64::WithLength(1024, 3072),
                1);
            runtime.AdvanceCurrentTime(TDuration::Minutes(10));
            runtime.DispatchEvents();
            auto response = client.RecvWriteBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_TIMEOUT, response->GetStatus());
            UNIT_ASSERT(response->GetErrorReason().Contains("timed out"));
        }

        // backoff = 1s
        // cumulative = 4.5s
        // timeout = 4s
        {
            client.SendZeroBlocksRequest(
                TBlockRange64::WithLength(1024, 3072));
            runtime.AdvanceCurrentTime(TDuration::Minutes(10));
            runtime.DispatchEvents();
            auto response = client.RecvZeroBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_TIMEOUT, response->GetStatus());
            UNIT_ASSERT(response->GetErrorReason().Contains("timed out"));
        }

        // backoff = 1.5s
        // cumulative = 10s
        // timeout = 5s
        {
            client.SendReadBlocksRequest(
                TBlockRange64::WithLength(1024, 3072));
            runtime.AdvanceCurrentTime(TDuration::Minutes(10));
            runtime.DispatchEvents();
            auto response = client.RecvReadBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_TIMEOUT, response->GetStatus());
        }

        // backoff = 2s
        // cumulative = 17s
        // timeout = 5s
        {
            client.SendReadBlocksRequest(
                TBlockRange64::WithLength(1024, 3072));
            runtime.AdvanceCurrentTime(TDuration::Minutes(10));
            runtime.DispatchEvents();
            auto response = client.RecvReadBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_TIMEOUT, response->GetStatus());
        }

        // the following attempt should get E_IO / E_IO_SILENT
        // backoff = 2.5s
        // cumulative = 24.5s
        // timeout = 5s
        {
            client.SendReadBlocksRequest(
                TBlockRange64::WithLength(1024, 3072));
            runtime.DispatchEvents();
            auto response = client.RecvReadBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_IO_SILENT, response->GetStatus());
        }
    }

    Y_UNIT_TEST(ShouldLimitIO)
    {
        TTestBasicRuntime runtime;

        TTestEnv env(runtime);
        env.DiskAgentState->ResponseDelay = TDuration::Max();

        TPartitionClient client(runtime, env.ActorId);

        for (int i = 0; i != 1024; ++i) {
            client.SendReadBlocksRequest(
                TBlockRange64::WithLength(1024, 3072));
        }

        {
            client.SendReadBlocksRequest(
                TBlockRange64::WithLength(1024, 3072));
            runtime.DispatchEvents();
            auto response = client.RecvReadBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_REJECTED, response->GetStatus());
            UNIT_ASSERT_VALUES_EQUAL(response->GetErrorReason(), "Inflight limit reached");
        }

        {
            client.SendWriteBlocksRequest(
                TBlockRange64::WithLength(1024, 3072),
                1);
            runtime.DispatchEvents();
            auto response = client.RecvWriteBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_REJECTED, response->GetStatus());
            UNIT_ASSERT_VALUES_EQUAL(response->GetErrorReason(), "Inflight limit reached");
        }

        {
            client.SendZeroBlocksRequest(
                TBlockRange64::WithLength(1024, 3072));
            runtime.DispatchEvents();
            auto response = client.RecvZeroBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_REJECTED, response->GetStatus());
            UNIT_ASSERT_VALUES_EQUAL(response->GetErrorReason(), "Inflight limit reached");
        }
    }

    Y_UNIT_TEST(ShouldHandleInvalidSessionError)
    {
        TTestBasicRuntime runtime;

        TTestEnv env(runtime);

        TPartitionClient client(runtime, env.ActorId);

        TActorId reacquireDiskRecipient;
        runtime.SetObserverFunc([&] (TAutoPtr<IEventHandle>& event) {
                switch (event->GetTypeRewrite()) {
                    case TEvDiskAgent::EvReadDeviceBlocksRequest: {
                        auto response = std::make_unique<TEvDiskAgent::TEvReadDeviceBlocksResponse>(
                            MakeError(E_BS_INVALID_SESSION, "invalid session")
                        );

                        runtime.Send(new IEventHandle(
                            event->Sender,
                            event->Recipient,
                            response.release(),
                            0, // flags
                            event->Cookie
                        ), 0);

                        return TTestActorRuntime::EEventAction::DROP;
                    }

                    case TEvDiskAgent::EvWriteDeviceBlocksRequest: {
                        auto response = std::make_unique<TEvDiskAgent::TEvWriteDeviceBlocksResponse>(
                            MakeError(E_BS_INVALID_SESSION, "invalid session")
                        );

                        runtime.Send(new IEventHandle(
                            event->Sender,
                            event->Recipient,
                            response.release(),
                            0, // flags
                            event->Cookie
                        ), 0);

                        return TTestActorRuntime::EEventAction::DROP;
                    }

                    case TEvDiskAgent::EvZeroDeviceBlocksRequest: {
                        auto response = std::make_unique<TEvDiskAgent::TEvZeroDeviceBlocksResponse>(
                            MakeError(E_BS_INVALID_SESSION, "invalid session")
                        );

                        runtime.Send(new IEventHandle(
                            event->Sender,
                            event->Recipient,
                            response.release(),
                            0, // flags
                            event->Cookie
                        ), 0);

                        return TTestActorRuntime::EEventAction::DROP;
                    }

                    case TEvVolume::EvReacquireDisk: {
                        reacquireDiskRecipient = event->Recipient;

                        return TTestActorRuntime::EEventAction::DROP;
                    }
                }

                return TTestActorRuntime::DefaultObserverFunc(event);
            }
        );

        {
            client.SendReadBlocksRequest(
                TBlockRange64::WithLength(0, 1024));
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

        {
            client.SendZeroBlocksRequest(
                TBlockRange64::WithLength(0, 1024));
            auto response = client.RecvZeroBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_REJECTED, response->GetStatus());
        }

        UNIT_ASSERT_VALUES_EQUAL(env.VolumeActorId, reacquireDiskRecipient);
    }

    Y_UNIT_TEST(ShouldSupportReadOnlyMode)
    {
        TTestBasicRuntime runtime;

        TTestEnv env(runtime, NProto::VOLUME_IO_ERROR_READ_ONLY);

        TPartitionClient client(runtime, env.ActorId);

        TString expectedBlockData(DefaultBlockSize, 0);

        auto readBlocks = [&]
        {
            auto response = client.ReadBlocks(
                TBlockRange64::WithLength(1024, 3072));
            const auto& blocks = response->Record.GetBlocks();

            UNIT_ASSERT_VALUES_EQUAL(3072, blocks.BuffersSize());
            UNIT_ASSERT_VALUES_EQUAL(DefaultBlockSize, blocks.GetBuffers(0).size());
            UNIT_ASSERT_VALUES_EQUAL(expectedBlockData, blocks.GetBuffers(0));

            UNIT_ASSERT_VALUES_EQUAL(DefaultBlockSize, blocks.GetBuffers(3071).size());
            UNIT_ASSERT_VALUES_EQUAL(expectedBlockData, blocks.GetBuffers(3071));
        };

        readBlocks();

        {
            client.SendWriteBlocksRequest(
                TBlockRange64::WithLength(1024, 3072),
                1);
            auto response = client.RecvWriteBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_IO, response->GetStatus());
        }
        {
            client.SendWriteBlocksRequest(
                TBlockRange64::MakeClosedInterval(1024, 4023),
                2);
            auto response = client.RecvWriteBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_IO, response->GetStatus());
        }

        readBlocks();

        {
            client.SendZeroBlocksRequest(
                TBlockRange64::MakeClosedInterval(2024, 3023));
            auto response = client.RecvZeroBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_IO, response->GetStatus());
        }

        readBlocks();

        expectedBlockData = TString(DefaultBlockSize, 'A');
        {
            auto request = client.CreateWriteBlocksLocalRequest(
                TBlockRange64::WithLength(1024, 3072),
                expectedBlockData);
            request->Record.MutableHeaders()->SetIsBackgroundRequest(true);
            client.SendRequest(client.GetActorId(), std::move(request));
            auto response = client.RecvWriteBlocksLocalResponse();
            UNIT_ASSERT_VALUES_EQUAL_C(
                S_OK,
                response->GetStatus(),
                response->GetErrorReason());
        }

        readBlocks();
    }

    Y_UNIT_TEST(ShouldSendStatsToVolume)
    {
        TTestBasicRuntime runtime;

        runtime.SetRegistrationObserverFunc(
            [] (auto& runtime, const auto& parentId, const auto& actorId)
        {
            Y_UNUSED(parentId);
            runtime.EnableScheduleForActor(actorId);
        });

        TTestEnv env(runtime);

        bool done = false;
        runtime.SetObserverFunc([&] (TAutoPtr<IEventHandle>& event) {
                switch (event->GetTypeRewrite()) {
                    case TEvStatsService::EvVolumePartCounters:
                        if (event->Recipient == MakeStorageStatsServiceId()) {
                            done = true;
                        }
                        break;
                }

                return TTestActorRuntime::DefaultObserverFunc(event);
            }
        );

        TPartitionClient client(runtime, env.ActorId);

        {
            runtime.AdvanceCurrentTime(TDuration::Seconds(15));
            runtime.DispatchEvents({}, TDuration::Seconds(1));
            UNIT_ASSERT_VALUES_EQUAL(true, done);
        }
    }

    Y_UNIT_TEST(ShouldEnforceWritableStateWithUnavailableAgent)
    {
        TTestBasicRuntime runtime;

        runtime.SetRegistrationObserverFunc(
            [] (auto& runtime, const auto& parentId, const auto& actorId)
        {
            Y_UNUSED(parentId);
            runtime.EnableScheduleForActor(actorId);
        });

        TTestEnv env(runtime);
        env.DiskAgentState->ResponseDelay = TDuration::Max();

        TPartitionClient client(runtime, env.ActorId);

        // wait for vasya
        for (;;) {
            client.SendReadBlocksRequest(
                TBlockRange64::MakeClosedInterval(0, 1024));
            runtime.AdvanceCurrentTime(TDuration::Minutes(10));
            runtime.DispatchEvents();
            auto response = client.RecvReadBlocksResponse();
            if (response->GetStatus() == E_IO_SILENT) {
                break;
            }
            UNIT_ASSERT_VALUES_EQUAL(E_TIMEOUT, response->GetStatus());
            UNIT_ASSERT(response->GetErrorReason().Contains("timed out"));
        }

        // read from petya
        {
            client.SendReadBlocksRequest(
                TBlockRange64::MakeClosedInterval(2048, 3072));
            runtime.DispatchEvents();
            auto response = client.RecvReadBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_TIMEOUT, response->GetStatus());
            UNIT_ASSERT(response->GetErrorReason().Contains("timed out"));
        }

        // can't write to petya
        {
            client.SendWriteBlocksRequest(
                TBlockRange64::MakeClosedInterval(2048, 3072),
                1);
            runtime.DispatchEvents();
            auto response = client.RecvWriteBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL(E_IO_SILENT, response->GetStatus());
            UNIT_ASSERT(response->GetErrorReason().Contains("disk has broken device"));
        }
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
}

}   // namespace NCloud::NBlockStore::NStorage
