#pragma once

#include <contrib/ydb/library/yql/providers/dq/common/yql_dq_settings.h>

#include <library/cpp/actors/core/actor.h>

#include <contrib/ydb/library/yql/providers/common/metrics/service_counters.h>

namespace NYql {

THolder<NActors::IActor> MakeTaskController(
    const TString& traceId,
    const NActors::TActorId& executerId,
    const NActors::TActorId& resultId,
    const TDqConfiguration::TPtr& settings,
    const ::NYql::NCommon::TServiceCounters& serviceCounters,
    const TDuration& pingPeriod = TDuration::Zero(),
    const TDuration& aggrPeriod = TDuration::Seconds(1));

} // namespace NYql
