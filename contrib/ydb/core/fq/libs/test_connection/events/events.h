#pragma once

#include <contrib/ydb/public/api/protos/draft/fq.pb.h>
#include <contrib/ydb/core/fq/libs/events/event_subspace.h>

#include <contrib/ydb/core/fq/libs/control_plane_config/events/events.h>
#include <contrib/ydb/core/fq/libs/control_plane_storage/events/events.h>

#include <library/cpp/actors/core/event_pb.h>
#include <library/cpp/actors/core/events.h>
#include <library/cpp/actors/interconnect/events_local.h>

#include <contrib/ydb/library/yql/public/issue/yql_issue.h>

namespace NFq {

struct TEvTestConnection {
    // Event ids.
    enum EEv : ui32 {
        EvTestConnectionRequest = YqEventSubspaceBegin(::NFq::TYqEventSubspace::TestConnection),
        EvTestConnectionResponse,
        EvEnd,
    };

    static_assert(EvEnd <= YqEventSubspaceEnd(::NFq::TYqEventSubspace::TestConnection), "All events must be in their subspace");

    struct TEvTestConnectionRequest : NActors::TEventLocal<TEvTestConnectionRequest, EvTestConnectionRequest> {
        explicit TEvTestConnectionRequest(const TString& scope,
                                          const FederatedQuery::TestConnectionRequest& request,
                                          const TString& user,
                                          const TString& token,
                                          const TString& cloudId,
                                          const TPermissions& permissions,
                                          TMaybe<TQuotaMap> quotas,
                                          TTenantInfo::TPtr tenantInfo,
                                          const FederatedQuery::Internal::ComputeDatabaseInternal& computeDatabase)
            : CloudId(cloudId)
            , Scope(scope)
            , Request(request)
            , User(user)
            , Token(token)
            , Permissions(permissions)
            , Quotas(std::move(quotas))
            , TenantInfo(tenantInfo)
            , ComputeDatabase(computeDatabase)
        {
        }

        TString CloudId;
        TString Scope;
        FederatedQuery::TestConnectionRequest Request;
        TString User;
        TString Token;
        TPermissions Permissions;
        const TMaybe<TQuotaMap> Quotas;
        TTenantInfo::TPtr TenantInfo;
        TMaybe<FederatedQuery::Internal::ComputeDatabaseInternal> ComputeDatabase;
    };

    struct TEvTestConnectionResponse : NActors::TEventLocal<TEvTestConnectionResponse, EvTestConnectionResponse> {
        explicit TEvTestConnectionResponse(const FederatedQuery::TestConnectionResult& result)
            : Result(result)
        {
        }

        explicit TEvTestConnectionResponse(const NYql::TIssues& issues)
            : Issues(issues)
        {
        }

        FederatedQuery::TestConnectionResult Result;
        NYql::TIssues Issues;
    };
};

}
