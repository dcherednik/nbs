#pragma once

#include <contrib/ydb/library/yql/dq/actors/compute/dq_compute_actor_async_io.h>
#include <contrib/ydb/library/yql/providers/common/http_gateway/yql_http_gateway.h>
#include <contrib/ydb/library/yql/providers/s3/proto/sink.pb.h>
#include <contrib/ydb/library/yql/providers/s3/proto/retry_config.pb.h>
#include <contrib/ydb/library/yql/providers/common/token_accessor/client/factory.h>
#include <library/cpp/actors/core/actor.h>

namespace NYql::NDq {

std::pair<IDqComputeActorAsyncOutput*, NActors::IActor*> CreateS3WriteActor(
    const NKikimr::NMiniKQL::TTypeEnvironment& typeEnv,
    const NKikimr::NMiniKQL::IFunctionRegistry& functionRegistry,
    IRandomProvider*,
    IHTTPGateway::TPtr gateway,
    NS3::TSink&& params,
    ui64 outputIndex,
    TCollectStatsLevel statsLevel,
    const TTxId& txId,
    const TString& prefix,
    const THashMap<TString, TString>& secureParams,
    IDqComputeActorAsyncOutput::ICallbacks* callbacks,
    ISecuredServiceAccountCredentialsFactory::TPtr credentialsFactory,
    const IHTTPGateway::TRetryPolicy::TPtr& retryPolicy);

} // namespace NYql::NDq
