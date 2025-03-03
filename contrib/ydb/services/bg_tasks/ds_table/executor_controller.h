#pragma once
#include "config.h"

#include <contrib/ydb/library/aclib/aclib.h>
#include <contrib/ydb/services/metadata/initializer/common.h>

#include <library/cpp/actors/core/actor.h>
#include <library/cpp/actors/core/actorid.h>

namespace NKikimr::NBackgroundTasks {

class TTask;

class TExecutorController: public NMetadata::NInitializer::IInitializerOutput {
private:
    const NActors::TActorIdentity ExecutorActorId;
    YDB_READONLY_DEF(TConfig, Config);
    const NACLib::TUserToken UserToken;
public:
    using TPtr = std::shared_ptr<TExecutorController>;
    TExecutorController(const NActors::TActorIdentity& executorActorId, const TConfig& config)
        : ExecutorActorId(executorActorId)
        , Config(config)
        , UserToken(NACLib::TSystemUsers::Metadata())
    {

    }

    const NACLib::TUserToken& GetUserToken() const {
        return UserToken;
    }

    TString GetTableName() const {
        return Config.GetTablePath();
    }

    const NMetadata::NRequest::TConfig& GetRequestConfig() const {
        return Config.GetRequestConfig();
    }

    virtual void OnInitializationFinished(const TString& id) const override;
    void OnLockPingerFinished() const;
    void OnTaskFetched(const TTask& task) const;
    void OnTaskFinished(const TString& taskId) const;
    void OnAssignFinished() const;
    void OnFetchingFinished() const;
};

}
