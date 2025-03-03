#pragma once

#include <contrib/ydb/core/fq/libs/compute/common/run_actor_params.h>

#include <contrib/ydb/core/fq/libs/compute/common/metrics.h>

#include <contrib/ydb/library/yql/providers/common/metrics/service_counters.h>

#include <contrib/ydb/public/sdk/cpp/client/ydb_types/status_codes.h>

#include <library/cpp/actors/core/actor_bootstrapped.h>
#include <library/cpp/retry/retry_policy.h>

namespace NFq {

template<typename TDerived>
class TBaseComputeActor : public NActors::TActorBootstrapped<TDerived> {
public:
    using TBase = NActors::TActorBootstrapped<TDerived>;
    using TBase::PassAway;

    TBaseComputeActor(const ::NYql::NCommon::TServiceCounters& queryCounters, const TString& stepName)
        : BaseCounters(queryCounters.Counters)
        , Counters(MakeIntrusive<TComputeRequestCounters>("Total", queryCounters.Counters->GetSubgroup("step", stepName)))
        , TotalStartTime(TInstant::Now())
    {}

    TBaseComputeActor(const ::NMonitoring::TDynamicCounterPtr& baseCounters, const TString& stepName)
        : BaseCounters(baseCounters)
        , Counters(MakeIntrusive<TComputeRequestCounters>("Total", baseCounters->GetSubgroup("step", stepName)))
        , TotalStartTime(TInstant::Now())
    {}

    void Bootstrap() {
        Counters->Register();
        Counters->InFly->Inc();
        AsDerived()->Start();
    }

    TDerived* AsDerived() {
        return static_cast<TDerived*>(this);
    }

    void CompleteAndPassAway() {
        Counters->Ok->Inc();
        PassAway();
    }

    void FailedAndPassAway() {
        Counters->Error->Inc();
        PassAway();
    }

    virtual ~TBaseComputeActor() {
        Counters->InFly->Dec();
        Counters->LatencyMs->Collect((TInstant::Now() - TotalStartTime).MilliSeconds());
    }

    ::NMonitoring::TDynamicCounterPtr GetStepCountersSubgroup() const {
        return Counters->Counters;
    }

    ::NMonitoring::TDynamicCounterPtr GetBaseCounters() const {
        return BaseCounters;
    }

private:
    ::NMonitoring::TDynamicCounterPtr BaseCounters;
    TComputeRequestCountersPtr Counters;
    TInstant TotalStartTime;
};

} /* NFq */
