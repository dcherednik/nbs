#pragma once

#include <contrib/ydb/core/fq/libs/compute/common/run_actor_params.h>

#include <contrib/ydb/library/yql/providers/common/metrics/service_counters.h>

#include <contrib/ydb/public/sdk/cpp/client/ydb_query/query.h>

#include <library/cpp/actors/core/actor.h>

namespace NFq {

std::unique_ptr<NActors::IActor> CreateFinalizerActor(const TRunActorParams& params,
                                                      const NActors::TActorId& parent,
                                                      const NActors::TActorId& pinger,
                                                      NYdb::NQuery::EExecStatus execStatus,
                                                      FederatedQuery::QueryMeta::ComputeStatus status,
                                                      const ::NYql::NCommon::TServiceCounters& queryCounters);

}
