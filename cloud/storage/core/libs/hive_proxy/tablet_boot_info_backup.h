#pragma once

#include "public.h"

#include <cloud/storage/core/libs/api/hive_proxy.h>
#include <cloud/storage/core/libs/hive_proxy/hive_proxy_events_private.h>
#include <cloud/storage/core/libs/hive_proxy/protos/tablet_boot_info_backup.pb.h>
#include <cloud/storage/core/libs/kikimr/helpers.h>
#include <cloud/storage/core/libs/kikimr/public.h>

#include <library/cpp/actors/core/actor_bootstrapped.h>
#include <library/cpp/actors/core/events.h>
#include <library/cpp/actors/core/hfunc.h>

#include <util/folder/path.h>
#include <util/generic/string.h>

#include <memory>

namespace NCloud::NStorage {

////////////////////////////////////////////////////////////////////////////////

struct TTabletBootInfo
{
    TTabletBootInfo() = default;

    TTabletBootInfo(
            NKikimr::TTabletStorageInfoPtr storageInfo,
            ui64 suggestedGeneration)
        : StorageInfo(std::move(storageInfo))
        , SuggestedGeneration(suggestedGeneration)
    {}

    NKikimr::TTabletStorageInfoPtr StorageInfo;
    ui64 SuggestedGeneration = 0;
};

////////////////////////////////////////////////////////////////////////////////

class TTabletBootInfoBackup final
    : public NActors::TActorBootstrapped<TTabletBootInfoBackup>
{
private:
    int LogComponent;
    const TFsPath BackupFilePath;
    const bool ReadOnlyMode = false;

    NHiveProxy::NProto::TTabletBootInfoBackup BackupProto;
    const TFsPath TmpBackupFilePath;

public:
    TTabletBootInfoBackup(
        int logComponent,
        TString backupFilePath,
        bool readOnlyMode);

    void Bootstrap(const NActors::TActorContext& ctx);

private:
    STFUNC(StateWork);

    void ScheduleBackup(const NActors::TActorContext& ctx);
    NProto::TError Backup(const NActors::TActorContext& ctx);

    void HandleWakeup(
        const NActors::TEvents::TEvWakeup::TPtr& ev,
        const NActors::TActorContext& ctx);

    void HandleReadTabletBootInfoBackup(
        const TEvHiveProxyPrivate::TEvReadTabletBootInfoBackupRequest::TPtr& ev,
        const NActors::TActorContext& ctx);

    void HandleUpdateTabletBootInfoBackup(
        const TEvHiveProxyPrivate::TEvUpdateTabletBootInfoBackupRequest::TPtr& ev,
        const NActors::TActorContext& ctx);

    void HandleBackupTabletBootInfos(
        const TEvHiveProxy::TEvBackupTabletBootInfosRequest::TPtr& ev,
        const NActors::TActorContext& ctx);
};

}   // namespace NCloud::NStorage
