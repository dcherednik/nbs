#include "part_actor.h"

#include <cloud/blockstore/libs/diagnostics/block_digest.h>

#include <library/cpp/actors/core/actor_bootstrapped.h>

#include <util/generic/vector.h>

namespace NCloud::NBlockStore::NStorage::NPartition {

using namespace NActors;

using namespace NKikimr;
using namespace NKikimr::NTabletFlatExecutor;

LWTRACE_USING(BLOCKSTORE_STORAGE_PROVIDER);

namespace {

////////////////////////////////////////////////////////////////////////////////

template <typename ...T>
IEventBasePtr CreateWriteBlocksResponse(bool replyLocal, T&& ...args)
{
    if (replyLocal) {
        return std::make_unique<TEvService::TEvWriteBlocksLocalResponse>(
            std::forward<T>(args)...);
    } else {
        return std::make_unique<TEvService::TEvWriteBlocksResponse>(
            std::forward<T>(args)...);
    }
}

////////////////////////////////////////////////////////////////////////////////

class TWriteMergedBlocksActor final
    : public TActorBootstrapped<TWriteMergedBlocksActor>
{
public:
    struct TWriteBlobRequest
    {
        const TPartialBlobId BlobId;
        const TBlockRange32 WriteRange;

        TWriteBlobRequest(
                const TPartialBlobId& blobId,
                const TBlockRange32& writeRange)
            : BlobId(blobId)
            , WriteRange(writeRange)
        {}
    };

private:
    const TActorId Tablet;
    const IBlockDigestGeneratorPtr BlockDigestGenerator;
    const ui64 CommitId;
    const TRequestInfoPtr RequestInfo;
    const TVector<TWriteBlobRequest> WriteBlobRequests;
    const bool ReplyLocal;
    const bool AddingUnconfirmedBlobsEnabled = false;
    const IWriteBlocksHandlerPtr WriteHandler;

    TVector<IProfileLog::TBlockInfo> AffectedBlockInfos;
    size_t WriteBlobRequestsCompleted = 0;

    TVector<TCallContextPtr> ForkedCallContexts;
    bool SafeToUseOrbit = true;

    bool UnconfirmedBlobsAdded = false;

public:
    TWriteMergedBlocksActor(
        const TActorId& tablet,
        IBlockDigestGeneratorPtr blockDigestGenerator,
        ui64 commitId,
        TRequestInfoPtr requestInfo,
        TVector<TWriteBlobRequest> writeBlobRequests,
        bool replyLocal,
        bool addingUnconfirmedBlobsEnabled,
        IWriteBlocksHandlerPtr writeHandler);

    void Bootstrap(const TActorContext& ctx);

private:
    TGuardedSgList BuildBlobContent(const TWriteBlobRequest& request);

    void WriteBlobs(const TActorContext& ctx);
    void AddBlobs(const TActorContext& ctx, bool confirmed);

    void NotifyCompleted(const TActorContext& ctx, const NProto::TError& error);
    bool HandleError(const TActorContext& ctx, const NProto::TError& error);

    void ReplyAndDie(const TActorContext& ctx, const NProto::TError& error);

    void Reply(
        const TActorContext& ctx,
        TRequestInfo& requestInfo,
        IEventBasePtr response);

private:
    STFUNC(StateWork);

    void HandleWriteBlobResponse(
        const TEvPartitionPrivate::TEvWriteBlobResponse::TPtr& ev,
        const TActorContext& ctx);

    void HandleAddBlobsResponse(
        const TEvPartitionPrivate::TEvAddBlobsResponse::TPtr& ev,
        const TActorContext& ctx);

    void HandleAddUnconfirmedBlobsResponse(
        const TEvPartitionPrivate::TEvAddUnconfirmedBlobsResponse::TPtr& ev,
        const TActorContext& ctx);

    void HandlePoisonPill(
        const TEvents::TEvPoisonPill::TPtr& ev,
        const TActorContext& ctx);
};

////////////////////////////////////////////////////////////////////////////////

TWriteMergedBlocksActor::TWriteMergedBlocksActor(
        const TActorId& tablet,
        IBlockDigestGeneratorPtr blockDigestGenerator,
        ui64 commitId,
        TRequestInfoPtr requestInfo,
        TVector<TWriteBlobRequest> writeBlobRequests,
        bool replyLocal,
        bool addingUnconfirmedBlobsEnabled,
        IWriteBlocksHandlerPtr writeHandler)
    : Tablet(tablet)
    , BlockDigestGenerator(std::move(blockDigestGenerator))
    , CommitId(commitId)
    , RequestInfo(std::move(requestInfo))
    , WriteBlobRequests(std::move(writeBlobRequests))
    , ReplyLocal(replyLocal)
    , AddingUnconfirmedBlobsEnabled(addingUnconfirmedBlobsEnabled)
    , WriteHandler(std::move(writeHandler))
{
    ActivityType = TBlockStoreActivities::PARTITION_WORKER;
}

void TWriteMergedBlocksActor::Bootstrap(const TActorContext& ctx)
{
    TRequestScope timer(*RequestInfo);

    LWTRACK(
        RequestReceived_PartitionWorker,
        RequestInfo->CallContext->LWOrbit,
        "WriteMergedBlocks",
        RequestInfo->CallContext->RequestId);

    Become(&TThis::StateWork);

    WriteBlobs(ctx);
    if (AddingUnconfirmedBlobsEnabled) {
        AddBlobs(ctx, false /* confirmed */);
    }
}

TGuardedSgList TWriteMergedBlocksActor::BuildBlobContent(
    const TWriteBlobRequest& request)
{
    auto guardedSgList =
        WriteHandler->GetBlocks(ConvertRangeSafe(request.WriteRange));

    if (auto guard = guardedSgList.Acquire()) {
        const auto& sgList = guard.Get();

        for (size_t index = 0; index < sgList.size(); ++index) {
            const auto& block = sgList[index];

            auto blockIndex = request.WriteRange.Start + index;
            const auto digest = BlockDigestGenerator->ComputeDigest(
                blockIndex,
                block);

            if (digest.Defined()) {
                AffectedBlockInfos.push_back({blockIndex, *digest});
            }
        }
    }
    return guardedSgList;
}

void TWriteMergedBlocksActor::WriteBlobs(const TActorContext& ctx)
{
    for (ui32 i = 0; i < WriteBlobRequests.size(); ++i) {
        const auto& req = WriteBlobRequests[i];
        auto guardedSglist = BuildBlobContent(req);

        auto request = std::make_unique<TEvPartitionPrivate::TEvWriteBlobRequest>(
            req.BlobId,
            std::move(guardedSglist));

        if (!RequestInfo->CallContext->LWOrbit.Fork(request->CallContext->LWOrbit)) {
            LWTRACK(
                ForkFailed,
                RequestInfo->CallContext->LWOrbit,
                "TEvPartitionPrivate::TEvWriteBlobRequest",
                RequestInfo->CallContext->RequestId);
        }

        ForkedCallContexts.emplace_back(request->CallContext);

        NCloud::Send(
            ctx,
            Tablet,
            std::move(request));
    }
}

void TWriteMergedBlocksActor::AddBlobs(
    const TActorContext& ctx,
    bool confirmed)
{
    Y_DEBUG_ABORT_UNLESS(RequestInfo);

    IEventBasePtr request;

    if (confirmed) {
        TVector<TAddMergedBlob> blobs(Reserve(WriteBlobRequests.size()));

        for (const auto& req: WriteBlobRequests) {
            blobs.emplace_back(req.BlobId, req.WriteRange, TBlockMask());
        }

        request = std::make_unique<TEvPartitionPrivate::TEvAddBlobsRequest>(
            RequestInfo->CallContext,
            CommitId,
            TVector<TAddMixedBlob>(),
            std::move(blobs),
            TVector<TAddFreshBlob>(),
            ADD_WRITE_RESULT
        );
    } else {
        TVector<TUnconfirmedBlob> blobs(Reserve(WriteBlobRequests.size()));

        for (const auto& req: WriteBlobRequests) {
            blobs.emplace_back(
                req.BlobId.UniqueId(),
                req.WriteRange);
        }

        request = std::make_unique<TEvPartitionPrivate::TEvAddUnconfirmedBlobsRequest>(
            RequestInfo->CallContext,
            CommitId,
            std::move(blobs));
    }

    SafeToUseOrbit = false;

    NCloud::Send(
        ctx,
        Tablet,
        std::move(request));
}

void TWriteMergedBlocksActor::NotifyCompleted(
    const TActorContext& ctx,
    const NProto::TError& error)
{
    using TEvent = TEvPartitionPrivate::TEvWriteBlocksCompleted;
    auto ev = std::make_unique<TEvent>(
        error,
        true,   // collectGarbageBarrierAcquired
        UnconfirmedBlobsAdded);

    ev->ExecCycles = RequestInfo->GetExecCycles();
    ev->TotalCycles = RequestInfo->GetTotalCycles();

    ev->CommitId = CommitId;
    ev->AffectedBlockInfos = std::move(AffectedBlockInfos);

    ui64 waitCycles = RequestInfo->GetWaitCycles();

    ui32 blocksCount = 0;
    for (const auto& r: WriteBlobRequests) {
        blocksCount += r.WriteRange.Size();
    }

    auto execTime = CyclesToDurationSafe(ev->ExecCycles);
    auto waitTime = CyclesToDurationSafe(waitCycles);

    auto& counters = *ev->Stats.MutableUserWriteCounters();
    counters.SetRequestsCount(1);
    counters.SetBatchCount(1);
    counters.SetBlocksCount(blocksCount);
    counters.SetExecTime(execTime.MicroSeconds());
    counters.SetWaitTime(waitTime.MicroSeconds());

    NCloud::Send(ctx, Tablet, std::move(ev));
}

bool TWriteMergedBlocksActor::HandleError(
    const TActorContext& ctx,
    const NProto::TError& error)
{
    if (FAILED(error.GetCode())) {
        ReplyAndDie(ctx, error);
        return true;
    }

    return false;
}

void TWriteMergedBlocksActor::ReplyAndDie(
    const TActorContext& ctx,
    const NProto::TError& error)
{
    NotifyCompleted(ctx, error);

    auto response = CreateWriteBlocksResponse(ReplyLocal, error);
    Reply(ctx, *RequestInfo, std::move(response));

    Die(ctx);
}

void TWriteMergedBlocksActor::Reply(
    const TActorContext& ctx,
    TRequestInfo& requestInfo,
    IEventBasePtr response)
{
    if (SafeToUseOrbit) {
        LWTRACK(
            ResponseSent_Partition,
            requestInfo.CallContext->LWOrbit,
            "WriteMergedBlocks",
            requestInfo.CallContext->RequestId);
    }

    NCloud::Reply(ctx, requestInfo, std::move(response));
}

////////////////////////////////////////////////////////////////////////////////

void TWriteMergedBlocksActor::HandleWriteBlobResponse(
    const TEvPartitionPrivate::TEvWriteBlobResponse::TPtr& ev,
    const TActorContext& ctx)
{
    const auto* msg = ev->Get();

    RequestInfo->AddExecCycles(msg->ExecCycles);

    if (HandleError(ctx, msg->GetError())) {
        return;
    }

    Y_ABORT_UNLESS(WriteBlobRequestsCompleted < WriteBlobRequests.size());
    if (++WriteBlobRequestsCompleted < WriteBlobRequests.size()) {
        return;
    }

    for (auto context: ForkedCallContexts) {
        RequestInfo->CallContext->LWOrbit.Join(context->LWOrbit);
    }

    if (AddingUnconfirmedBlobsEnabled) {
        if (UnconfirmedBlobsAdded) {
            ReplyAndDie(ctx, MakeError(S_OK));
        }
    } else {
        AddBlobs(ctx, true /* confirmed */);
    }
}

void TWriteMergedBlocksActor::HandleAddBlobsResponse(
    const TEvPartitionPrivate::TEvAddBlobsResponse::TPtr& ev,
    const TActorContext& ctx)
{
    const auto* msg = ev->Get();

    SafeToUseOrbit = true;

    RequestInfo->AddExecCycles(msg->ExecCycles);

    const auto& error = msg->GetError();
    if (HandleError(ctx, error)) {
        return;
    }

    ReplyAndDie(ctx, error);
}

void TWriteMergedBlocksActor::HandleAddUnconfirmedBlobsResponse(
    const TEvPartitionPrivate::TEvAddUnconfirmedBlobsResponse::TPtr& ev,
    const TActorContext& ctx)
{
    const auto* msg = ev->Get();

    SafeToUseOrbit = true;

    RequestInfo->AddExecCycles(msg->ExecCycles);

    const auto& error = msg->GetError();
    if (HandleError(ctx, error)) {
        return;
    }

    UnconfirmedBlobsAdded = true;

    if (WriteBlobRequestsCompleted < WriteBlobRequests.size()) {
        return;
    }

    ReplyAndDie(ctx, error);
}

void TWriteMergedBlocksActor::HandlePoisonPill(
    const TEvents::TEvPoisonPill::TPtr& ev,
    const TActorContext& ctx)
{
    Y_UNUSED(ev);

    auto error = MakeError(E_REJECTED, "Tablet is dead");

    ReplyAndDie(ctx, error);
}

STFUNC(TWriteMergedBlocksActor::StateWork)
{
    TRequestScope timer(*RequestInfo);

    switch (ev->GetTypeRewrite()) {
        HFunc(TEvents::TEvPoisonPill, HandlePoisonPill);
        HFunc(TEvPartitionPrivate::TEvWriteBlobResponse, HandleWriteBlobResponse);
        HFunc(TEvPartitionPrivate::TEvAddBlobsResponse, HandleAddBlobsResponse);
        HFunc(TEvPartitionPrivate::TEvAddUnconfirmedBlobsResponse, HandleAddUnconfirmedBlobsResponse);

        default:
            HandleUnexpectedEvent(ev, TBlockStoreComponents::PARTITION_WORKER);
            break;
    }
}

}   // namespace

////////////////////////////////////////////////////////////////////////////////

void TPartitionActor::WriteMergedBlocks(
    const TActorContext& ctx,
    TRequestInBuffer<TWriteBufferRequestData> requestInBuffer)
{
    const auto commitId = State->GenerateCommitId();

    if (commitId == InvalidCommitId) {
        requestInBuffer.Data.RequestInfo->CancelRequest(ctx);
        RebootPartitionOnCommitIdOverflow(ctx, "WriteMergedBlocks");
        return;
    }

    State->GetCommitQueue().AcquireBarrier(commitId);
    State->GetGarbageQueue().AcquireBarrier(commitId);

    const auto writeRange = requestInBuffer.Data.Range;
    const auto maxBlocksInBlob = State->GetMaxBlocksInBlob();

    LOG_TRACE(ctx, TBlockStoreComponents::PARTITION,
        "[%lu] Writing merged blocks @%lu (range: %s)",
        TabletID(),
        commitId,
        DescribeRange(writeRange).data()
    );

    ui32 blobIndex = 0;

    TVector<TWriteMergedBlocksActor::TWriteBlobRequest> requests(
        Reserve(1 + writeRange.Size() / maxBlocksInBlob));

    for (ui64 blockIndex: xrange(writeRange, maxBlocksInBlob)) {
        auto range = TBlockRange32::MakeClosedIntervalWithLimit(
            blockIndex,
            blockIndex + maxBlocksInBlob - 1,
            writeRange.End);

        auto blobId = State->GenerateBlobId(
            EChannelDataKind::Merged,
            EChannelPermission::UserWritesAllowed,
            commitId,
            range.Size() * State->GetBlockSize(),
            blobIndex++);

        requests.emplace_back(blobId, range);
    }

    Y_ABORT_UNLESS(requests);

    auto actor = NCloud::Register<TWriteMergedBlocksActor>(
        ctx,
        SelfId(),
        BlockDigestGenerator,
        commitId,
        requestInBuffer.Data.RequestInfo,
        std::move(requests),
        requestInBuffer.Data.ReplyLocal,
        Config->GetAddingUnconfirmedBlobsEnabled(),
        std::move(requestInBuffer.Data.Handler)
    );
    Actors.Insert(actor);
}

}   // namespace NCloud::NBlockStore::NStorage::NPartition
