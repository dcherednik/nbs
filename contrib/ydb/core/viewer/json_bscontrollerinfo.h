#pragma once
#include <library/cpp/actors/core/actor_bootstrapped.h>
#include <library/cpp/actors/core/interconnect.h>
#include <library/cpp/actors/core/mon.h>
#include <contrib/ydb/core/base/tablet_pipe.h>
#include <contrib/ydb/library/services/services.pb.h>
#include "viewer.h"
#include "json_pipe_req.h"
#include <contrib/ydb/core/viewer/json/json.h>

namespace NKikimr {
namespace NViewer {

using namespace NActors;

class TJsonBSControllerInfo : public TViewerPipeClient<TJsonBSControllerInfo> {
    using TBase = TViewerPipeClient<TJsonBSControllerInfo>;
    IViewer* Viewer;
    NMon::TEvHttpInfo::TPtr Event;
    TAutoPtr<TEvBlobStorage::TEvResponseControllerInfo> ControllerInfo;
    TJsonSettings JsonSettings;
    ui32 Timeout = 0;

public:
    static constexpr NKikimrServices::TActivity::EType ActorActivityType() {
        return NKikimrServices::TActivity::VIEWER_HANDLER;
    }

    TJsonBSControllerInfo(IViewer* viewer, NMon::TEvHttpInfo::TPtr &ev)
        : Viewer(viewer)
        , Event(ev)
    {}

    void Bootstrap(const TActorContext& ctx) {
        const auto& params(Event->Get()->Request.GetParams());
        JsonSettings.EnumAsNumbers = !FromStringWithDefault<bool>(params.Get("enums"), false);
        JsonSettings.UI64AsString = !FromStringWithDefault<bool>(params.Get("ui64"), false);
        Timeout = FromStringWithDefault<ui32>(params.Get("timeout"), 10000);
        InitConfig(params);
        RequestBSControllerInfo();
        Become(&TThis::StateRequestedInfo, ctx, TDuration::MilliSeconds(Timeout), new TEvents::TEvWakeup());
    }

    STATEFN(StateRequestedInfo) {
        switch (ev->GetTypeRewrite()) {
            hFunc(TEvBlobStorage::TEvResponseControllerInfo, Handle);
            hFunc(TEvTabletPipe::TEvClientConnected, TBase::Handle);
            cFunc(TEvents::TSystem::Wakeup, HandleTimeout);
        }
    }

    void Handle(TEvBlobStorage::TEvResponseControllerInfo::TPtr& ev) {
        ControllerInfo = ev->Release();
        RequestDone();
    }

    void ReplyAndPassAway() {
        TStringStream json;
        if (ControllerInfo != nullptr) {
            TProtoToJson::ProtoToJson(json, ControllerInfo->Record);
        } else {
            json << "null";
        }
        Send(Event->Sender, new NMon::TEvHttpInfoRes(Viewer->GetHTTPOKJSON(Event->Get()) + json.Str(), 0, NMon::IEvHttpInfoRes::EContentType::Custom));
        PassAway();
    }

    void HandleTimeout() {
        Send(Event->Sender, new NMon::TEvHttpInfoRes(Viewer->GetHTTPGATEWAYTIMEOUT(Event->Get()), 0, NMon::IEvHttpInfoRes::EContentType::Custom));
        PassAway();
    }
};

template <>
struct TJsonRequestSchema<TJsonBSControllerInfo> {
    static TString GetSchema() {
        TStringStream stream;
        TProtoToJson::ProtoToJsonSchema<TEvBlobStorage::TEvResponseControllerInfo::ProtoRecordType>(stream);
        return stream.Str();
    }
};

template <>
struct TJsonRequestParameters<TJsonBSControllerInfo> {
    static TString GetParameters() {
        return R"___([{"name":"controller_id","in":"query","description":"storage controller identifier (tablet id)","required":true,"type":"string"},
                      {"name":"enums","in":"query","description":"convert enums to strings","required":false,"type":"boolean"},
                      {"name":"ui64","in":"query","description":"return ui64 as number","required":false,"type":"boolean"},
                      {"name":"timeout","in":"query","description":"timeout in ms","required":false,"type":"integer"}])___";
    }
};

template <>
struct TJsonRequestSummary<TJsonBSControllerInfo> {
    static TString GetSummary() {
        return "\"Storage controller information\"";
    }
};

template <>
struct TJsonRequestDescription<TJsonBSControllerInfo> {
    static TString GetDescription() {
        return "\"Returns information about storage controller\"";
    }
};

}
}
