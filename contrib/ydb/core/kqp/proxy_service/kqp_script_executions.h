#pragma once
#include <contrib/ydb/core/kqp/common/events/script_executions.h>
#include <contrib/ydb/core/kqp/common/kqp.h>
#include <contrib/ydb/core/kqp/common/kqp_timeouts.h>
#include <contrib/ydb/library/yql/public/issue/yql_issue.h>

#include <library/cpp/actors/core/actor.h>
#include <library/cpp/actors/core/event_local.h>
#include <library/cpp/actors/core/events.h>

namespace NKikimr::NKqp {

// Creates all needed tables.
// Sends result event back when the work is done.
NActors::IActor* CreateScriptExecutionsTablesCreator(THolder<NActors::IEventBase> resultEvent);

// Create script execution and run it.
NActors::IActor* CreateScriptExecutionCreatorActor(TEvKqp::TEvScriptRequest::TPtr&& ev, const NKikimrConfig::TQueryServiceConfig& queryServiceConfig, TIntrusivePtr<TKqpCounters> counters, TDuration maxRunTime = SCRIPT_TIMEOUT_LIMIT);

// Operation API impl.
NActors::IActor* CreateForgetScriptExecutionOperationActor(TEvForgetScriptExecutionOperation::TPtr ev);
NActors::IActor* CreateGetScriptExecutionOperationActor(TEvGetScriptExecutionOperation::TPtr ev);
NActors::IActor* CreateListScriptExecutionOperationsActor(TEvListScriptExecutionOperations::TPtr ev);
NActors::IActor* CreateCancelScriptExecutionOperationActor(TEvCancelScriptExecutionOperation::TPtr ev);

// Updates lease deadline in database.
NActors::IActor* CreateScriptLeaseUpdateActor(const TActorId& runScriptActorId, const TString& database, const TString& executionId, TDuration leaseDuration, TIntrusivePtr<TKqpCounters> counters);

// Store and fetch results.
NActors::IActor* CreateSaveScriptExecutionResultMetaActor(const NActors::TActorId& runScriptActorId, const TString& database, const TString& executionId, const TString& serializedMeta);
NActors::IActor* CreateSaveScriptExecutionResultActor(const NActors::TActorId& runScriptActorId, const TString& database, const TString& executionId, i32 resultSetId, TMaybe<TInstant> expireAt, i64 firstRow, Ydb::ResultSet&& resultSet);
NActors::IActor* CreateGetScriptExecutionResultActor(const NActors::TActorId& replyActorId, const TString& database, const TString& executionId, i32 resultSetId, i64 offset, i64 limit);

// Compute external effects and updates status in database
NActors::IActor* CreateSaveScriptExternalEffectActor(TEvSaveScriptExternalEffectRequest::TPtr ev);
NActors::IActor* CreateSaveScriptFinalStatusActor(TEvScriptFinalizeRequest::TPtr ev);
NActors::IActor* CreateScriptFinalizationFinisherActor(const TString& executionId, const TString& database, std::optional<Ydb::StatusIds::StatusCode> operationStatus, NYql::TIssues operationIssues);

} // namespace NKikimr::NKqp
