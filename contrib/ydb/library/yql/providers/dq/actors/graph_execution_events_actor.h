#pragma once

#include <library/cpp/actors/core/actor.h>
#include <contrib/ydb/library/yql/providers/dq/interface/yql_dq_task_preprocessor.h>

namespace NYql::NDqs {

NActors::IActor* MakeGraphExecutionEventsActor(const TString& traceID, std::vector<IDqTaskPreprocessor::TPtr>&& taskPreprocessors);

} // namespace NYql::NDq
