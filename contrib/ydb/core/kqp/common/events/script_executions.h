#pragma once
#include <contrib/ydb/core/kqp/common/simple/kqp_event_ids.h>
#include <contrib/ydb/core/protos/kqp.pb.h>
#include <contrib/ydb/library/yql/public/issue/yql_issue.h>
#include <contrib/ydb/public/api/protos/ydb_operation.pb.h>
#include <contrib/ydb/public/api/protos/ydb_status_codes.pb.h>
#include <contrib/ydb/public/lib/operation_id/operation_id.h>

#include <library/cpp/actors/core/event_local.h>

#include <util/generic/maybe.h>

#include <google/protobuf/any.pb.h>

namespace NKikimr::NKqp {

enum EFinalizationStatus : i32 {
    FS_COMMIT,
    FS_ROLLBACK,
};

struct TEvForgetScriptExecutionOperation : public NActors::TEventLocal<TEvForgetScriptExecutionOperation, TKqpScriptExecutionEvents::EvForgetScriptExecutionOperation> {
    explicit TEvForgetScriptExecutionOperation(const TString& database, const NOperationId::TOperationId& id)
        : Database(database)
        , OperationId(id)
    {
    }

    TString Database;
    NOperationId::TOperationId OperationId;
};

struct TEvForgetScriptExecutionOperationResponse : public NActors::TEventLocal<TEvForgetScriptExecutionOperationResponse, TKqpScriptExecutionEvents::EvForgetScriptExecutionOperationResponse> {
    TEvForgetScriptExecutionOperationResponse(Ydb::StatusIds::StatusCode status,  NYql::TIssues issues) 
        : Status(status)
        , Issues(issues)
    {
    }
    
    Ydb::StatusIds::StatusCode Status;
    NYql::TIssues Issues;
};

struct TEvGetScriptExecutionOperation : public NActors::TEventLocal<TEvGetScriptExecutionOperation, TKqpScriptExecutionEvents::EvGetScriptExecutionOperation> {
    explicit TEvGetScriptExecutionOperation(const TString& database, const NOperationId::TOperationId& id)
        : Database(database)
        , OperationId(id)
    {
    }

    TString Database;
    NOperationId::TOperationId OperationId;
};

struct TEvGetScriptExecutionOperationQueryResponse : public NActors::TEventLocal<TEvGetScriptExecutionOperationQueryResponse, TKqpScriptExecutionEvents::EvGetScriptExecutionOperationQueryResponse> {
    TEvGetScriptExecutionOperationQueryResponse(bool ready, bool leaseExpired, std::optional<EFinalizationStatus> finalizationStatus, TActorId runScriptActorId,
        const TString& executionId, Ydb::StatusIds::StatusCode status, NYql::TIssues issues, Ydb::Query::ExecuteScriptMetadata metadata)
        : Ready(ready)
        , LeaseExpired(leaseExpired)
        , FinalizationStatus(finalizationStatus)
        , RunScriptActorId(runScriptActorId)
        , ExecutionId(executionId)
        , Status(status)
        , Issues(std::move(issues))
        , Metadata(std::move(metadata))
    {}

    bool Ready;
    bool LeaseExpired;
    std::optional<EFinalizationStatus> FinalizationStatus;
    TActorId RunScriptActorId;
    TString ExecutionId;
    Ydb::StatusIds::StatusCode Status;
    NYql::TIssues Issues;
    Ydb::Query::ExecuteScriptMetadata Metadata;
};

struct TEvGetScriptExecutionOperationResponse : public NActors::TEventLocal<TEvGetScriptExecutionOperationResponse, TKqpScriptExecutionEvents::EvGetScriptExecutionOperationResponse> {
    TEvGetScriptExecutionOperationResponse(bool ready, Ydb::StatusIds::StatusCode status, NYql::TIssues issues, TMaybe<google::protobuf::Any> metadata)
        : Ready(ready)
        , Status(status)
        , Issues(std::move(issues))
        , Metadata(std::move(metadata))
    {}

    TEvGetScriptExecutionOperationResponse(Ydb::StatusIds::StatusCode status, NYql::TIssues issues)
        : Ready(false)
        , Status(status)
        , Issues(std::move(issues))
    {}

    bool Ready;
    Ydb::StatusIds::StatusCode Status;
    NYql::TIssues Issues;
    TMaybe<google::protobuf::Any> Metadata;
};

struct TEvListScriptExecutionOperations : public NActors::TEventLocal<TEvListScriptExecutionOperations, TKqpScriptExecutionEvents::EvListScriptExecutionOperations> {
    TEvListScriptExecutionOperations(const TString& database, const ui64 pageSize, const TString& pageToken)
        : Database(database)
        , PageSize(pageSize)
        , PageToken(pageToken)
    {}

    TString Database;
    ui64 PageSize;
    TString PageToken;
};

struct TEvListScriptExecutionOperationsResponse : public NActors::TEventLocal<TEvListScriptExecutionOperationsResponse, TKqpScriptExecutionEvents::EvListScriptExecutionOperationsResponse> {
    TEvListScriptExecutionOperationsResponse(Ydb::StatusIds::StatusCode status, NYql::TIssues issues, const TString& nextPageToken, std::vector<Ydb::Operations::Operation> operations)
        : Status(status)
        , Issues(std::move(issues))
        , NextPageToken(nextPageToken)
        , Operations(std::move(operations))
    {
    }

    TEvListScriptExecutionOperationsResponse(Ydb::StatusIds::StatusCode status, NYql::TIssues issues)
        : Status(status)
        , Issues(std::move(issues))
    {
    }

    Ydb::StatusIds::StatusCode Status;
    NYql::TIssues Issues;
    TString NextPageToken;
    std::vector<Ydb::Operations::Operation> Operations;
};

struct TEvScriptLeaseUpdateResponse : public NActors::TEventLocal<TEvScriptLeaseUpdateResponse, TKqpScriptExecutionEvents::EvScriptLeaseUpdateResponse> {
    TEvScriptLeaseUpdateResponse(bool executionEntryExists, TInstant currentDeadline, Ydb::StatusIds::StatusCode status, NYql::TIssues issues)
        : ExecutionEntryExists(executionEntryExists)
        , CurrentDeadline(currentDeadline)
        , Status(status)
        , Issues(std::move(issues))
    {
    }

    bool ExecutionEntryExists;
    TInstant CurrentDeadline;
    Ydb::StatusIds::StatusCode Status;
    NYql::TIssues Issues;
};

struct TEvCheckAliveRequest : public NActors::TEventPB<TEvCheckAliveRequest, NKikimrKqp::TEvCheckAliveRequest, TKqpScriptExecutionEvents::EvCheckAliveRequest> {
};

struct TEvCheckAliveResponse : public NActors::TEventPB<TEvCheckAliveResponse, NKikimrKqp::TEvCheckAliveResponse, TKqpScriptExecutionEvents::EvCheckAliveResponse> {
};

struct TEvCancelScriptExecutionOperation : public NActors::TEventLocal<TEvCancelScriptExecutionOperation, TKqpScriptExecutionEvents::EvCancelScriptExecutionOperation> {
    explicit TEvCancelScriptExecutionOperation(const TString& database, const NOperationId::TOperationId& id)
        : Database(database)
        , OperationId(id)
    {
    }

    TString Database;
    NOperationId::TOperationId OperationId;
};

struct TEvCancelScriptExecutionOperationResponse : public NActors::TEventLocal<TEvCancelScriptExecutionOperationResponse, TKqpScriptExecutionEvents::EvCancelScriptExecutionOperationResponse> {
    TEvCancelScriptExecutionOperationResponse() = default;

    TEvCancelScriptExecutionOperationResponse(Ydb::StatusIds::StatusCode status, NYql::TIssues issues = {})
        : Status(status)
        , Issues(std::move(issues))
    {
    }

    Ydb::StatusIds::StatusCode Status;
    NYql::TIssues Issues;
};

struct TEvScriptExecutionFinished : public NActors::TEventLocal<TEvScriptExecutionFinished, TKqpScriptExecutionEvents::EvScriptExecutionFinished> {
    TEvScriptExecutionFinished(bool operationAlreadyFinalized, Ydb::StatusIds::StatusCode status, NYql::TIssues issues = {})
        : OperationAlreadyFinalized(operationAlreadyFinalized)
        , Status(status)
        , Issues(std::move(issues))
    {}

    bool OperationAlreadyFinalized;
    Ydb::StatusIds::StatusCode Status;
    NYql::TIssues Issues;
};

struct TEvSaveScriptResultMetaFinished : public NActors::TEventLocal<TEvSaveScriptResultMetaFinished, TKqpScriptExecutionEvents::EvSaveScriptResultMetaFinished> {
    TEvSaveScriptResultMetaFinished(Ydb::StatusIds::StatusCode status, NYql::TIssues issues = {})
        : Status(status)
        , Issues(std::move(issues))
    {
    }

    Ydb::StatusIds::StatusCode Status;
    NYql::TIssues Issues;
};

struct TEvSaveScriptResultFinished : public NActors::TEventLocal<TEvSaveScriptResultFinished, TKqpScriptExecutionEvents::EvSaveScriptResultFinished> {
    TEvSaveScriptResultFinished(Ydb::StatusIds::StatusCode status, NYql::TIssues issues = {})
        : Status(status)
        , Issues(std::move(issues))
    {
    }

    Ydb::StatusIds::StatusCode Status;
    NYql::TIssues Issues;
};

struct TEvFetchScriptResultsQueryResponse : public NActors::TEventLocal<TEvFetchScriptResultsQueryResponse, TKqpScriptExecutionEvents::EvFetchScriptResultsQueryResponse> {
    TEvFetchScriptResultsQueryResponse(bool truncated, NKikimrKqp::TEvFetchScriptResultsResponse&& results)
        : Truncated(truncated)
        , Results(std::move(results))
    {
    }

    bool Truncated;
    NKikimrKqp::TEvFetchScriptResultsResponse Results;
};

struct TEvSaveScriptExternalEffectRequest : public NActors::TEventLocal<TEvSaveScriptExternalEffectRequest, TKqpScriptExecutionEvents::EvSaveScriptExternalEffectRequest> {
    TEvSaveScriptExternalEffectRequest(const TString& executionId, const TString& database, const TString& customerSuppliedId, const TString& userToken)
        : ExecutionId(executionId)
        , Database(database)
        , CustomerSuppliedId(customerSuppliedId)
        , UserToken(userToken)
    {}

    TString ExecutionId;
    TString Database;

    TString CustomerSuppliedId;
    TString UserToken;
    std::vector<NKqpProto::TKqpExternalSink> Sinks;
    std::vector<TString> SecretNames;
};

struct TEvSaveScriptExternalEffectResponse : public NActors::TEventLocal<TEvSaveScriptExternalEffectResponse, TKqpScriptExecutionEvents::EvSaveScriptExternalEffectResponse> {
    TEvSaveScriptExternalEffectResponse(Ydb::StatusIds::StatusCode status, NYql::TIssues issues)
        : Status(status)
        , Issues(std::move(issues))
    {}

    Ydb::StatusIds::StatusCode Status;
    NYql::TIssues Issues;
};

struct TEvScriptFinalizeRequest : public NActors::TEventLocal<TEvScriptFinalizeRequest, TKqpScriptExecutionEvents::EvScriptFinalizeRequest> {
    TEvScriptFinalizeRequest(EFinalizationStatus finalizationStatus, const TString& executionId, const TString& database,
        Ydb::StatusIds::StatusCode operationStatus, Ydb::Query::ExecStatus execStatus, NYql::TIssues issues = {}, std::optional<NKqpProto::TKqpStatsQuery> queryStats = std::nullopt,
        std::optional<TString> queryPlan = std::nullopt, std::optional<TString> queryAst = std::nullopt, std::optional<ui64> leaseGeneration = std::nullopt)
        : FinalizationStatus(finalizationStatus)
        , ExecutionId(executionId)
        , Database(database)
        , OperationStatus(operationStatus)
        , ExecStatus(execStatus)
        , Issues(std::move(issues))
        , QueryStats(std::move(queryStats))
        , QueryPlan(std::move(queryPlan))
        , QueryAst(std::move(queryAst))
        , LeaseGeneration(leaseGeneration)
    {}

    EFinalizationStatus FinalizationStatus;
    TString ExecutionId;
    TString Database;
    Ydb::StatusIds::StatusCode OperationStatus;
    Ydb::Query::ExecStatus ExecStatus;
    NYql::TIssues Issues;
    std::optional<NKqpProto::TKqpStatsQuery> QueryStats;
    std::optional<TString> QueryPlan;
    std::optional<TString> QueryAst;
    std::optional<ui64> LeaseGeneration;
};

struct TEvScriptFinalizeResponse : public NActors::TEventLocal<TEvScriptFinalizeResponse, TKqpScriptExecutionEvents::EvScriptFinalizeResponse> {
    explicit TEvScriptFinalizeResponse(const TString& executionId)
        : ExecutionId(executionId)
    {}

    TString ExecutionId;
};

struct TEvSaveScriptFinalStatusResponse : public NActors::TEventLocal<TEvSaveScriptFinalStatusResponse, TKqpScriptExecutionEvents::EvSaveScriptFinalStatusResponse> {
    TEvSaveScriptFinalStatusResponse(const TString& customerSuppliedId, const TString& userToken)
        : CustomerSuppliedId(customerSuppliedId)
        , UserToken(userToken)
    {}

    TString CustomerSuppliedId;
    TString UserToken;
    std::vector<NKqpProto::TKqpExternalSink> Sinks;
    std::vector<TString> SecretNames;
};

struct TEvDescribeSecretsResponse : public NActors::TEventLocal<TEvDescribeSecretsResponse, TKqpScriptExecutionEvents::EvDescribeSecretsResponse> {
    struct TDescription {
        TDescription(Ydb::StatusIds::StatusCode status, NYql::TIssues issues)
            : Status(status)
            , Issues(std::move(issues))
        {}

        TDescription(const std::vector<TString>& secretValues)
            : SecretValues(secretValues)
            , Status(Ydb::StatusIds::SUCCESS)
        {}

        std::vector<TString> SecretValues;
        Ydb::StatusIds::StatusCode Status;
        NYql::TIssues Issues;
    };

    TEvDescribeSecretsResponse(const TDescription& description)
        : Description(description)
    {}

    TDescription Description;
};

} // namespace NKikimr::NKqp
