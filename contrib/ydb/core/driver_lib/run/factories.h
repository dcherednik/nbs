#pragma once
#include <contrib/ydb/core/blobstorage/pdisk/blobstorage_pdisk_util_devicemode.h>
#include <contrib/ydb/core/kqp/common/kqp.h>
#include <contrib/ydb/core/tx/datashard/export_iface.h>
#include <contrib/ydb/core/persqueue/actor_persqueue_client_iface.h>
#include <contrib/ydb/core/protos/auth.pb.h>
#include <contrib/ydb/core/base/grpc_service_factory.h>

#include <contrib/ydb/core/ymq/actor/auth_factory.h>
#include <contrib/ydb/core/http_proxy/auth_factory.h>

#include <contrib/ydb/library/folder_service/folder_service.h>
#include <contrib/ydb/library/folder_service/proto/config.pb.h>
#include <contrib/ydb/library/pdisk_io/aio.h>
#include <contrib/ydb/core/fq/libs/config/protos/audit.pb.h>

#include <contrib/ydb/library/yql/minikql/computation/mkql_computation_node.h>
#include <contrib/ydb/library/yql/providers/pq/cm_client/client.h>

#include <library/cpp/actors/core/actorsystem.h>

#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace NKikimr {

// A way to parameterize YDB binary, we do it via a set of factories
struct TModuleFactories {
    // A backend factory for Query Replay
    std::shared_ptr<NKqp::IQueryReplayBackendFactory> QueryReplayBackendFactory;
    //
    std::shared_ptr<NMsgBusProxy::IPersQueueGetReadSessionsInfoWorkerFactory> PQReadSessionsInfoWorkerFactory;
    // Can be nullptr. In that case there would be no ability to work with internal configuration manager.
    NPq::NConfigurationManager::IConnections::TPtr PqCmConnections;
    // Export implementation for Data Shards
    std::shared_ptr<NDataShard::IExportFactory> DataShardExportFactory;
    // Factory for Simple queue services implementation details
    std::shared_ptr<NSQS::IEventsWriterFactory> SqsEventsWriterFactory;

    IActor*(*CreateTicketParser)(const NKikimrProto::TAuthConfig&);
    IActor*(*FolderServiceFactory)(const NKikimrProto::NFolderService::TFolderServiceConfig&);

    // Factory for grpc services
    TGrpcServiceFactory GrpcServiceFactory;

    std::shared_ptr<NPQ::IPersQueueMirrorReaderFactory> PersQueueMirrorReaderFactory;
    /// Factory for pdisk's aio engines
    std::shared_ptr<NPDisk::IIoContextFactory> IoContextFactory;

    std::function<NActors::TMon* (NActors::TMon::TConfig, const NKikimrConfig::TAppConfig& appConfig)> MonitoringFactory;
    std::shared_ptr<NSQS::IAuthFactory> SqsAuthFactory;

    std::shared_ptr<NHttpProxy::IAuthFactory> DataStreamsAuthFactory;
    std::vector<NKikimr::NMiniKQL::TComputationNodeFactory> AdditionalComputationNodeFactories;

    ~TModuleFactories();
};

} // NKikimr
