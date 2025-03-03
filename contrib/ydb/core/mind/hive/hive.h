#pragma once
#include <bitset>

#include <util/generic/queue.h>
#include <util/random/random.h>

#include <contrib/ydb/core/base/hive.h>
#include <contrib/ydb/core/base/statestorage.h>
#include <contrib/ydb/core/base/blobstorage.h>
#include <contrib/ydb/core/base/subdomain.h>
#include <contrib/ydb/core/base/appdata.h>
#include <contrib/ydb/core/base/tablet_pipe.h>
#include <contrib/ydb/core/mind/local.h>
#include <contrib/ydb/core/protos/counters_hive.pb.h>
#include <contrib/ydb/core/tablet/tablet_exception.h>
#include <contrib/ydb/core/tablet/tablet_responsiveness_pinger.h>
#include <contrib/ydb/core/scheme/scheme_types_defs.h>
#include <contrib/ydb/core/engine/minikql/flat_local_tx_factory.h>
#include <contrib/ydb/core/tablet/tablet_counters_aggregator.h>
#include <contrib/ydb/core/tablet/tablet_counters_protobuf.h>
#include <contrib/ydb/core/tablet/tablet_pipe_client_cache.h>
#include <contrib/ydb/core/tablet/pipe_tracker.h>
#include <contrib/ydb/core/tablet/tablet_impl.h>

#include <contrib/ydb/core/tablet_flat/flat_executor_counters.h>

#include <library/cpp/actors/core/interconnect.h>
#include <library/cpp/actors/core/hfunc.h>

#include <contrib/ydb/core/tablet/tablet_metrics.h>

#include <util/stream/format.h>

namespace NKikimr {
namespace NHive {

using NTabletFlatExecutor::TTabletExecutedFlat;
using NTabletFlatExecutor::TTransactionContext;
using NTabletFlatExecutor::TExecutorCounters;

using TTabletId = ui64;
using TTabletCategoryId = ui64;
using TNodeId = ui32;
using TDataCenterId = TString;
using TFollowerId = ui32;
using TFollowerGroupId = ui32;
using TStorageGroupId = ui32;
using TFullTabletId = std::pair<TTabletId, TFollowerId>;
using TObjectId = ui64; // schema object id, used to organize tablets of the same schema object
using TOwnerId = ui64;
using TFullObjectId = std::pair<TOwnerId, TObjectId>;
using TResourceRawValues = std::tuple<i64, i64, i64, i64>; // CPU, Memory, Network, Counter
using TResourceNormalizedValues = std::tuple<double, double, double, double>;
using TOwnerIdxType = NScheme::TPairUi64Ui64;

static constexpr std::size_t MAX_TABLET_CHANNELS = 256;

enum class ETabletState : ui64 {
    Unknown = 0,
    GroupAssignment = 50,
    StoppingInGroupAssignment = 98,
    Stopping = 99,
    Stopped = 100,
    ReadyToWork = 200,
    BlockStorage,   // blob storage block request for previous group of 0 channel
    Deleting,
};

TString ETabletStateName(ETabletState value);

enum class EFollowerStrategy : ui32 {
    Unknown,
    Backup,
    Read,
};

TString EFollowerStrategyName(EFollowerStrategy value);

enum class EBalancerType {
    Manual,
    Scatter,
    ScatterCounter,
    ScatterCPU,
    ScatterMemory,
    ScatterNetwork,
    Emergency,
    SpreadNeighbours,

    Last = SpreadNeighbours,
};

constexpr std::size_t EBalancerTypeSize = static_cast<std::size_t>(EBalancerType::Last) + 1;

TString EBalancerTypeName(EBalancerType value);

enum class EResourceToBalance {
    Dominant,
    Counter,
    CPU,
    Memory,
    Network,
};

EResourceToBalance ToResourceToBalance(NMetrics::EResource resource);

struct ISubActor {
    virtual void Cleanup() = 0;
};


struct TCompleteNotifications {
    TVector<std::pair<THolder<IEventHandle>, TDuration>> Notifications;
    TActorId SelfID;

    void Reset(const TActorId& selfId) {
        Notifications.clear();
        SelfID = selfId;
    }

    void Send(const TActorId& recipient, IEventBase* ev, ui32 flags = 0, ui64 cookie = 0) {
        Y_ABORT_UNLESS(!!SelfID);
        Notifications.emplace_back(new IEventHandle(recipient, SelfID, ev, flags, cookie), TDuration());
    }

    void Schedule(TDuration duration, IEventBase* ev, ui32 flags = 0, ui64 cookie = 0) {
        Y_ABORT_UNLESS(!!SelfID);
        Notifications.emplace_back(new IEventHandle(SelfID, {}, ev, flags, cookie), duration);
    }

    size_t size() const {
        return Notifications.size();
    }

    void Send(const TActorContext& ctx) {
        for (auto& [notification, duration] : Notifications) {
            if (duration) {
                ctx.ExecutorThread.Schedule(duration, notification.Release());
            } else {
                ctx.ExecutorThread.Send(notification.Release());
            }
        }
        Notifications.clear();
    }
};

struct TCompleteActions {
    std::vector<std::unique_ptr<IActor>> Actors;
    std::vector<std::function<void()>> Callbacks;

    void Reset() {
        Actors.clear();
        Callbacks.clear();
    }

    void Register(IActor* actor) {
        Actors.emplace_back(actor);
    }

    void Callback(std::function<void()> callback) {
        Callbacks.emplace_back(std::move(callback));
    }

    void Run(const TActorContext& ctx) {
        for (auto& callback : Callbacks) {
            callback();
        }
        Callbacks.clear();
        for (auto& actor : Actors) {
            ctx.Register(actor.release());
        }
        Actors.clear();
    }
};

struct TSideEffects : TCompleteNotifications, TCompleteActions {
    void Reset(const TActorId& selfId) {
        TCompleteActions::Reset();
        TCompleteNotifications::Reset(selfId);
    }

    void Complete(const TActorContext& ctx) {
        TCompleteActions::Run(ctx);
        TCompleteNotifications::Send(ctx);
    }
};

TResourceNormalizedValues NormalizeRawValues(const TResourceRawValues& values, const TResourceRawValues& maximum);
NMetrics::EResource GetDominantResourceType(const TResourceRawValues& values, const TResourceRawValues& maximum);
NMetrics::EResource GetDominantResourceType(const TResourceNormalizedValues& normValues);

template <typename... ResourceTypes>
inline std::tuple<ResourceTypes...> GetStDev(const TVector<std::tuple<ResourceTypes...>>& values) {
    std::tuple<ResourceTypes...> sum;
    if (values.empty())
        return sum;
    for (const auto& v : values) {
        sum = sum + v;
    }
    auto mean = sum / values.size();
    sum = std::tuple<ResourceTypes...>();
    for (const auto& v : values) {
        auto diff = v - mean;
        sum = sum + diff * diff;
    }
    auto div = sum / values.size();
    auto st_dev = sqrt(div);
    return tuple_cast<ResourceTypes...>::cast(st_dev);
}

class THive;

struct THiveSharedSettings {
    NKikimrConfig::THiveConfig CurrentConfig;

    NKikimrConfig::THiveConfig::EHiveStorageBalanceStrategy GetStorageBalanceStrategy() const {
        return CurrentConfig.GetStorageBalanceStrategy();
    }

    NKikimrConfig::THiveConfig::EHiveStorageSelectStrategy GetStorageSelectStrategy() const {
        return CurrentConfig.GetStorageSelectStrategy();
    }

    NKikimrConfig::THiveConfig::EHiveTabletBalanceStrategy GetTabletBalanceStrategy() const {
        return CurrentConfig.GetTabletBalanceStrategy();
    }

    NKikimrConfig::THiveConfig::EHiveNodeBalanceStrategy GetNodeBalanceStrategy() const {
        return CurrentConfig.GetNodeBalanceStrategy();
    }

    NKikimrConfig::THiveConfig::EHiveNodeSelectStrategy GetNodeSelectStrategy() const {
        return CurrentConfig.GetNodeSelectStrategy();
    }

    double GetStorageOvercommit() const {
        return CurrentConfig.GetStorageOvercommit();
    }

    bool GetStorageSafeMode() const {
        return CurrentConfig.GetStorageSafeMode();
    }

    TDuration GetStoragePoolFreshPeriod() const {
        return TDuration::MilliSeconds(CurrentConfig.GetStoragePoolFreshPeriod());
    }
};

struct TDrainSettings {
    bool Persist = true;
    bool KeepDown = false;
    ui32 DrainInFlight = 0;
};

struct TBalancerSettings {
    EBalancerType Type = EBalancerType::Manual;
    int MaxMovements = 0;
    bool RecheckOnFinish = false;
    ui64 MaxInFlight = 1;
    const std::vector<TNodeId> FilterNodeIds = {};
    EResourceToBalance ResourceToBalance = EResourceToBalance::Dominant;
    std::optional<TFullObjectId> FilterObjectId;
};

struct TBalancerStats {
    ui64 TotalRuns = 0;
    ui64 TotalMovements = 0;
    bool IsRunningNow = false;
    ui64 CurrentMovements = 0;
    ui64 CurrentMaxMovements = 0;
    TInstant LastRunTimestamp;
    ui64 LastRunMovements = 0;
};

struct TNodeFilter {
    TVector<TSubDomainKey> AllowedDomains;
    TVector<TNodeId> AllowedNodes;
    TVector<TDataCenterId> AllowedDataCenters;
};

} // NHive
} // NKikimr

template <>
inline void Out<NKikimr::NHive::TCompleteNotifications>(IOutputStream& o, const NKikimr::NHive::TCompleteNotifications& n) {
    if (!n.Notifications.empty()) {
        o << "Notifications: ";
        for (auto it = n.Notifications.begin(); it != n.Notifications.end(); ++it) {
            if (it != n.Notifications.begin()) {
                o << ',';
            }
            o << Hex(it->first->Type) << " " << it->first.Get()->Recipient;
        }
    }
}

template <>
inline void Out<NKikimr::NHive::TCompleteActions>(IOutputStream& o, const NKikimr::NHive::TCompleteActions& n) {
    if (!n.Callbacks.empty()) {
        o << "Callbacks: " << n.Callbacks.size();
    }
    if (!n.Actors.empty()) {
        if (!n.Callbacks.empty()) {
            o << ' ';
        }
        o << "Actions: ";
        for (auto it = n.Actors.begin(); it != n.Actors.end(); ++it) {
            if (it != n.Actors.begin()) {
                o << '.';
            }
            o << TypeName(*(it->get()));
        }
    }
}

template <>
inline void Out<NKikimr::NHive::TSideEffects>(IOutputStream& o, const NKikimr::NHive::TSideEffects& e) {
    o << '{';
    o << static_cast<const NKikimr::NHive::TCompleteNotifications&>(e);
    if (!e.Notifications.empty() && !e.Actors.empty()) {
        o << ' ';
    }
    o << static_cast<const NKikimr::NHive::TCompleteActions&>(e);
    o << '}';
}
