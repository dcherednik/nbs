#include "logging.h"
#include "private_events.h"
#include "stream_creator.h"
#include "target_with_stream.h"
#include "util.h"

#include <library/cpp/actors/core/actor_bootstrapped.h>
#include <library/cpp/actors/core/hfunc.h>

#include <contrib/ydb/core/base/path.h>
#include <contrib/ydb/core/tx/replication/ydb_proxy/ydb_proxy.h>

namespace NKikimr::NReplication::NController {

class TStreamCreator: public TActorBootstrapped<TStreamCreator> {
    static NYdb::NTable::TChangefeedDescription MakeChangefeed(const TString& name) {
        using namespace NYdb::NTable;
        return TChangefeedDescription(name, EChangefeedMode::Updates, EChangefeedFormat::Json)
            .WithInitialScan();
    }

    void CreateStream() {
        switch (Kind) {
        case TReplication::ETargetKind::Table:
            Send(YdbProxy, new TEvYdbProxy::TEvAlterTableRequest(SrcPath, NYdb::NTable::TAlterTableSettings()
                .AppendAddChangefeeds(Changefeed)));
            break;
        }

        Become(&TThis::StateCreateStream);
    }

    STATEFN(StateCreateStream) {
        switch (ev->GetTypeRewrite()) {
            hFunc(TEvYdbProxy::TEvAlterTableResponse, Handle);
            sFunc(TEvents::TEvWakeup, CreateStream);
        default:
            return StateBase(ev);
        }
    }

    void Handle(TEvYdbProxy::TEvAlterTableResponse::TPtr& ev) {
        LOG_T("Handle " << ev->Get()->ToString());
        auto& result = ev->Get()->Result;

        if (!result.IsSuccess()) {
            if (IsRetryableError(result)) {
                LOG_D("Retry CreateStream");
                return Schedule(TDuration::Seconds(10), new TEvents::TEvWakeup);
            }

            LOG_E("Error"
                << ": status# " << result.GetStatus()
                << ", issues# " << result.GetIssues().ToOneLineString());
            return Reply(std::move(result));
        } else {
            LOG_I("Success"
                << ": issues# " << result.GetIssues().ToOneLineString());
            return CreateConsumer();
        }
    }

    void CreateConsumer() {
        const auto streamPath = CanonizePath(ChildPath(SplitPath(SrcPath), Changefeed.GetName()));
        const auto settings = NYdb::NTopic::TAlterTopicSettings()
            .BeginAddConsumer()
                .ConsumerName(ReplicationConsumerName)
            .EndAddConsumer();

        Send(YdbProxy, new TEvYdbProxy::TEvAlterTopicRequest(streamPath, settings));
        Become(&TThis::StateCreateConsumer);
    }

    STATEFN(StateCreateConsumer) {
        switch (ev->GetTypeRewrite()) {
            hFunc(TEvYdbProxy::TEvAlterTopicResponse, Handle);
            sFunc(TEvents::TEvWakeup, CreateConsumer);
        default:
            return StateBase(ev);
        }
    }

    void Handle(TEvYdbProxy::TEvAlterTopicResponse::TPtr& ev) {
        LOG_T("Handle " << ev->Get()->ToString());
        auto& result = ev->Get()->Result;

        if (!result.IsSuccess()) {
            if (IsRetryableError(result)) {
                LOG_D("Retry CreateConsumer");
                return Schedule(TDuration::Seconds(10), new TEvents::TEvWakeup);
            }

            LOG_E("Error"
                << ": status# " << result.GetStatus()
                << ", issues# " << result.GetIssues().ToOneLineString());
        } else {
            LOG_I("Success"
                << ": issues# " << result.GetIssues().ToOneLineString());
        }

        Reply(std::move(result));
    }

    void Reply(NYdb::TStatus&& status) {
        Send(Parent, new TEvPrivate::TEvCreateStreamResult(ReplicationId, TargetId, std::move(status)));
        PassAway();
    }

public:
    static constexpr NKikimrServices::TActivity::EType ActorActivityType() {
        return NKikimrServices::TActivity::REPLICATION_CONTROLLER_STREAM_CREATOR;
    }

    explicit TStreamCreator(
            const TActorId& parent,
            const TActorId& proxy,
            ui64 rid,
            ui64 tid,
            TReplication::ETargetKind kind,
            const TString& srcPath,
            const TString& streamName)
        : Parent(parent)
        , YdbProxy(proxy)
        , ReplicationId(rid)
        , TargetId(tid)
        , Kind(kind)
        , SrcPath(srcPath)
        , Changefeed(MakeChangefeed(streamName))
        , LogPrefix("StreamCreator", ReplicationId, TargetId)
    {
    }

    void Bootstrap() {
        CreateStream();
    }

    STATEFN(StateBase) {
        switch (ev->GetTypeRewrite()) {
            sFunc(TEvents::TEvPoison, PassAway);
        }
    }

private:
    const TActorId Parent;
    const TActorId YdbProxy;
    const ui64 ReplicationId;
    const ui64 TargetId;
    const TReplication::ETargetKind Kind;
    const TString SrcPath;
    const NYdb::NTable::TChangefeedDescription Changefeed;
    const TActorLogPrefix LogPrefix;

}; // TStreamCreator

IActor* CreateStreamCreator(const TActorId& parent, const TActorId& proxy, ui64 rid, ui64 tid,
        TReplication::ETargetKind kind, const TString& srcPath, const TString& streamName)
{
    return new TStreamCreator(parent, proxy, rid, tid, kind, srcPath, streamName);
}

}
