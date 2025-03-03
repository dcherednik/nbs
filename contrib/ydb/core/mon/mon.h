#pragma once

#include <library/cpp/monlib/service/monservice.h>
#include <library/cpp/monlib/dynamic_counters/counters.h>
#include <library/cpp/monlib/service/pages/resources/css_mon_page.h>
#include <library/cpp/monlib/service/pages/resources/fonts_mon_page.h>
#include <library/cpp/monlib/service/pages/resources/js_mon_page.h>
#include <library/cpp/monlib/service/pages/tablesorter/css_mon_page.h>
#include <library/cpp/monlib/service/pages/tablesorter/js_mon_page.h>

#include <library/cpp/actors/core/actor.h>
#include <library/cpp/actors/core/mon.h>

namespace NActors {

IEventHandle* GetAuthorizeTicketHandle(const NActors::TActorId& owner, const TString& ticket);
IEventHandle* SelectAuthorizationScheme(const NActors::TActorId& owner, NMonitoring::IMonHttpRequest& request);
IEventHandle* GetAuthorizeTicketResult(const NActors::TActorId& owner);

class TActorSystem;
struct TActorId;

class TMon {
public:
    using TRequestAuthorizer = std::function<IEventHandle*(const NActors::TActorId& owner, NMonitoring::IMonHttpRequest& request)>;

    static NActors::IEventHandle* DefaultAuthorizer(const NActors::TActorId& owner, NMonitoring::IMonHttpRequest& request);

    struct TConfig {
        ui16 Port = 0;
        TString Address;
        ui32 Threads = 10;
        TString Title;
        TString Host;
        TRequestAuthorizer Authorizer = DefaultAuthorizer;
        TVector<TString> AllowedSIDs;
        TString RedirectMainPageTo;
        TString Certificate;
    };

    virtual ~TMon() = default;
    virtual void Start(TActorSystem* actorSystem = {}) = 0;
    virtual void Stop() = 0;
    virtual void Register(NMonitoring::IMonPage* page) = 0;

    virtual NMonitoring::TIndexMonPage* RegisterIndexPage(const TString& path, const TString& title) = 0;

    struct TRegisterActorPageFields {
        TString Title;
        TString RelPath;
        TActorSystem* ActorSystem;
        NMonitoring::TIndexMonPage* Index;
        bool PreTag = false;
        TActorId ActorId;
        bool UseAuth = true;
        TVector<TString> AllowedSIDs;
        bool SortPages = true;
    };

    virtual NMonitoring::IMonPage* RegisterActorPage(TRegisterActorPageFields fields) = 0;
    NMonitoring::IMonPage* RegisterActorPage(NMonitoring::TIndexMonPage* index, const TString& relPath,
        const TString& title, bool preTag, TActorSystem* actorSystem, const TActorId& actorId, bool useAuth = true, bool sortPages = true);
    virtual NMonitoring::IMonPage* RegisterCountersPage(const TString& path, const TString& title, TIntrusivePtr<::NMonitoring::TDynamicCounters> counters) = 0;
    virtual NMonitoring::IMonPage* FindPage(const TString& relPath) = 0;
    virtual void RegisterHandler(const TString& path, const TActorId& handler) = 0;
};

} // NActors
