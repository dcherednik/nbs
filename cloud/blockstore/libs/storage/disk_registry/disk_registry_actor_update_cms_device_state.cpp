#include "disk_registry_actor.h"

namespace NCloud::NBlockStore::NStorage {

using namespace NActors;
using namespace NKikimr;
using namespace NKikimr::NTabletFlatExecutor;

////////////////////////////////////////////////////////////////////////////////

void TDiskRegistryActor::HandleUpdateCmsHostDeviceState(
    const TEvDiskRegistryPrivate::TEvUpdateCmsHostDeviceStateRequest::TPtr& ev,
    const TActorContext& ctx)
{
    BLOCKSTORE_DISK_REGISTRY_COUNTER(UpdateCmsHostDeviceState);

    auto* msg = ev->Get();

    auto requestInfo = CreateRequestInfo(
        ev->Sender,
        ev->Cookie,
        msg->CallContext);

    LOG_INFO(ctx, TBlockStoreComponents::DISK_REGISTRY,
        "[%lu] Received UpdateCmsHostDeviceState request"
        ": host=%s, path=%s, State=%u",
        TabletID(),
        msg->Host.c_str(),
        msg->Path.c_str(),
        static_cast<ui32>(msg->State));

    ExecuteTx<TUpdateCmsHostDeviceState>(
        ctx,
        std::move(requestInfo),
        std::move(msg->Host),
        std::move(msg->Path),
        msg->State,
        msg->DryRun);
}
////////////////////////////////////////////////////////////////////////////////

bool TDiskRegistryActor::PrepareUpdateCmsHostDeviceState(
    const TActorContext& ctx,
    TTransactionContext& tx,
    TTxDiskRegistry::TUpdateCmsHostDeviceState& args)
{
    Y_UNUSED(ctx);
    Y_UNUSED(tx);
    Y_UNUSED(args);

    return true;
}

void TDiskRegistryActor::ExecuteUpdateCmsHostDeviceState(
    const TActorContext& ctx,
    TTransactionContext& tx,
    TTxDiskRegistry::TUpdateCmsHostDeviceState& args)
{
    TDiskRegistryDatabase db(tx.DB);

    args.TxTs = ctx.Now();
    args.Error = State->UpdateCmsDeviceState(
        db,
        args.Host,
        args.Path,
        args.State,
        args.TxTs,
        args.DryRun,
        args.AffectedDisks,
        args.Timeout);
}

void TDiskRegistryActor::CompleteUpdateCmsHostDeviceState(
    const TActorContext& ctx,
    TTxDiskRegistry::TUpdateCmsHostDeviceState& args)
{
    LOG_INFO(ctx, TBlockStoreComponents::DISK_REGISTRY,
        "UpdateCmsDeviceState result: %s %u",
        FormatError(args.Error).c_str(),
        args.Timeout.Seconds());

    ReallocateDisks(ctx);
    NotifyUsers(ctx);
    PublishDiskStates(ctx);

    SecureErase(ctx);
    StartMigration(ctx);

    using TResponse = TEvDiskRegistryPrivate::TEvUpdateCmsHostDeviceStateResponse;

    auto response = std::make_unique<TResponse>(std::move(args.Error));
    response->Timeout = args.Timeout;
    response->DependentDiskIds = std::move(args.AffectedDisks);

    NCloud::Reply(ctx, *args.RequestInfo, std::move(response));
}

}   // namespace NCloud::NBlockStore::NStorage
