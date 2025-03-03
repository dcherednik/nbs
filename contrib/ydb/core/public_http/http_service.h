#pragma once

#include <contrib/ydb/core/base/appdata.h>
#include <contrib/ydb/core/protos/config.pb.h>
#include <library/cpp/actors/core/actor.h>

namespace NKikimr::NPublicHttp {
    inline NActors::TActorId MakePublicHttpServerID() {
        static const char x[12] = "pub_http_sr";
        return NActors::TActorId(0, TStringBuf(x, 12));
    }

    inline NActors::TActorId MakePublicHttpID() {
        static const char x[12] = "public_http";
        return NActors::TActorId(0, TStringBuf(x, 12));
    }

    void Initialize(NActors::TActorSystemSetup::TLocalServices& localServices, const TAppData& appData, const NKikimrConfig::THttpProxyConfig& config);

} // namespace NKikimr::NPublicHttp
