#pragma once

#include "events.h"
#include <contrib/ydb/core/protos/serverless_proxy_config.pb.h>

#include <library/cpp/actors/core/actor.h>

namespace NKikimr::NHttpProxy {

    struct TDiscoverySettings {
        TString DiscoveryEndpoint;
        TMaybe<TString> CaCert;
        TString Database;
    };

    NActors::IActor* CreateDiscoveryProxyActor(std::shared_ptr<NYdb::ICredentialsProvider> credentialsProvider, const NKikimrConfig::TServerlessProxyConfig& config);

} // namespace
