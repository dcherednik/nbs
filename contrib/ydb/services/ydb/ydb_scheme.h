#pragma once

#include <library/cpp/actors/core/actorsystem.h>
#include <library/cpp/grpc/server/grpc_server.h>
#include <contrib/ydb/public/api/grpc/ydb_scheme_v1.grpc.pb.h>
#include <contrib/ydb/core/grpc_services/base/base_service.h>

namespace NKikimr {
namespace NGRpcService {

class TGRpcYdbSchemeService
    : public TGrpcServiceBase<Ydb::Scheme::V1::SchemeService>
{
public:
    using TGrpcServiceBase<Ydb::Scheme::V1::SchemeService>::TGrpcServiceBase;

private:
    void SetupIncomingRequests(NGrpc::TLoggerPtr logger);

};

} // namespace NGRpcService
} // namespace NKikimr
