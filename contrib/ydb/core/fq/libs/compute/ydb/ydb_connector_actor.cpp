#include <contrib/ydb/core/fq/libs/common/util.h>
#include <contrib/ydb/core/fq/libs/compute/common/run_actor_params.h>
#include <contrib/ydb/core/fq/libs/compute/ydb/events/events.h>
#include <contrib/ydb/core/fq/libs/ydb/ydb.h>

#include <contrib/ydb/public/sdk/cpp/client/ydb_query/client.h>
#include <contrib/ydb/public/sdk/cpp/client/ydb_operation/operation.h>

#include <contrib/ydb/library/yql/public/issue/yql_issue_message.h>

#include <library/cpp/actors/core/actor.h>
#include <library/cpp/actors/core/actor_bootstrapped.h>
#include <library/cpp/actors/core/actorsystem.h>
#include <library/cpp/actors/core/hfunc.h>

namespace NFq {

using namespace NActors;
using namespace NFq;

class TYdbConnectorActor : public NActors::TActorBootstrapped<TYdbConnectorActor> {
public:
    explicit TYdbConnectorActor(const TRunActorParams& params)
        : YqSharedResources(params.YqSharedResources)
        , CredentialsProviderFactory(params.CredentialsProviderFactory)
        , ComputeConnection(params.ComputeConnection)
    {
        StatsMode = params.Config.GetControlPlaneStorage().GetStatsMode();
        if (StatsMode == Ydb::Query::StatsMode::STATS_MODE_UNSPECIFIED) {
            StatsMode = Ydb::Query::StatsMode::STATS_MODE_FULL;
        }
    }

    void Bootstrap() {
        auto querySettings = NFq::GetClientSettings<NYdb::NQuery::TClientSettings>(ComputeConnection, CredentialsProviderFactory);
        QueryClient = std::make_unique<NYdb::NQuery::TQueryClient>(YqSharedResources->UserSpaceYdbDriver, querySettings);
        auto operationSettings = NFq::GetClientSettings<NYdb::TCommonClientSettings>(ComputeConnection, CredentialsProviderFactory);
        OperationClient = std::make_unique<NYdb::NOperation::TOperationClient>(YqSharedResources->UserSpaceYdbDriver, operationSettings);
        Become(&TYdbConnectorActor::StateFunc);
    }

    STRICT_STFUNC(StateFunc,
        hFunc(TEvYdbCompute::TEvExecuteScriptRequest, Handle);
        hFunc(TEvYdbCompute::TEvGetOperationRequest, Handle);
        hFunc(TEvYdbCompute::TEvFetchScriptResultRequest, Handle);
        hFunc(TEvYdbCompute::TEvCancelOperationRequest, Handle);
        hFunc(TEvYdbCompute::TEvForgetOperationRequest, Handle);
        cFunc(NActors::TEvents::TEvPoisonPill::EventType, PassAway);
    )

    void Handle(const TEvYdbCompute::TEvExecuteScriptRequest::TPtr& ev) {
        const auto& event = *ev->Get();
        NYdb::NQuery::TExecuteScriptSettings settings;
        settings.ResultsTtl(event.ResultTtl);
        settings.OperationTimeout(event.OperationTimeout);
        settings.Syntax(event.Syntax);
        settings.ExecMode(event.ExecMode);
        settings.StatsMode(StatsMode);
        settings.TraceId(event.TraceId);
        QueryClient
            ->ExecuteScript(event.Sql, settings)
            .Apply([actorSystem = NActors::TActivationContext::ActorSystem(), recipient = ev->Sender, cookie = ev->Cookie, database = ComputeConnection.database()](auto future) {
                try {
                    auto response = future.ExtractValueSync();
                    if (response.Status().IsSuccess()) {
                        actorSystem->Send(recipient, new TEvYdbCompute::TEvExecuteScriptResponse(response.Id(), response.Metadata().ExecutionId), 0, cookie);
                    } else {
                        actorSystem->Send(
                            recipient,
                            MakeResponse<TEvYdbCompute::TEvExecuteScriptResponse>(
                                response.Status().GetIssues(),
                                response.Status().GetStatus(), database),
                            0, cookie);
                    }
                } catch (...) {
                    actorSystem->Send(
                        recipient,
                        MakeResponse<TEvYdbCompute::TEvExecuteScriptResponse>(
                            CurrentExceptionMessage(),
                            NYdb::EStatus::GENERIC_ERROR, database),
                        0, cookie);
                }
            });
    }

    void Handle(const TEvYdbCompute::TEvGetOperationRequest::TPtr& ev) {
        OperationClient
            ->Get<NYdb::NQuery::TScriptExecutionOperation>(ev->Get()->OperationId)
            .Apply([actorSystem = NActors::TActivationContext::ActorSystem(), recipient = ev->Sender, cookie = ev->Cookie, database = ComputeConnection.database()](auto future) {
                try {
                    auto response = future.ExtractValueSync();
                    if (response.Id().GetKind() != Ydb::TOperationId::UNUSED) {
                        actorSystem->Send(recipient, new TEvYdbCompute::TEvGetOperationResponse(response.Metadata().ExecStatus, static_cast<Ydb::StatusIds::StatusCode>(response.Status().GetStatus()), response.Metadata().ResultSetsMeta, response.Metadata().ExecStats, RemoveDatabaseFromIssues(response.Status().GetIssues(), database)), 0, cookie);
                    } else {
                        actorSystem->Send(
                            recipient,
                            MakeResponse<TEvYdbCompute::TEvGetOperationResponse>(
                                response.Status().GetIssues(),
                                response.Status().GetStatus(), database),
                            0, cookie);
                    }
                } catch (...) {
                    actorSystem->Send(
                        recipient,
                        MakeResponse<TEvYdbCompute::TEvGetOperationResponse>(
                            CurrentExceptionMessage(),
                            NYdb::EStatus::GENERIC_ERROR, database),
                        0, cookie);
                }
            });
    }

    void Handle(const TEvYdbCompute::TEvFetchScriptResultRequest::TPtr& ev) {
        NYdb::NQuery::TFetchScriptResultsSettings settings;
        settings.FetchToken(ev->Get()->FetchToken);
        QueryClient
            ->FetchScriptResults(ev->Get()->OperationId, ev->Get()->ResultSetId, settings)
            .Apply([actorSystem = NActors::TActivationContext::ActorSystem(), recipient = ev->Sender, cookie = ev->Cookie, database = ComputeConnection.database()](auto future) {
                try {
                    auto response = future.ExtractValueSync();
                    if (response.IsSuccess()) {
                        actorSystem->Send(recipient, new TEvYdbCompute::TEvFetchScriptResultResponse(response.ExtractResultSet(), response.GetNextFetchToken()), 0, cookie);
                    } else {
                        actorSystem->Send(
                            recipient,
                            MakeResponse<TEvYdbCompute::TEvFetchScriptResultResponse>(
                                response.GetIssues(),
                                response.GetStatus(), database),
                            0, cookie);
                    }
                } catch (...) {
                    actorSystem->Send(
                        recipient,
                        MakeResponse<TEvYdbCompute::TEvFetchScriptResultResponse>(
                            CurrentExceptionMessage(),
                            NYdb::EStatus::GENERIC_ERROR, database),
                        0, cookie);
                }
            });
    }

    void Handle(const TEvYdbCompute::TEvCancelOperationRequest::TPtr& ev) {
        OperationClient
            ->Cancel(ev->Get()->OperationId)
            .Apply([actorSystem = NActors::TActivationContext::ActorSystem(), recipient = ev->Sender, cookie = ev->Cookie, database = ComputeConnection.database()](auto future) {
                try {
                    auto response = future.ExtractValueSync();
                    actorSystem->Send(
                        recipient,
                        MakeResponse<TEvYdbCompute::TEvCancelOperationResponse>(
                            response.GetIssues(),
                            response.GetStatus(), database),
                        0, cookie);
                } catch (...) {
                    actorSystem->Send(
                        recipient,
                        MakeResponse<TEvYdbCompute::TEvCancelOperationResponse>(
                            CurrentExceptionMessage(),
                            NYdb::EStatus::GENERIC_ERROR, database),
                        0, cookie);
                }
            });
    }

    void Handle(const TEvYdbCompute::TEvForgetOperationRequest::TPtr& ev) {
        OperationClient
            ->Forget(ev->Get()->OperationId)
            .Apply([actorSystem = NActors::TActivationContext::ActorSystem(), recipient = ev->Sender, cookie = ev->Cookie, database = ComputeConnection.database()](auto future) {
                try {
                    auto response = future.ExtractValueSync();
                    actorSystem->Send(
                        recipient,
                        MakeResponse<TEvYdbCompute::TEvForgetOperationResponse>(
                            response.GetIssues(),
                            response.GetStatus(), database),
                        0, cookie);
                } catch (...) {
                    actorSystem->Send(
                        recipient,
                        MakeResponse<TEvYdbCompute::TEvForgetOperationResponse>(
                            CurrentExceptionMessage(),
                            NYdb::EStatus::GENERIC_ERROR, database),
                        0, cookie);
                }
            });
    }
    
    template<typename TResponse>
    static TResponse* MakeResponse(TString msg, NYdb::EStatus status, TString databasePath) {
        return new TResponse(NYql::TIssues{NYql::TIssue{RemoveDatabaseFromStr(msg, databasePath)}}, status);
    }

    template<typename TResponse>
    static TResponse* MakeResponse(const NYql::TIssues& issues, NYdb::EStatus status, TString databasePath) {
        return new TResponse(RemoveDatabaseFromIssues(issues, databasePath), status);
    }

private:
    TYqSharedResources::TPtr YqSharedResources;
    NKikimr::TYdbCredentialsProviderFactory CredentialsProviderFactory;
    NConfig::TYdbStorageConfig ComputeConnection;
    std::unique_ptr<NYdb::NQuery::TQueryClient> QueryClient;
    std::unique_ptr<NYdb::NOperation::TOperationClient> OperationClient;
    Ydb::Query::StatsMode StatsMode;
};

std::unique_ptr<NActors::IActor> CreateConnectorActor(const TRunActorParams& params) {
    return std::make_unique<TYdbConnectorActor>(params);
}

}
