#pragma once

#include "public.h"

#include "tablet_counters.h"
#include "tablet_database.h"
#include "tablet_private.h"
#include "tablet_state.h"
#include "tablet_tx.h"

#include <cloud/filestore/libs/diagnostics/metrics/histogram.h>
#include <cloud/filestore/libs/diagnostics/metrics/window_calculator.h>
#include <cloud/filestore/libs/diagnostics/metrics/public.h>
#include <cloud/filestore/libs/diagnostics/public.h>
#include <cloud/filestore/libs/storage/api/service.h>
#include <cloud/filestore/libs/storage/api/tablet.h>
#include <cloud/filestore/libs/storage/core/config.h>
#include <cloud/filestore/libs/storage/core/tablet.h>
#include <cloud/filestore/libs/storage/core/utils.h>
#include <cloud/filestore/libs/storage/tablet/model/range.h>
#include <cloud/filestore/libs/storage/tablet/model/throttler_logger.h>
#include <cloud/filestore/libs/storage/tablet/model/verify.h>

#include <cloud/storage/core/libs/diagnostics/public.h>
#include <cloud/storage/core/libs/diagnostics/busy_idle_calculator.h>
#include <cloud/storage/core/libs/throttling/public.h>

#include <contrib/ydb/core/base/tablet_pipe.h>
#include <contrib/ydb/core/mind/local.h>
#include <contrib/ydb/core/filestore/core/filestore.h>

#include <library/cpp/actors/core/actor.h>
#include <library/cpp/actors/core/events.h>
#include <library/cpp/actors/core/hfunc.h>
#include <library/cpp/actors/core/log.h>
#include <library/cpp/actors/core/mon.h>

#include <util/generic/string.h>

#include <atomic>

namespace NCloud::NFileStore::NStorage {

////////////////////////////////////////////////////////////////////////////////

class TIndexTabletActor final
    : public NActors::TActor<TIndexTabletActor>
    , public TTabletBase<TIndexTabletActor>
    , public TIndexTabletState
{
    static constexpr size_t MaxBlobStorageBlobSize = 40 * 1024 * 1024;

    enum EState
    {
        STATE_BOOT,
        STATE_INIT,
        STATE_WORK,
        STATE_ZOMBIE,
        STATE_BROKEN,
        STATE_MAX,
    };

    struct TStateInfo
    {
        TString Name;
        NActors::IActor::TReceiveFunc Func;
    };

private:
    struct TMetrics
    {
        bool Initialized{false};

        std::atomic<i64> TotalBytesCount{0};
        std::atomic<i64> UsedBytesCount{0};

        std::atomic<i64> TotalNodesCount{0};
        std::atomic<i64> UsedNodesCount{0};

        std::atomic<i64> UsedSessionsCount{0};
        std::atomic<i64> UsedHandlesCount{0};
        std::atomic<i64> UsedLocksCount{0};

        std::atomic<i64> AllocatedCompactionRangesCount{0};
        std::atomic<i64> UsedCompactionRangesCount{0};

        std::atomic<i64> MixedBytesCount{0};
        std::atomic<i64> FreshBytesCount{0};
        std::atomic<i64> GarbageQueueSize{0};

        // Throttling
        std::atomic<i64> MaxReadBandwidth{0};
        std::atomic<i64> MaxWriteBandwidth{0};
        std::atomic<i64> MaxReadIops{0};
        std::atomic<i64> MaxWriteIops{0};
        std::atomic<i64> RejectedRequests{0};
        std::atomic<i64> PostponedRequests{0};
        std::atomic<i64> UsedQuota{0};

        // Tablet busy/idle time
        std::atomic<i64> BusyTime{0};
        std::atomic<i64> IdleTime{0};
        TBusyIdleTimeCalculatorAtomics BusyIdleCalc;

        NMetrics::TDefaultWindowCalculator MaxUsedQuota{0};
        NMetrics::THistogram<NMetrics::EHistUnit::HU_TIME_MICROSECONDS> ReadDataPostponed;
        NMetrics::THistogram<NMetrics::EHistUnit::HU_TIME_MICROSECONDS> WriteDataPostponed;

        const NMetrics::IMetricsRegistryPtr StorageRegistry;
        const NMetrics::IMetricsRegistryPtr StorageFsRegistry;

        NMetrics::IMetricsRegistryPtr FsRegistry;
        NMetrics::IMetricsRegistryPtr AggregatableFsRegistry;

        explicit TMetrics(NMetrics::IMetricsRegistryPtr metricsRegistry);

        void Register(const TString& fsId, const TString& mediaKind);
        void Update(
            const NProto::TFileSystem& fileSystem,
            const NProto::TFileSystemStats& stats,
            const NProto::TFileStorePerformanceProfile& performanceProfile,
            const TCompactionMapStats& compactionStats);
    } Metrics;

    const IProfileLogPtr ProfileLog;
    const ITraceSerializerPtr TraceSerializer;

    static const TStateInfo States[];
    EState CurrentState = STATE_BOOT;

    NKikimr::TTabletCountersWithTxTypes* Counters = nullptr;
    bool UpdateCountersScheduled = false;
    bool UpdateLeakyBucketCountersScheduled = false;
    bool CleanupSessionsScheduled = false;

    TDeque<NActors::IEventHandlePtr> WaitReadyRequests;

    TSet<NActors::TActorId> WorkerActors;

    TInstant ReassignRequestSentTs;

    TThrottlerLogger ThrottlerLogger;
    ITabletThrottlerPtr Throttler;

    TStorageConfigPtr Config;

public:
    TIndexTabletActor(
        const NActors::TActorId& owner,
        NKikimr::TTabletStorageInfoPtr storage,
        TStorageConfigPtr config,
        IProfileLogPtr profileLog,
        ITraceSerializerPtr traceSerializer,
        NMetrics::IMetricsRegistryPtr metricsRegistry);
    ~TIndexTabletActor();

    static constexpr ui32 LogComponent = TFileStoreComponents::TABLET;
    using TCounters = TIndexTabletCounters;

    static TString GetStateName(ui32 state);

    void RebootTabletOnCommitOverflow(
        const NActors::TActorContext& ctx,
        const TString& request);

private:
    void Enqueue(STFUNC_SIG) override;
    void DefaultSignalTabletActive(const NActors::TActorContext& ctx) override;
    void OnActivateExecutor(const NActors::TActorContext& ctx) override;
    bool ReassignChannelsEnabled() const override;
    void ReassignDataChannelsIfNeeded(const NActors::TActorContext& ctx);
    bool OnRenderAppHtmlPage(
        NActors::NMon::TEvRemoteHttpInfo::TPtr ev,
        const NActors::TActorContext& ctx) override;
    void OnDetach(const NActors::TActorContext& ctx) override;
    void OnTabletDead(
        NKikimr::TEvTablet::TEvTabletDead::TPtr& ev,
        const NActors::TActorContext& ctx) override;

    void Suicide(const NActors::TActorContext& ctx);
    void BecomeAux(const NActors::TActorContext& ctx, EState state);
    void ReportTabletState(const NActors::TActorContext& ctx);

    void RegisterStatCounters();
    void RegisterCounters(const NActors::TActorContext& ctx);
    void ScheduleUpdateCounters(const NActors::TActorContext& ctx);
    void UpdateCounters();
    void UpdateDelayCounter(
        TThrottlingPolicy::EOpType opType,
        TDuration time);

    void ScheduleCleanupSessions(const NActors::TActorContext& ctx);
    void RestartCheckpointDestruction(const NActors::TActorContext& ctx);

    void EnqueueWriteBatch(
        const NActors::TActorContext& ctx,
        std::unique_ptr<TWriteRequest> request);

    void EnqueueFlushIfNeeded(const NActors::TActorContext& ctx);
    void EnqueueBlobIndexOpIfNeeded(const NActors::TActorContext& ctx);
    void EnqueueCollectGarbageIfNeeded(const NActors::TActorContext& ctx);
    void EnqueueTruncateIfNeeded(const NActors::TActorContext& ctx);
    void EnqueueForcedCompactionIfNeeded(const NActors::TActorContext& ctx);

    void NotifySessionEvent(
        const NActors::TActorContext& ctx,
        const NProto::TSessionEvent& event);

    TBackpressureThresholds BuildBackpressureThresholds() const;

    void ResetThrottlingPolicy();

    void ExecuteTx_AddBlob_Write(
        const NActors::TActorContext& ctx,
        NKikimr::NTabletFlatExecutor::TTransactionContext& tx,
        TTxIndexTablet::TAddBlob& args);

    void ExecuteTx_AddBlob_Flush(
        const NActors::TActorContext& ctx,
        NKikimr::NTabletFlatExecutor::TTransactionContext& tx,
        TTxIndexTablet::TAddBlob& args);

    void ExecuteTx_AddBlob_FlushBytes(
        const NActors::TActorContext& ctx,
        NKikimr::NTabletFlatExecutor::TTransactionContext& tx,
        TTxIndexTablet::TAddBlob& args);

    void ExecuteTx_AddBlob_Compaction(
        const NActors::TActorContext& ctx,
        NKikimr::NTabletFlatExecutor::TTransactionContext& tx,
        TTxIndexTablet::TAddBlob& args);

    bool CheckSessionForDestroy(const TSession* session, ui64 seqNo);

private:
    template <typename TMethod>
    TSession* AcceptRequest(
        const typename TMethod::TRequest::TPtr& ev,
        const NActors::TActorContext& ctx,
        const std::function<NProto::TError(
            const typename TMethod::TRequest::ProtoRecordType&)>& validator = {});

    template <typename TMethod>
    void CompleteResponse(
        typename TMethod::TResponse::ProtoRecordType& response,
        const TCallContextPtr& callContext,
        const NActors::TActorContext& ctx);

    template <typename TMethod>
    NProto::TError Throttle(
        const typename TMethod::TRequest::TPtr& ev,
        const NActors::TActorContext& ctx);

    template <typename TMethod>
    bool ThrottleIfNeeded(
        const typename TMethod::TRequest::TPtr& ev,
        const NActors::TActorContext& ctx);

    void HandleWakeup(
        const NActors::TEvents::TEvWakeup::TPtr& ev,
        const NActors::TActorContext& ctx);

    void HandlePoisonPill(
        const NActors::TEvents::TEvPoisonPill::TPtr& ev,
        const NActors::TActorContext& ctx);

    void HandleHttpInfo(
        const NActors::NMon::TEvRemoteHttpInfo::TPtr& ev,
        const NActors::TActorContext& ctx);
    void HandleHttpInfo_Default(
        const NActors::TActorContext& ctx,
        const TCgiParameters& params,
        TRequestInfoPtr requestInfo);
    void HandleHttpInfo_Compaction(
        const NActors::TActorContext& ctx,
        const TCgiParameters& params,
        TRequestInfoPtr requestInfo);
    void HandleHttpInfo_DumpCompactionRange(
        const NActors::TActorContext& ctx,
        const TCgiParameters& params,
        TRequestInfoPtr requestInfo);

    void HandleSessionDisconnected(
        const NKikimr::TEvTabletPipe::TEvServerDisconnected::TPtr& ev,
        const NActors::TActorContext& ctx);

    void HandleTabletMetrics(
        const NKikimr::TEvLocal::TEvTabletMetrics::TPtr& ev,
        const NActors::TActorContext& ctx);

    void HandleUpdateConfig(
        const NKikimr::TEvFileStore::TEvUpdateConfig::TPtr& ev,
        const NActors::TActorContext& ctx);

    void HandleUpdateCounters(
        const TEvIndexTabletPrivate::TEvUpdateCounters::TPtr& ev,
        const NActors::TActorContext& ctx);

    void HandleUpdateLeakyBucketCounters(
        const TEvIndexTabletPrivate::TEvUpdateLeakyBucketCounters::TPtr& ev,
        const NActors::TActorContext& ctx);

    void HandleReadDataCompleted(
        const TEvIndexTabletPrivate::TEvReadDataCompleted::TPtr& ev,
        const NActors::TActorContext& ctx);

    void HandleWriteDataCompleted(
        const TEvIndexTabletPrivate::TEvWriteDataCompleted::TPtr& ev,
        const NActors::TActorContext& ctx);

    bool HandleRequests(STFUNC_SIG);
    bool RejectRequests(STFUNC_SIG);
    bool RejectRequestsByBrokenTablet(STFUNC_SIG);

    bool HandleCompletions(STFUNC_SIG);
    bool IgnoreCompletions(STFUNC_SIG);

    FILESTORE_TABLET_REQUESTS(FILESTORE_IMPLEMENT_REQUEST, TEvIndexTablet)
    FILESTORE_SERVICE_REQUESTS_FWD(FILESTORE_IMPLEMENT_REQUEST, TEvService)

    FILESTORE_TABLET_REQUESTS_PRIVATE_SYNC(FILESTORE_IMPLEMENT_REQUEST, TEvIndexTabletPrivate)
    FILESTORE_TABLET_REQUESTS_PRIVATE_ASYNC(FILESTORE_IMPLEMENT_ASYNC_REQUEST, TEvIndexTabletPrivate)

    FILESTORE_TABLET_TRANSACTIONS(FILESTORE_IMPLEMENT_TRANSACTION, TTxIndexTablet);

    STFUNC(StateBoot);
    STFUNC(StateInit);
    STFUNC(StateWork);
    STFUNC(StateZombie);
    STFUNC(StateBroken);

    void RegisterFileStore(const NActors::TActorContext& ctx);
    void UnregisterFileStore(const NActors::TActorContext& ctx);

    void UpdateLogTag();
};

}   // namespace NCloud::NFileStore::NStorage
