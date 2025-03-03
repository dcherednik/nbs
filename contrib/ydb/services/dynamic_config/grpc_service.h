#pragma once

#include <library/cpp/actors/core/actorsystem.h>
#include <library/cpp/grpc/server/grpc_server.h>
#include <contrib/ydb/public/api/grpc/draft/ydb_dynamic_config_v1.grpc.pb.h>
#include <contrib/ydb/core/grpc_services/base/base_service.h>

namespace NKikimr {
namespace NGRpcService {

class TGRpcDynamicConfigService
    : public TGrpcServiceBase<Ydb::DynamicConfig::V1::DynamicConfigService>
{
public:
    using TGrpcServiceBase<Ydb::DynamicConfig::V1::DynamicConfigService>::TGrpcServiceBase;
private:
    void SetupIncomingRequests(NGrpc::TLoggerPtr logger);
};

} // namespace NGRpcService
} // namespace NKikimr
