#pragma once

#include <contrib/ydb/core/kqp/common/kqp.h>
#include <contrib/ydb/core/ymq/base/counters.h>
#include <contrib/ydb/public/lib/deprecated/kicli/kicli.h>

#include <library/cpp/actors/core/actor_bootstrapped.h>
#include <library/cpp/actors/core/hfunc.h>
#include <library/cpp/actors/core/actorsystem.h>
#include <library/cpp/actors/core/log.h>


namespace NKikimr::NSQS {

class TMonitoringActor : public TActorBootstrapped<TMonitoringActor> {
private:
    enum class EState {
        LockQueue,
        GetQueue,
        RemoveData,
        Finish
    };

public:
    TMonitoringActor(TIntrusivePtr<TMonitoringCounters> counters);

    void Bootstrap(const TActorContext& ctx);

    STRICT_STFUNC(StateFunc,
        HFunc(NKqp::TEvKqp::TEvQueryResponse, HandleQueryResponse);
        IgnoreFunc(NKqp::TEvKqp::TEvCloseSessionResponse);
    )

    void RequestMetrics(TDuration runAfter, const TActorContext& ctx);

    void HandleError(const TString& error, const TActorContext& ctx);

    void HandleQueryResponse(NKqp::TEvKqp::TEvQueryResponse::TPtr& ev, const TActorContext& ctx);

private:
    TIntrusivePtr<TMonitoringCounters> Counters;
    TDuration RetryPeriod;

    TString RemovedQueuesQuery;
};

} // namespace NKikimr::NSQS
