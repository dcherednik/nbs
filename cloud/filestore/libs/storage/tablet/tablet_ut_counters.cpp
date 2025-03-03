#include "tablet.h"

#include <cloud/filestore/libs/diagnostics/metrics/registry.h>
#include <cloud/filestore/libs/diagnostics/metrics/visitor.h>
#include <cloud/filestore/libs/storage/testlib/tablet_client.h>
#include <cloud/filestore/libs/storage/testlib/test_env.h>

#include <library/cpp/testing/unittest/registar.h>

#include <util/stream/file.h>

#include <memory>


namespace NCloud::NFileStore::NStorage {

namespace {

////////////////////////////////////////////////////////////////////////////////

using NCloud::NFileStore::NMetrics::TLabel;

TFileSystemConfig MakeThrottlerConfig(
    bool throttlingEnabled,
    ui32 maxReadIops,
    ui32 maxWriteIops,
    ui32 maxReadBandwidth,
    ui32 maxWriteBandwidth,
    ui32 boostTime,
    ui32 boostRefillTime,
    ui32 boostPercentage,
    ui32 maxPostponedWeight,
    ui32 maxWriteCostMultiplier,
    ui32 maxPostponedTime,
    ui32 maxPostponedCount,
    ui32 burstPercentage,
    ui32 defaultPostponedRequestWeight)
{
    TFileSystemConfig config;
    config.PerformanceProfile.ThrottlingEnabled = throttlingEnabled;
    config.PerformanceProfile.MaxReadIops = maxReadIops;
    config.PerformanceProfile.MaxWriteIops = maxWriteIops;
    config.PerformanceProfile.MaxReadBandwidth = maxReadBandwidth;
    config.PerformanceProfile.MaxWriteBandwidth = maxWriteBandwidth;
    config.PerformanceProfile.BoostTime = boostTime;
    config.PerformanceProfile.BoostRefillTime = boostRefillTime;
    config.PerformanceProfile.BoostPercentage = boostPercentage;
    config.PerformanceProfile.MaxPostponedWeight = maxPostponedWeight;
    config.PerformanceProfile.MaxWriteCostMultiplier = maxWriteCostMultiplier;
    config.PerformanceProfile.MaxPostponedTime = maxPostponedTime;
    config.PerformanceProfile.MaxPostponedCount = maxPostponedCount;
    config.PerformanceProfile.BurstPercentage = burstPercentage;
    config.PerformanceProfile.DefaultPostponedRequestWeight =
        defaultPostponedRequestWeight;

    return config;
}

////////////////////////////////////////////////////////////////////////////////

class TTestRegistryVisitor
    : public NMetrics::IRegistryVisitor
{
private:
    struct TMetricsEntry
    {
        TInstant Time;
        NMetrics::EAggregationType AggrType;
        NMetrics::EMetricType MetrType;
        THashMap<TString, TString> Labels;
        i64 Value;

        bool Matches(const TVector<TLabel>& labels) const
        {
            for (auto& label: labels) {
                auto it = Labels.find(label.GetName());
                if (it == Labels.end() || it->second != label.GetValue()) {
                    return false;
                }
            }
            return true;
        }
    };

    TVector<TMetricsEntry> MetricsEntries;
    TMetricsEntry CurrentEntry;;

public:
    void OnStreamBegin() override
    {
        CurrentEntry = TMetricsEntry();
        MetricsEntries.clear();
    }

    void OnStreamEnd() override
    {}

    void OnMetricBegin(
        TInstant time,
        NMetrics::EAggregationType aggrType,
        NMetrics::EMetricType metrType) override
    {
        CurrentEntry.Time = time;
        CurrentEntry.AggrType = aggrType;
        CurrentEntry.MetrType = metrType;
    }

    void OnMetricEnd() override
    {
        MetricsEntries.emplace_back(std::move(CurrentEntry));
    }

    void OnLabelsBegin() override
    {}

    void OnLabelsEnd() override
    {}

    void OnLabel(TStringBuf name, TStringBuf value) override
    {
        CurrentEntry.Labels.emplace(TString(name), TString(value));
    }

    void OnValue(i64 value) override
    {
        CurrentEntry.Value = value;
    }

public:
    const TVector<TMetricsEntry>& GetEntries() const
    {
        return MetricsEntries;
    }

    void ValidateExpectedCounters(
        const TVector<std::pair<TVector<TLabel>, i64>>& expectedCounters)
    {
        for (const auto& [labels, value] : expectedCounters) {
            int matchingCountersCount = 0;
            for (const auto& entry : MetricsEntries) {
                if (entry.Matches(labels)) {
                    ++matchingCountersCount;
                    UNIT_ASSERT_EQUAL(entry.Value, value);
                }
            }
            UNIT_ASSERT_EQUAL(matchingCountersCount, 1);
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

struct TEnv
    : public NUnitTest::TBaseFixture
{
    TTestEnv Env;
    std::unique_ptr<TIndexTabletClient> Tablet;

    TTestRegistryVisitor Visitor;

    void SetUp(NUnitTest::TTestContext& /*context*/) override
    {
        Env.CreateSubDomain("nfs");

        const ui32 nodeIdx = Env.CreateNode("nfs");
        const ui64 tabletId = Env.BootIndexTablet(nodeIdx);

        Tablet = std::make_unique<TIndexTabletClient>(
            Env.GetRuntime(),
            nodeIdx,
            tabletId);
    }

    void TearDown(NUnitTest::TTestContext& /*context*/) override
    {}

};

}   // namespace

////////////////////////////////////////////////////////////////////////////////

Y_UNIT_TEST_SUITE(TIndexTabletTest_Counters)
{
    Y_UNIT_TEST_F(ShouldRegisterCounters, TEnv)
    {
        auto registry = Env.GetRegistry();

        registry->Visit(TInstant::Zero(), Visitor);
        // clang-format off
        Visitor.ValidateExpectedCounters({
            {{{"component", "storage_fs"}, {"host", "cluster"}, {"filesystem", "test"}, {"sensor", "FreshBytesCount"}}, 0},
            {{{"component", "storage_fs"}, {"host", "cluster"}, {"filesystem", "test"}, {"sensor", "GarbageQueueSize"}}, 0},
            {{{"component", "storage_fs"}, {"host", "cluster"}, {"filesystem", "test"}, {"sensor", "MixedBytesCount"}}, 0},
            {{{"component", "storage_fs"}, {"host", "cluster"}, {"filesystem", "test"}, {"sensor", "PostponedRequests"}}, 0},
            {{{"component", "storage_fs"}, {"host", "cluster"}, {"filesystem", "test"}, {"sensor", "RejectedRequests"}}, 0},
            {{{"component", "storage_fs"}, {"host", "cluster"}, {"filesystem", "test"}, {"sensor", "UsedSessionsCount"}}, 0},
            {{{"component", "storage_fs"}, {"host", "cluster"}, {"filesystem", "test"}, {"sensor", "UsedBytesCount"}}, 0},
            {{{"component", "storage_fs"}, {"host", "cluster"}, {"filesystem", "test"}, {"sensor", "UsedQuota"}}, 0},
            {{{"component", "storage"}, {"type", "hdd"}, {"sensor", "FreshBytesCount"}}, 0},
            {{{"component", "storage"}, {"type", "hdd"}, {"sensor", "GarbageQueueSize"}}, 0},
            {{{"component", "storage"}, {"type", "hdd"}, {"sensor", "MixedBytesCount"}}, 0},
            {{{"component", "storage"}, {"type", "hdd"}, {"sensor", "UsedSessionsCount"}}, 0},
            {{{"component", "storage"}, {"type", "hdd"}, {"sensor", "UsedBytesCount"}}, 0}
        });
        // clang-format on
    }

    Y_UNIT_TEST_F(ShouldCorrectlyWriteThrottlerMaxParams, TEnv)
    {
        auto registry = Env.GetRegistry();

        Tablet->AdvanceTime(TDuration::Seconds(15));
        Env.GetRuntime().DispatchEvents({}, TDuration::Seconds(5));

        registry->Visit(TInstant::Zero(), Visitor);
        Visitor.ValidateExpectedCounters({
            {{{"sensor", "MaxWriteBandwidth"}}, 4_GB - 1},
            {{{"sensor", "MaxReadIops"}}, 4_GB - 1},
            {{{"sensor", "MaxWriteIops"}}, 4_GB - 1},
            {{{"sensor", "MaxReadBandwidth"}}, 4_GB - 1}
        });
        const auto config = MakeThrottlerConfig(
            true,                                    // throttlingEnabled
            100,                                     // maxReadIops
            200,                                     // maxWriteIops
            4_KB,                                    // maxReadBandwidth
            8_KB,                                    // maxWriteBandwidth
            TDuration::Minutes(30).MilliSeconds(),   // boostTime
            TDuration::Hours(12).MilliSeconds(),     // boostRefillTime
            10,                                      // boostPercentage
            32_KB,                                   // maxPostponedWeight
            10,                                      // maxWriteCostMultiplier
            TDuration::Seconds(25).MilliSeconds(),   // maxPostponedTime
            64,                                      // maxPostponedCount
            100,                                     // burstPercentage
            1_KB                                     // defaultPostponedRequestWeight
        );
        Tablet->UpdateConfig(config);

        Tablet->AdvanceTime(TDuration::Seconds(15));
        Env.GetRuntime().DispatchEvents({}, TDuration::Seconds(5));

        registry->Visit(TInstant::Zero(), Visitor);
        Visitor.ValidateExpectedCounters({
            {{{"sensor", "MaxWriteBandwidth"}}, 8_KB},
            {{{"sensor", "MaxReadIops"}}, 100},
            {{{"sensor", "MaxWriteIops"}}, 200},
            {{{"sensor", "MaxReadBandwidth"}}, 4_KB}
        });
    }

    Y_UNIT_TEST_F(ShouldIncrementAndDecrementSessionCount, TEnv)
    {
        auto registry = Env.GetRegistry();

        registry->Visit(TInstant::Zero(), Visitor);
        Visitor.ValidateExpectedCounters({
           {{{"sensor", "UsedSessionsCount"}, {"filesystem", "test"}}, 0},
        });

        Tablet->InitSession("client", "session");

        Tablet->AdvanceTime(TDuration::Seconds(15));
        Env.GetRuntime().DispatchEvents({}, TDuration::Seconds(5));

        registry->Visit(TInstant::Zero(), Visitor);
        Visitor.ValidateExpectedCounters({
            {{{"sensor", "UsedSessionsCount"}, {"filesystem", "test"}}, 1},
        });

        Tablet->DestroySession();

        Tablet->AdvanceTime(TDuration::Seconds(15));
        Env.GetRuntime().DispatchEvents({}, TDuration::Seconds(5));

        registry->Visit(TInstant::Zero(), Visitor);
        Visitor.ValidateExpectedCounters({
            {{{"sensor", "UsedSessionsCount"}, {"filesystem", "test"}}, 0},
        });
    }

    Y_UNIT_TEST_F(ShouldCorrectlyUpdateValuesOnThrottling, TEnv)
    {
        NProto::TStorageConfig storageConfig;
        storageConfig.SetThrottlingEnabled(true);

        TTestEnv env({}, storageConfig);
        auto registry = env.GetRegistry();

        env.CreateSubDomain("nfs");
        const auto nodeIdx = env.CreateNode("nfs");
        const auto tabletId = env.BootIndexTablet(nodeIdx);

        TIndexTabletClient tablet(env.GetRuntime(), nodeIdx, tabletId);
        tablet.InitSession("client", "session");
        const auto nodeId =
            CreateNode(tablet, TCreateNodeArgs::File(RootNodeId, "test"));
        const auto handle = CreateHandle(tablet, nodeId);

        const auto config = MakeThrottlerConfig(
            true,                                    // throttlingEnabled
            2,                                       // maxReadIops
            2,                                       // maxWriteIops
            8_KB,                                    // maxReadBandwidth
            8_KB,                                    // maxWriteBandwidth
            TDuration::Minutes(30).MilliSeconds(),   // boostTime
            TDuration::Hours(12).MilliSeconds(),     // boostRefillTime
            10,                                      // boostPercentage
            32_KB,                                   // maxPostponedWeight
            10,                                      // maxWriteCostMultiplier
            TDuration::Seconds(25).MilliSeconds(),   // maxPostponedTime
            64,                                      // maxPostponedCount
            100,                                     // burstPercentage
            1_KB                                     // defaultPostponedRequestWeight
        );
        tablet.UpdateConfig(config);

        for (size_t i = 0; i < 10; ++i) {
            tablet.AdvanceTime(TDuration::Seconds(1));
            tablet.SendReadDataRequest(handle, 4_KB * i, 4_KB);
            tablet.AssertReadDataQuickResponse(S_OK);

            tablet.AdvanceTime(TDuration::Seconds(1));
            tablet.SendWriteDataRequest(
                handle,
                4_KB * (i + 1),
                4_KB,
                static_cast<char>('a' + i));
            tablet.AssertWriteDataQuickResponse(S_OK);
        }

        // 1. Testing that excess requests are postponed.
        for (size_t i = 0; i < 20; ++i) {
            tablet.SendReadDataRequest(handle, 4_KB * i, 4_KB);
            tablet.AssertReadDataNoResponse();
        }

        registry->Visit(TInstant::Zero(), Visitor);
        Visitor.ValidateExpectedCounters({
            {{{"sensor", "UsedQuota"}}, 100},
            {{{"sensor", "MaxUsedQuota"}}, 100},
            {{{"sensor", "MaxWriteBandwidth"}}, 8_KB},
            {{{"sensor", "MaxReadBandwidth"}}, 8_KB},
            {{{"sensor", "MaxWriteIops"}}, 2},
            {{{"sensor", "MaxReadIops"}}, 2},
            {{{"sensor", "PostponedRequests"}}, 20},
        });

        // Now we have 20_KB in PostponeQueue.

        for (size_t i = 0; i < 3; ++i) {
            tablet.SendWriteDataRequest(handle, 0, 4_KB, 'z');
            tablet.AssertWriteDataNoResponse();
        }

        registry->Visit(TInstant::Zero(), Visitor);
        Visitor.ValidateExpectedCounters({
            {{{"sensor", "PostponedRequests"}}, 23},
        });

        // Now we have 32_KB in PostponedQueue. Equal to MaxPostponedWeight.

        // 2. Testing that we start rejecting requests after
        // our postponed limit saturates.
        tablet.SendWriteDataRequest(handle, 0, 1, 'y');   // Event 1 byte request must be rejected.
        tablet.AssertWriteDataQuickResponse(E_FS_THROTTLED);
        tablet.SendReadDataRequest(handle, 0, 1_KB);
        tablet.AssertReadDataQuickResponse(E_FS_THROTTLED);

        registry->Visit(TInstant::Zero(), Visitor);
        Visitor.ValidateExpectedCounters({
            {{{"sensor", "RejectedRequests"}}, 2},
        });
    }
}

}   // namespace NCloud::NFileStore::NStorage
