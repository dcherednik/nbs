#include "part_mirror.h"
#include "part_mirror_actor.h"
#include "ut_env.h"

#include <cloud/blockstore/libs/common/sglist_test.h>
#include <cloud/blockstore/libs/diagnostics/block_digest.h>
#include <cloud/blockstore/libs/diagnostics/profile_log.h>
#include <cloud/blockstore/libs/storage/api/disk_agent.h>
#include <cloud/blockstore/libs/storage/api/disk_registry_proxy.h>
#include <cloud/blockstore/libs/storage/api/volume.h>
#include <cloud/blockstore/libs/storage/core/config.h>
#include <cloud/blockstore/libs/storage/protos/disk.pb.h>
#include <cloud/blockstore/libs/storage/testlib/disk_agent_mock.h>

#include <contrib/ydb/core/testlib/basics/runtime.h>
#include <contrib/ydb/core/testlib/tablet_helpers.h>

#include <library/cpp/testing/unittest/registar.h>

#include <util/datetime/base.h>
#include <util/string/builder.h>

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

    static TDevices DefaultReplica(ui64 nodeId, ui32 replicaId)
    {
        auto devices = DefaultDevices(nodeId);
        for (auto& device: devices) {
            if (device.GetDeviceUUID()) {
                device.SetDeviceUUID(TStringBuilder() << device.GetDeviceUUID()
                    << "#" << replicaId);
            }
        }
        return devices;
    }

    static TMigrations DefaultMigrations(ui64 nodeId, ui32 replicaId)
    {
        TMigrations migrations;
        auto devices = replicaId
            ? DefaultReplica(nodeId, replicaId)
            : DefaultDevices(nodeId);
        for (auto& device: devices) {
            if (device.GetDeviceUUID()) {
                auto* m = migrations.Add();
                m->SetSourceDeviceId(device.GetDeviceUUID());
                m->MutableTargetDevice()->CopyFrom(device);
                m->MutableTargetDevice()->SetDeviceUUID(
                    TStringBuilder() << device.GetDeviceUUID() << "-migration");
            }
        }
        return migrations;
    }

    TTestEnv(TTestActorRuntime& runtime)
        : TTestEnv(
            runtime,
            false,
            DefaultDevices(runtime.GetNodeId(0)),
            TVector<TDevices>{
                DefaultReplica(runtime.GetNodeId(0), 1),
                DefaultReplica(runtime.GetNodeId(0), 2),
            }
        )
    {
    }

    TTestEnv(
            TTestActorRuntime& runtime,
            bool markBlocksUsed,
            TDevices devices,
            TVector<TDevices> replicas,
            TMigrations migrations = {},
            THashSet<TString> freshDeviceIds = {})
        : Runtime(runtime)
        , ActorId(0, "YYY")
        , VolumeActorId(0, "VVV")
        , StorageStatsServiceState(MakeIntrusive<TStorageStatsServiceState>())
        , DiskAgentState(std::make_shared<TDiskAgentState>())
    {
        SetupLogging();

        NProto::TStorageServiceConfig storageConfig;
        storageConfig.SetMaxTimedOutDeviceStateDuration(20'000);
        storageConfig.SetNonReplicatedMinRequestTimeoutSSD(1'000);
        storageConfig.SetNonReplicatedMaxRequestTimeoutSSD(5'000);

        auto config = std::make_shared<TStorageConfig>(
            std::move(storageConfig),
            std::make_shared<TFeaturesConfig>(NProto::TFeaturesConfig())
        );

        auto nodeId = Runtime.GetNodeId(0);

        TDevices allDevices;
        for (const auto& d: devices) {
            allDevices.Add()->CopyFrom(d);
        }
        for (const auto& r: replicas) {
            for (const auto& d: r) {
                allDevices.Add()->CopyFrom(d);
            }
        }
        for (auto& m: migrations) {
            allDevices.Add()->CopyFrom(m.GetTargetDevice());
            ToLogicalBlocks(*m.MutableTargetDevice(), DefaultBlockSize);
        }

        Runtime.AddLocalService(
            MakeDiskAgentServiceId(nodeId),
            TActorSetupCmd(
                new TDiskAgentMock(allDevices, DiskAgentState),
                TMailboxType::Simple,
                0
            )
        );

        auto partConfig = std::make_shared<TNonreplicatedPartitionConfig>(
            ToLogicalBlocks(devices, DefaultBlockSize),
            NProto::VOLUME_IO_OK,
            "test",
            DefaultBlockSize,
            TNonreplicatedPartitionConfig::TVolumeInfo{
                Now(),
                // only SSD/HDD distinction matters
                NProto::STORAGE_MEDIA_SSD_MIRROR3},
            VolumeActorId,
            false, // muteIOErrors
            markBlocksUsed,
            std::move(freshDeviceIds),
            TDuration::Zero(), // maxTimedOutDeviceStateDuration
            false, // maxTimedOutDeviceStateDurationOverridden
            true // useSimpleMigrationBandwidthLimiter
        );

        for (auto& replica: replicas) {
            replica = ToLogicalBlocks(replica, DefaultBlockSize);
        }

        auto part = std::make_unique<TMirrorPartitionActor>(
            std::move(config),
            CreateProfileLogStub(),
            CreateBlockDigestGeneratorStub(),
            "", // rwClientId
            std::move(partConfig),
            std::move(migrations),
            std::move(replicas),
            nullptr, // rdmaClient
            VolumeActorId,
            TActorId() // resyncActorId
        );

        Runtime.AddLocalService(
            ActorId,
            TActorSetupCmd(part.release(), TMailboxType::Simple, 0)
        );

        auto volume = std::make_unique<TDummyActor>();

        Runtime.AddLocalService(
            VolumeActorId,
            TActorSetupCmd(volume.release(), TMailboxType::Simple, 0)
        );

        auto diskRegistry = std::make_unique<TDummyActor>();

        Runtime.AddLocalService(
            MakeDiskRegistryProxyServiceId(),
            TActorSetupCmd(diskRegistry.release(), TMailboxType::Simple, 0)
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

        for (ui32 i = TBlockStoreComponents::START; i < TBlockStoreComponents::END; ++i) {
           Runtime.SetLogPriority(i, NLog::PRI_INFO);
        }
        // Runtime.SetLogPriority(NLog::InvalidComponent, NLog::PRI_DEBUG);
    }
};

}   // namespace

////////////////////////////////////////////////////////////////////////////////

Y_UNIT_TEST_SUITE(TMirrorPartitionTest)
{
    // TODO: reduce code duplication (see part_nonrepl_ut.cpp)

    Y_UNIT_TEST(ShouldReadWriteZero)
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
            auto response = client.ReadBlocks(
                TBlockRange64::WithLength(1024, 3072));
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
            auto response = client.ReadBlocks(
                TBlockRange64::WithLength(1024, 3072));
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

        client.WriteBlocks(TBlockRange64::MakeClosedInterval(5000, 5199), 3);
        client.ZeroBlocks(TBlockRange64::MakeClosedInterval(5050, 5150));

        {
            auto response = client.ReadBlocks(
                TBlockRange64::MakeClosedInterval(5000, 5119));
            const auto& blocks = response->Record.GetBlocks();
            UNIT_ASSERT_VALUES_EQUAL(120, blocks.BuffersSize());
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
        }

        {
            client.SendReadBlocksRequest(
                TBlockRange64::MakeClosedInterval(5000, 5120));
            auto response = client.RecvReadBlocksResponse();
            UNIT_ASSERT_VALUES_EQUAL_C(
                E_INVALID_STATE,
                response->GetStatus(),
                response->GetErrorReason());
        }

        runtime.AdvanceCurrentTime(UpdateCountersInterval);
        runtime.DispatchEvents({}, TDuration::Seconds(1));
        runtime.AdvanceCurrentTime(UpdateCountersInterval);
        runtime.DispatchEvents({}, TDuration::Seconds(1));

        auto& counters = env.StorageStatsServiceState->Counters.RequestCounters;
        UNIT_ASSERT_VALUES_EQUAL(4, counters.ReadBlocks.Count);
        UNIT_ASSERT_VALUES_EQUAL(
            DefaultBlockSize * 9336,
            counters.ReadBlocks.RequestBytes
        );
        UNIT_ASSERT_VALUES_EQUAL(3 * 3, counters.WriteBlocks.Count);
        UNIT_ASSERT_VALUES_EQUAL(
            3 * DefaultBlockSize * 6192,
            counters.WriteBlocks.RequestBytes
        );
        UNIT_ASSERT_VALUES_EQUAL(3 * 2, counters.ZeroBlocks.Count);
        UNIT_ASSERT_VALUES_EQUAL(
            3 * DefaultBlockSize * 1070,
            counters.ZeroBlocks.RequestBytes
        );
    }

    Y_UNIT_TEST(ShouldLocalReadWrite)
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

        const auto blockRange2 = TBlockRange64::MakeClosedInterval(5000, 5199);
        client.WriteBlocksLocal(blockRange2, TString(DefaultBlockSize, 'B'));

        const auto blockRange3 = TBlockRange64::MakeClosedInterval(5000, 5150);

        {
            TVector<TString> blocks;

            client.ReadBlocksLocal(
                TBlockRange64::MakeClosedInterval(5000, 5119),
                TGuardedSgList(ResizeBlocks(
                    blocks,
                    120,
                    TString(DefaultBlockSize, '\0')
                )));

            for (ui32 i = 0; i < 120; ++i) {
                const auto& block = blocks[i];
                for (auto c: block) {
                    UNIT_ASSERT_VALUES_EQUAL('B', c);
                }
            }
        }

        {
            TVector<TString> blocks;

            client.SendReadBlocksLocalRequest(
                blockRange3,
                TGuardedSgList(ResizeBlocks(
                    blocks,
                    blockRange3.Size(),
                    TString(DefaultBlockSize, '\0')
                )));
            auto response = client.RecvReadBlocksLocalResponse();
            UNIT_ASSERT_VALUES_EQUAL_C(
                E_INVALID_STATE,
                response->GetStatus(),
                response->GetErrorReason());
        }

        runtime.AdvanceCurrentTime(UpdateCountersInterval);
        runtime.DispatchEvents({}, TDuration::Seconds(1));
        runtime.AdvanceCurrentTime(UpdateCountersInterval);
        runtime.DispatchEvents({}, TDuration::Seconds(1));

        auto& counters = env.StorageStatsServiceState->Counters.RequestCounters;
        UNIT_ASSERT_VALUES_EQUAL(2, counters.ReadBlocks.Count);
        UNIT_ASSERT_VALUES_EQUAL(
            DefaultBlockSize * (
                blockRange1.Size() + blockRange3.Intersect(diskRange).Size()
            ),
            counters.ReadBlocks.RequestBytes
        );
        UNIT_ASSERT_VALUES_EQUAL(3 * 2, counters.WriteBlocks.Count);
        UNIT_ASSERT_VALUES_EQUAL(
            3 * DefaultBlockSize * (
                blockRange1.Size() + blockRange2.Intersect(diskRange).Size()
            ),
            counters.WriteBlocks.RequestBytes
        );
    }

    Y_UNIT_TEST(ShouldMirrorWriteAndZeroRequests)
    {
        TTestBasicRuntime runtime;

        runtime.SetRegistrationObserverFunc(
            [] (auto& runtime, const auto& parentId, const auto& actorId)
        {
            Y_UNUSED(parentId);
            runtime.EnableScheduleForActor(actorId);
        });

        THashMap<TString, TBlockRange64> device2WriteRange;
        THashMap<TString, TBlockRange64> device2ZeroRange;

        runtime.SetObserverFunc([&] (TAutoPtr<IEventHandle>& event) {
                switch (event->GetTypeRewrite()) {
                    case TEvDiskAgent::EvWriteDeviceBlocksRequest: {
                        using TRequest =
                            TEvDiskAgent::TEvWriteDeviceBlocksRequest;
                        const auto& record = event->Get<TRequest>()->Record;
                        UNIT_ASSERT(!device2WriteRange.contains(record.GetDeviceUUID()));
                        device2WriteRange[record.GetDeviceUUID()] =
                            TBlockRange64::WithLength(
                                record.GetStartIndex(),
                                record.GetBlocks().GetBuffers().size());
                        break;
                    }

                    case TEvDiskAgent::EvZeroDeviceBlocksRequest: {
                        using TRequest =
                            TEvDiskAgent::TEvZeroDeviceBlocksRequest;
                        const auto& record = event->Get<TRequest>()->Record;
                        UNIT_ASSERT(!device2ZeroRange.contains(record.GetDeviceUUID()));
                        device2ZeroRange[record.GetDeviceUUID()] =
                            TBlockRange64::WithLength(
                                record.GetStartIndex(),
                                record.GetBlocksCount());
                        break;
                    }
                }

                return TTestActorRuntime::DefaultObserverFunc(event);
            }
        );

        TTestEnv env(runtime);

        TPartitionClient client(runtime, env.ActorId);

        client.WriteBlocks(TBlockRange64::WithLength(1024, 3072), 1);
        client.ZeroBlocks(TBlockRange64::WithLength(0, 3072));

        UNIT_ASSERT_VALUES_EQUAL(6, device2WriteRange.size());
        UNIT_ASSERT_VALUES_EQUAL(6, device2ZeroRange.size());
        UNIT_ASSERT_VALUES_EQUAL(
            DescribeRange(TBlockRange64::WithLength(1024, 1024)),
            DescribeRange(device2WriteRange["vasya"]));
        UNIT_ASSERT_VALUES_EQUAL(
            DescribeRange(TBlockRange64::WithLength(0, 2048)),
            DescribeRange(device2WriteRange["petya"]));
        UNIT_ASSERT_VALUES_EQUAL(
            DescribeRange(TBlockRange64::WithLength(1024, 1024)),
            DescribeRange(device2WriteRange["vasya#1"]));
        UNIT_ASSERT_VALUES_EQUAL(
            DescribeRange(TBlockRange64::WithLength(0, 2048)),
            DescribeRange(device2WriteRange["petya#1"]));
        UNIT_ASSERT_VALUES_EQUAL(
            DescribeRange(TBlockRange64::WithLength(1024, 1024)),
            DescribeRange(device2WriteRange["vasya#2"]));
        UNIT_ASSERT_VALUES_EQUAL(
            DescribeRange(TBlockRange64::WithLength(0, 2048)),
            DescribeRange(device2WriteRange["petya#2"]));

        UNIT_ASSERT_VALUES_EQUAL(
            DescribeRange(TBlockRange64::WithLength(0, 2048)),
            DescribeRange(device2ZeroRange["vasya"]));
        UNIT_ASSERT_VALUES_EQUAL(
            DescribeRange(TBlockRange64::WithLength(0, 1024)),
            DescribeRange(device2ZeroRange["petya"]));
        UNIT_ASSERT_VALUES_EQUAL(
            DescribeRange(TBlockRange64::WithLength(0, 2048)),
            DescribeRange(device2ZeroRange["vasya#1"]));
        UNIT_ASSERT_VALUES_EQUAL(
            DescribeRange(TBlockRange64::WithLength(0, 1024)),
            DescribeRange(device2ZeroRange["petya#1"]));
        UNIT_ASSERT_VALUES_EQUAL(
            DescribeRange(TBlockRange64::WithLength(0, 2048)),
            DescribeRange(device2ZeroRange["vasya#2"]));
        UNIT_ASSERT_VALUES_EQUAL(
            DescribeRange(TBlockRange64::WithLength(0, 1024)),
            DescribeRange(device2ZeroRange["petya#2"]));

        device2WriteRange.clear();

        client.WriteBlocksLocal(
            TBlockRange64::MakeClosedInterval(1000, 4000),
            TString(DefaultBlockSize, 'A'));

        UNIT_ASSERT_VALUES_EQUAL(6, device2WriteRange.size());
        UNIT_ASSERT_VALUES_EQUAL(
            DescribeRange(TBlockRange64::MakeClosedInterval(1000, 2047)),
            DescribeRange(device2WriteRange["vasya"]));
        UNIT_ASSERT_VALUES_EQUAL(
            DescribeRange(TBlockRange64::MakeClosedInterval(0, 1952)),
            DescribeRange(device2WriteRange["petya"]));
        UNIT_ASSERT_VALUES_EQUAL(
            DescribeRange(TBlockRange64::MakeClosedInterval(1000, 2047)),
            DescribeRange(device2WriteRange["vasya#1"]));
        UNIT_ASSERT_VALUES_EQUAL(
            DescribeRange(TBlockRange64::MakeClosedInterval(0, 1952)),
            DescribeRange(device2WriteRange["petya#1"]));
        UNIT_ASSERT_VALUES_EQUAL(
            DescribeRange(TBlockRange64::MakeClosedInterval(1000, 2047)),
            DescribeRange(device2WriteRange["vasya#2"]));
        UNIT_ASSERT_VALUES_EQUAL(
            DescribeRange(TBlockRange64::MakeClosedInterval(0, 1952)),
            DescribeRange(device2WriteRange["petya#2"]));
    }

    Y_UNIT_TEST(ShouldNotReadFromFreshDevices)
    {
        TTestBasicRuntime runtime;

        runtime.SetRegistrationObserverFunc(
            [] (auto& runtime, const auto& parentId, const auto& actorId)
        {
            Y_UNUSED(parentId);
            runtime.EnableScheduleForActor(actorId);
        });

        const THashSet<TString> freshDeviceIds{"vasya", "vasya#1", "petya#2"};
        TTestEnv env(
            runtime,
            false, // markBlocksUsed
            TTestEnv::DefaultDevices(runtime.GetNodeId(0)),
            TVector<TDevices>{
                TTestEnv::DefaultReplica(runtime.GetNodeId(0), 1),
                TTestEnv::DefaultReplica(runtime.GetNodeId(0), 2),
            },
            {}, // migrations
            freshDeviceIds);

        TPartitionClient client(runtime, env.ActorId);

        // vasya should be migrated => 2 ranges
        WaitForMigrations(runtime, 2);

        const auto diskRange = TBlockRange64::WithLength(0, 5120);
        client.WriteBlocksLocal(diskRange, TString(DefaultBlockSize, 'A'));

        {
            auto nodeId = runtime.GetNodeId(0);
            auto diskAgentActorId = MakeDiskAgentServiceId(nodeId);

            for (const auto& deviceId: freshDeviceIds) {
                auto sender = runtime.AllocateEdgeActor();

                auto request =
                    std::make_unique<TEvDiskAgent::TEvZeroDeviceBlocksRequest>();

                request->Record.SetStartIndex(0);
                request->Record.SetBlocksCount(Max<ui32>());
                request->Record.SetBlockSize(4_KB);
                request->Record.SetDeviceUUID(deviceId);

                runtime.Send(new IEventHandle(
                    diskAgentActorId,
                    sender,
                    request.release()));
            }

            runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));
        }

#define TEST_READ(blockRange) {                                                \
            TVector<TString> blocks;                                           \
                                                                               \
            client.ReadBlocksLocal(                                            \
                blockRange,                                                    \
                TGuardedSgList(ResizeBlocks(                                   \
                    blocks,                                                    \
                    blockRange.Size(),                                         \
                    TString(DefaultBlockSize, '\0')                            \
                )));                                                           \
                                                                               \
            for (const auto& block: blocks) {                                  \
                for (auto c: block) {                                          \
                    UNIT_ASSERT_VALUES_EQUAL('A', c);                          \
                }                                                              \
            }                                                                  \
        }                                                                      \
// TEST_READ

        // doing multiple reads to check that none of them targets fresh devices
        TEST_READ(TBlockRange64::MakeOneBlock(0));
        TEST_READ(TBlockRange64::MakeOneBlock(0));
        TEST_READ(TBlockRange64::MakeOneBlock(0));

        TEST_READ(TBlockRange64::MakeOneBlock(2047));
        TEST_READ(TBlockRange64::MakeOneBlock(2047));
        TEST_READ(TBlockRange64::MakeOneBlock(2047));

        TEST_READ(TBlockRange64::MakeOneBlock(2048));
        TEST_READ(TBlockRange64::MakeOneBlock(2048));
        TEST_READ(TBlockRange64::MakeOneBlock(2048));

        TEST_READ(TBlockRange64::MakeOneBlock(5119));
        TEST_READ(TBlockRange64::MakeOneBlock(5119));
        TEST_READ(TBlockRange64::MakeOneBlock(5119));

#undef TEST_READ
    }

    struct TMigrationTestRuntime
    {
        TTestBasicRuntime Runtime;

        TString DiskId;
        TString SourceDeviceId;
        TString TargetDeviceId;
        bool FinishRequestObserved = false;
        ui32 MigratedRanges = 0;

        TMigrationTestRuntime()
        {
            auto obs = [&] (auto& event) {
                switch (event->GetTypeRewrite()) {
                    case TEvDiskRegistry::EvFinishMigrationRequest: {
                        UNIT_ASSERT(!FinishRequestObserved);
                        FinishRequestObserved = true;

                        using TEv = TEvDiskRegistry::TEvFinishMigrationRequest;
                        auto request = event->template Get<TEv>();
                        DiskId = request->Record.GetDiskId();
                        auto& migrations = request->Record.GetMigrations();
                        UNIT_ASSERT_VALUES_EQUAL(1, migrations.size());
                        SourceDeviceId = migrations[0].GetSourceDeviceId();
                        TargetDeviceId = migrations[0].GetTargetDeviceId();

                        break;
                    }

                    case TEvNonreplPartitionPrivate::EvRangeMigrated: {
                        using TEv = TEvNonreplPartitionPrivate::TEvRangeMigrated;
                        auto* msg = event->template Get<TEv>();
                        if (!HasError(msg->Error)) {
                            ++MigratedRanges;
                        }

                        break;
                    }
                }

                return TTestActorRuntime::DefaultObserverFunc(event);
            };

            Runtime.SetRegistrationObserverFunc(
                [=] (auto& runtime, const auto& parentId, const auto& actorId)
            {
                Y_UNUSED(parentId);
                runtime.SetObserverFunc(obs);
                runtime.EnableScheduleForActor(actorId);
            });
        }

        void WriteToAgent(TString deviceId, char c, ui32 blockCount)
        {
            auto sender = Runtime.AllocateEdgeActor();

            auto request =
                std::make_unique<TEvDiskAgent::TEvWriteDeviceBlocksRequest>();

            request->Record.SetStartIndex(0);
            request->Record.SetBlockSize(4_KB);
            request->Record.SetDeviceUUID(deviceId);
            auto& blocks = *request->Record.MutableBlocks();

            for (ui32 i = 0; i < blockCount; ++i) {
                *blocks.AddBuffers() = TString(4_KB, c);
            }

            auto diskAgentActorId = MakeDiskAgentServiceId(Runtime.GetNodeId(0));
            Runtime.Send(new IEventHandle(
                diskAgentActorId,
                sender,
                request.release()));
        };

        void TestAgentData(TString deviceId, char c, ui32 blockCount)
        {
            auto sender = Runtime.AllocateEdgeActor();

            auto request =
                std::make_unique<TEvDiskAgent::TEvReadDeviceBlocksRequest>();

            request->Record.SetStartIndex(0);
            request->Record.SetBlockSize(4_KB);
            request->Record.SetDeviceUUID(deviceId);
            request->Record.SetBlocksCount(blockCount);

            auto diskAgentActorId = MakeDiskAgentServiceId(Runtime.GetNodeId(0));
            Runtime.Send(new IEventHandle(
                diskAgentActorId,
                sender,
                request.release()));

            TAutoPtr<IEventHandle> handle;
            using TResponse = TEvDiskAgent::TEvReadDeviceBlocksResponse;
            Runtime.GrabEdgeEventRethrow<TResponse>(handle, WaitTimeout);

            UNIT_ASSERT(handle);
            auto response = handle->Release<TResponse>();

            const auto& buffers = response->Record.GetBlocks().GetBuffers();
            UNIT_ASSERT_VALUES_EQUAL(blockCount, buffers.size());

            UNIT_ASSERT_VALUES_EQUAL(TString(4_KB, c), buffers[0]);
            UNIT_ASSERT_VALUES_EQUAL(TString(4_KB, c), buffers[blockCount - 1]);
        };
    };

    Y_UNIT_TEST(ShouldCopyDataToFreshDevices)
    {
        TMigrationTestRuntime mr;
        auto& runtime = mr.Runtime;

        const THashSet<TString> freshDeviceIds{"vasya", "vasya#1", "petya#2"};
        TTestEnv env(
            runtime,
            false, // markBlocksUsed
            TTestEnv::DefaultDevices(runtime.GetNodeId(0)),
            TVector<TDevices>{
                TTestEnv::DefaultReplica(runtime.GetNodeId(0), 1),
                TTestEnv::DefaultReplica(runtime.GetNodeId(0), 2),
            },
            {}, // migrations
            freshDeviceIds);

        mr.WriteToAgent("vasya#2", 'A', 2048);
        mr.WriteToAgent("petya", 'B', 3072);
        mr.WriteToAgent("petya#1", 'B', 3072);

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        TPartitionClient client(runtime, env.ActorId);

        TDispatchOptions options;
        options.FinalEvents.emplace_back(
            TEvDiskRegistry::EvFinishMigrationRequest);
        runtime.DispatchEvents(options);
        // vasya should be migrated => 2 ranges
        UNIT_ASSERT_VALUES_EQUAL(2, mr.MigratedRanges);

        UNIT_ASSERT_VALUES_EQUAL("test", mr.DiskId);
        UNIT_ASSERT_VALUES_EQUAL("vasya#2", mr.SourceDeviceId);
        UNIT_ASSERT_VALUES_EQUAL("vasya", mr.TargetDeviceId);

        mr.TestAgentData("vasya", 'A', 2048);

        // TODO trigger and test migration for petya and petya#1
    }

    Y_UNIT_TEST(ShouldMigrateDevices)
    {
        // this test tests actual migrations, not replication

        TMigrationTestRuntime mr;
        auto& runtime = mr.Runtime;

        TTestEnv env(
            runtime,
            false, // markBlocksUsed
            TTestEnv::DefaultDevices(runtime.GetNodeId(0)),
            TVector<TDevices>{
                TTestEnv::DefaultReplica(runtime.GetNodeId(0), 1),
                TTestEnv::DefaultReplica(runtime.GetNodeId(0), 2),
            },
            TTestEnv::DefaultMigrations(runtime.GetNodeId(0), 0),
            {}  // freshDeviceIds
        );

        mr.WriteToAgent("vasya", 'A', 2048);
        mr.WriteToAgent("petya", 'B', 3072);

        runtime.DispatchEvents(TDispatchOptions(), TDuration::Seconds(1));

        TPartitionClient client(runtime, env.ActorId);

        TDispatchOptions options;
        options.FinalEvents.emplace_back(
            TEvDiskRegistry::EvFinishMigrationRequest);
        runtime.DispatchEvents(options);
        // vasya should be migrated => 2 ranges
        UNIT_ASSERT_VALUES_EQUAL(2, mr.MigratedRanges);

        UNIT_ASSERT_VALUES_EQUAL("test", mr.DiskId);
        UNIT_ASSERT_VALUES_EQUAL("vasya", mr.SourceDeviceId);
        UNIT_ASSERT_VALUES_EQUAL("vasya-migration", mr.TargetDeviceId);

        mr.TestAgentData("vasya-migration", 'A', 2048);

        // TODO trigger and test migration for petya and petya#1
    }

    Y_UNIT_TEST(ShouldTransformIOErrorToRetriable)
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

        const auto blockRange = TBlockRange64::WithLength(1024, 3072);
        client.WriteBlocksLocal(blockRange, TString(DefaultBlockSize, 'A'));

        env.DiskAgentState->Error = MakeError(E_IO, "io error");

        {
            TVector<TString> blocks;

            client.SendReadBlocksLocalRequest(
                blockRange,
                TGuardedSgList(ResizeBlocks(
                    blocks,
                    blockRange.Size(),
                    TString(DefaultBlockSize, '\0')
                )));
            auto response = client.RecvReadBlocksLocalResponse();
            UNIT_ASSERT_VALUES_EQUAL_C(
                E_REJECTED,
                response->GetStatus(),
                response->GetErrorReason());
        }

        {
            TString data(DefaultBlockSize, 'B');
            client.SendWriteBlocksLocalRequest(blockRange, data);

            auto response = client.RecvWriteBlocksLocalResponse();
            UNIT_ASSERT_VALUES_EQUAL_C(
                E_REJECTED,
                response->GetStatus(),
                response->GetErrorReason());
        }

        env.DiskAgentState->Error = {};

        {
            TVector<TString> blocks;

            client.ReadBlocksLocal(
                blockRange,
                TGuardedSgList(ResizeBlocks(
                    blocks,
                    blockRange.Size(),
                    TString(DefaultBlockSize, '\0')
                )));

            for (const auto& block: blocks) {
                for (auto c: block) {
                    UNIT_ASSERT_VALUES_EQUAL('A', c);
                }
            }
        }
    }

    Y_UNIT_TEST(ShouldReportSimpleCounters)
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

        runtime.AdvanceCurrentTime(UpdateCountersInterval);
        runtime.DispatchEvents({}, TDuration::Seconds(1));
        runtime.AdvanceCurrentTime(UpdateCountersInterval);
        runtime.DispatchEvents({}, TDuration::Seconds(1));

        auto& counters = env.StorageStatsServiceState->Counters.Simple;
        UNIT_ASSERT_VALUES_EQUAL(
            6 * 1024 * DefaultBlockSize,
            counters.BytesCount.Value);
    }
}

}   // namespace NCloud::NBlockStore::NStorage
