#pragma once

#include "public.h"

#include "config.h"
#include "part_mirror_resync_state.h"
#include "part_nonrepl_events_private.h"
#include "resync_range.h"

#include <cloud/blockstore/libs/diagnostics/public.h>
#include <cloud/blockstore/libs/rdma/iface/public.h>
#include <cloud/blockstore/libs/storage/api/disk_registry.h>
#include <cloud/blockstore/libs/storage/api/partition.h>
#include <cloud/blockstore/libs/storage/api/service.h>
#include <cloud/blockstore/libs/storage/api/volume.h>
#include <cloud/blockstore/libs/storage/core/disk_counters.h>
#include <cloud/blockstore/libs/storage/core/request_info.h>
#include <cloud/blockstore/libs/storage/model/requests_in_progress.h>
#include <cloud/blockstore/libs/storage/partition_common/drain_actor_companion.h>

#include <library/cpp/actors/core/actor_bootstrapped.h>
#include <library/cpp/actors/core/events.h>
#include <library/cpp/actors/core/hfunc.h>
#include <library/cpp/actors/core/mon.h>

namespace NCloud::NBlockStore::NStorage {

////////////////////////////////////////////////////////////////////////////////

constexpr TDuration ResyncNextRangeInterval = TDuration::MilliSeconds(1);

////////////////////////////////////////////////////////////////////////////////

class TMirrorPartitionResyncActor final
    : public NActors::TActorBootstrapped<TMirrorPartitionResyncActor>
{
private:
    const TStorageConfigPtr Config;
    const IProfileLogPtr ProfileLog;
    const IBlockDigestGeneratorPtr BlockDigestGenerator;
    TString RWClientId;
    TNonreplicatedPartitionConfigPtr PartConfig;
    TMigrations Migrations;
    TVector<TDevices> ReplicaDevices;
    NRdma::IClientPtr RdmaClient;
    NActors::TActorId StatActorId;

    TMirrorPartitionResyncState State;

    NActors::TActorId MirrorActorId;
    TVector<TResyncReplica> Replicas;

    TPartitionDiskCountersPtr MirrorCounters;
    bool UpdateCountersScheduled = false;

    bool ResyncFinished = false;

    TRequestsInProgress<ui64, TBlockRange64> WriteAndZeroRequestsInProgress{
        EAllowedRequests::WriteOnly};
    TDrainActorCompanion DrainActorCompanion{
        WriteAndZeroRequestsInProgress,
        PartConfig->GetName()};

    TRequestInfoPtr Poisoner;

    struct TPostponedRead
    {
        NActors::IEventHandlePtr Ev;
        TBlockRange64 BlockRange;
    };
    TDeque<TPostponedRead> PostponedReads;

public:
    TMirrorPartitionResyncActor(
        TStorageConfigPtr config,
        IProfileLogPtr profileLog,
        IBlockDigestGeneratorPtr digestGenerator,
        TString rwClientId,
        TNonreplicatedPartitionConfigPtr partConfig,
        TMigrations migrations,
        TVector<TDevices> replicaDevices,
        NRdma::IClientPtr rdmaClient,
        NActors::TActorId statActorId,
        ui64 initialResyncIndex);

    ~TMirrorPartitionResyncActor();

    void Bootstrap(const NActors::TActorContext& ctx);

    // For unit tests
    void SetRequestIdentityKey(ui64 value) {
        WriteAndZeroRequestsInProgress.SetRequestIdentityKey(value);
    }

private:
    void RejectPostponedRead(TPostponedRead& pr);
    void KillActors(const NActors::TActorContext& ctx);
    void SetupPartitions(const NActors::TActorContext& ctx);
    void ScheduleCountersUpdate(const NActors::TActorContext& ctx);
    void SendStats(const NActors::TActorContext& ctx);

    void ContinueResyncIfNeeded(const NActors::TActorContext& ctx);
    void ScheduleResyncNextRange(const NActors::TActorContext& ctx);
    void ResyncNextRange(const NActors::TActorContext& ctx);

    bool IsAnybodyAlive() const;
    void ReplyAndDie(const NActors::TActorContext& ctx);

private:
    STFUNC(StateWork);
    STFUNC(StateZombie);

    void HandleResyncNextRange(
        const TEvNonreplPartitionPrivate::TEvResyncNextRange::TPtr& ev,
        const NActors::TActorContext& ctx);

    void HandleRangeResynced(
        const TEvNonreplPartitionPrivate::TEvRangeResynced::TPtr& ev,
        const NActors::TActorContext& ctx);

    void HandleWriteOrZeroCompleted(
        const TEvNonreplPartitionPrivate::TEvWriteOrZeroCompleted::TPtr& ev,
        const NActors::TActorContext& ctx);

    void HandleResyncStateUpdated(
        const TEvVolume::TEvResyncStateUpdated::TPtr& ev,
        const NActors::TActorContext& ctx);

    void HandleRWClientIdChanged(
        const TEvVolume::TEvRWClientIdChanged::TPtr& ev,
        const NActors::TActorContext& ctx);

    void HandlePartCounters(
        const TEvVolume::TEvDiskRegistryBasedPartitionCounters::TPtr& ev,
        const NActors::TActorContext& ctx);

    void HandleUpdateCounters(
        const TEvNonreplPartitionPrivate::TEvUpdateCounters::TPtr& ev,
        const NActors::TActorContext& ctx);

    void HandlePoisonPill(
        const NActors::TEvents::TEvPoisonPill::TPtr& ev,
        const NActors::TActorContext& ctx);

    void HandlePoisonTaken(
        const NActors::TEvents::TEvPoisonTaken::TPtr& ev,
        const NActors::TActorContext& ctx);

    template <typename TMethod>
    void ForwardRequest(
        const typename TMethod::TRequest::TPtr& ev,
        const NActors::TActorContext& ctx);

    template <typename TMethod>
    void ProcessReadRequest(
        const typename TMethod::TRequest::TPtr& ev,
        const NActors::TActorContext& ctx);

    BLOCKSTORE_IMPLEMENT_REQUEST(ReadBlocks, TEvService);
    BLOCKSTORE_IMPLEMENT_REQUEST(WriteBlocks, TEvService);
    BLOCKSTORE_IMPLEMENT_REQUEST(ReadBlocksLocal, TEvService);
    BLOCKSTORE_IMPLEMENT_REQUEST(WriteBlocksLocal, TEvService);
    BLOCKSTORE_IMPLEMENT_REQUEST(ZeroBlocks, TEvService);
    BLOCKSTORE_IMPLEMENT_REQUEST(Drain, NPartition::TEvPartition);

    BLOCKSTORE_IMPLEMENT_REQUEST(DescribeBlocks, TEvVolume);
    BLOCKSTORE_IMPLEMENT_REQUEST(CompactRange, TEvVolume);
    BLOCKSTORE_IMPLEMENT_REQUEST(GetCompactionStatus, TEvVolume);
    BLOCKSTORE_IMPLEMENT_REQUEST(RebuildMetadata, TEvVolume);
    BLOCKSTORE_IMPLEMENT_REQUEST(GetRebuildMetadataStatus, TEvVolume);
};

}   // namespace NCloud::NBlockStore::NStorage
