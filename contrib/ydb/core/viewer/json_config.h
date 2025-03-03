#pragma once
#include <library/cpp/actors/core/actor_bootstrapped.h>
#include <library/cpp/actors/core/interconnect.h>
#include <library/cpp/actors/core/mon.h>
#include <contrib/ydb/core/base/hive.h>
#include <contrib/ydb/core/base/tablet_pipe.h>
#include <contrib/ydb/library/services/services.pb.h>
#include "viewer.h"

namespace NKikimr {
namespace NViewer {

using namespace NActors;

class TJsonConfig : public TActorBootstrapped<TJsonConfig> {
    using TBase = TActorBootstrapped<TJsonConfig>;
    IViewer* Viewer;
    NMon::TEvHttpInfo::TPtr Event;

public:
    static constexpr NKikimrServices::TActivity::EType ActorActivityType() {
        return NKikimrServices::TActivity::VIEWER_HANDLER;
    }

    TJsonConfig(IViewer *viewer, NMon::TEvHttpInfo::TPtr &ev)
        : Viewer(viewer)
        , Event(ev)
    {}

    void Bootstrap(const TActorContext& ctx) {
        const TKikimrRunConfig& kikimrRunConfig = Viewer->GetKikimrRunConfig();
        TStringStream json;
        auto config = kikimrRunConfig.AppConfig;
        config.MutableNameserviceConfig()->ClearClusterUUID();
        config.MutableNameserviceConfig()->ClearAcceptUUID();
        config.ClearAuthConfig();
        TProtoToJson::ProtoToJson(json, config);
        ctx.Send(Event->Sender, new NMon::TEvHttpInfoRes(Viewer->GetHTTPOKJSON(Event->Get()) + json.Str(), 0, NMon::IEvHttpInfoRes::EContentType::Custom));
        Die(ctx);
    }
};

template <>
struct TJsonRequestSchema<TJsonConfig> {
    static TString GetSchema() {
        TStringStream stream;
        TProtoToJson::ProtoToJsonSchema<NKikimrConfig::TAppConfig>(stream);
        return stream.Str();
    }
};

template <>
struct TJsonRequestSummary<TJsonConfig> {
    static TString GetSummary() {
        return "\"Configuration\"";
    }
};

template <>
struct TJsonRequestDescription<TJsonConfig> {
    static TString GetDescription() {
        return "\"Returns configuration\"";
    }
};

}
}
