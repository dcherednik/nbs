#pragma once

#include <contrib/ydb/core/fq/libs/config/protos/checkpoint_coordinator.pb.h>
#include <contrib/ydb/core/fq/libs/config/protos/common.pb.h>
#include <contrib/ydb/core/fq/libs/shared_resources/shared_resources.h>

#include <contrib/ydb/library/security/ydb_credentials_provider_factory.h>

#include <library/cpp/actors/core/actor.h>

#include <memory>

namespace NFq {

std::unique_ptr<NActors::IActor> NewCheckpointStorageService(
    const NConfig::TCheckpointCoordinatorConfig& config,
    const NConfig::TCommonConfig& commonConfig,
    const NKikimr::TYdbCredentialsProviderFactory& credentialsProviderFactory,
    const TYqSharedResources::TPtr& yqSharedResources);

} // namespace NFq
