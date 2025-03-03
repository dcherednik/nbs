#pragma once

#include <contrib/ydb/library/yql/providers/dq/interface/yql_dq_full_result_writer.h>
#include <library/cpp/actors/core/actor.h>

namespace NYql::NDqs {

THolder<NActors::IActor> MakeFullResultWriterActor(
    const TString& traceId,
    const TString& resultType,
    THolder<IDqFullResultWriter>&& writer,
    const NActors::TActorId& aggregatorId);

} // namespace NYql::NDqs
