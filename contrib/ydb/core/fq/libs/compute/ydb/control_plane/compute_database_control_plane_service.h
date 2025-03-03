#pragma once

#include <contrib/ydb/core/fq/libs/config/protos/compute.pb.h>

#include <contrib/ydb/core/fq/libs/config/protos/common.pb.h>
#include <contrib/ydb/core/fq/libs/shared_resources/shared_resources.h>
#include <contrib/ydb/core/fq/libs/signer/signer.h>

#include <contrib/ydb/library/security/ydb_credentials_provider_factory.h>
#include <contrib/ydb/library/ycloud/impl/grpc_service_settings.h>

#include <library/cpp/actors/core/actor.h>

namespace NFq {

NActors::TActorId ComputeDatabaseControlPlaneServiceActorId();

std::unique_ptr<NActors::IActor> CreateComputeDatabaseControlPlaneServiceActor(const NFq::NConfig::TComputeConfig& config,
                                                                               const NKikimr::TYdbCredentialsProviderFactory& credentialsProviderFactory,
                                                                               const NConfig::TCommonConfig& commonConfig,
                                                                               const TSigner::TPtr& signer,
                                                                               const TYqSharedResources::TPtr& yqSharedResources,
                                                                               const ::NMonitoring::TDynamicCounterPtr& counters);

std::unique_ptr<NActors::IActor> CreateYdbcpGrpcClientActor(const NCloud::TGrpcClientSettings& settings, const NYdb::TCredentialsProviderPtr& credentialsProvider);

std::unique_ptr<NActors::IActor> CreateCmsGrpcClientActor(const NCloud::TGrpcClientSettings& settings, const NYdb::TCredentialsProviderPtr& credentialsProvider);

std::unique_ptr<NActors::IActor> CreateComputeDatabasesCacheActor(const NActors::TActorId& databaseClientActorId, const TString& databasesCacheReloadPeriod, const ::NMonitoring::TDynamicCounterPtr& counters);

}
