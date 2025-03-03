#pragma once

#include "public.h"

#include <cloud/filestore/libs/service/error.h>
#include <cloud/filestore/libs/service/filestore.h>
#include <cloud/filestore/libs/storage/api/components.h>
#include <cloud/filestore/libs/storage/api/events.h>
#include <cloud/filestore/libs/storage/tablet/model/blob.h>
#include <cloud/filestore/libs/storage/tablet/model/block.h>
#include <cloud/filestore/libs/storage/tablet/model/range.h>

#include <contrib/ydb/core/base/blobstorage.h>

#include <util/datetime/base.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

namespace NCloud::NFileStore::NStorage {

////////////////////////////////////////////////////////////////////////////////

#define FILESTORE_TABLET_REQUESTS_PRIVATE_ASYNC(xxx, ...)                      \
    xxx(Compaction,                             __VA_ARGS__)                   \
    xxx(CollectGarbage,                         __VA_ARGS__)                   \
    xxx(CleanupSessions,                        __VA_ARGS__)                   \
    xxx(DeleteCheckpoint,                       __VA_ARGS__)                   \
    xxx(DumpCompactionRange,                    __VA_ARGS__)                   \
    xxx(Flush,                                  __VA_ARGS__)                   \
    xxx(FlushBytes,                             __VA_ARGS__)                   \
    xxx(ForcedCompaction,                       __VA_ARGS__)                   \
    xxx(Truncate,                               __VA_ARGS__)                   \
    xxx(ReadBlob,                               __VA_ARGS__)                   \
    xxx(WriteBlob,                              __VA_ARGS__)                   \
    xxx(WriteBatch,                             __VA_ARGS__)                   \
// FILESTORE_TABLET_REQUESTS_PRIVATE_ASYNC

#define FILESTORE_TABLET_REQUESTS_PRIVATE_SYNC(xxx, ...)                       \
    xxx(AddBlob,                                __VA_ARGS__)                   \
    xxx(Cleanup,                                __VA_ARGS__)                   \
    xxx(DeleteGarbage,                          __VA_ARGS__)                   \
    xxx(TruncateRange,                          __VA_ARGS__)                   \
    xxx(ZeroRange,                              __VA_ARGS__)                   \
    xxx(FilterAliveNodes,                       __VA_ARGS__)                   \
// FILESTORE_TABLET_REQUESTS_PRIVATE

#define FILESTORE_TABLET_REQUESTS_PRIVATE(xxx, ...)                            \
    FILESTORE_TABLET_REQUESTS_PRIVATE_ASYNC(xxx, __VA_ARGS__)                  \
    FILESTORE_TABLET_REQUESTS_PRIVATE_SYNC(xxx,  __VA_ARGS__)                  \
// FILESTORE_TABLET_REQUESTS_PRIVATE

#define FILESTORE_DECLARE_PRIVATE_EVENT_IDS(name, ...)                         \
    FILESTORE_DECLARE_EVENT_IDS(name, __VA_ARGS__)                             \
    Ev##name##Completed,                                                       \
// FILESTORE_DECLARE_PRIVATE_EVENT_IDS

#define FILESTORE_DECLARE_PRIVATE_EVENTS(name, ...)                            \
    FILESTORE_DECLARE_EVENTS(name, __VA_ARGS__)                                \
                                                                               \
    using TEv##name##Completed = TResponseEvent<                               \
        T##name##Completed,                                                    \
        Ev##name##Completed                                                    \
    >;                                                                         \
// FILESTORE_DECLARE_PRIVATE_EVENTS

#define FILESTORE_IMPLEMENT_ASYNC_REQUEST(name, ns)                            \
    FILESTORE_IMPLEMENT_REQUEST(name, ns)                                      \
    void Handle##name##Completed(                                              \
        const ns::TEv##name##Completed::TPtr& ev,                              \
        const NActors::TActorContext& ctx);                                    \
// FILESTORE_IMPLEMENT_ASYNC_REQUEST

#define FILESTORE_HANDLE_COMPLETION(name, ns)                                  \
    HFunc(ns::TEv##name##Completed, Handle##name##Completed);                  \
// FILESTORE_HANDLE_COMPLETION

#define FILESTORE_IGNORE_COMPLETION(name, ns)                                  \
    IgnoreFunc(ns::TEv##name##Completed);                                      \
// FILESTORE_HANDLE_COMPLETION

////////////////////////////////////////////////////////////////////////////////

struct TReadBlob
{
    struct TBlock
    {
        ui32 BlobOffset;
        ui32 BlockOffset;

        TBlock(ui32 blobOffset, ui32 blockOffset)
            : BlobOffset(blobOffset)
            , BlockOffset(blockOffset)
        {}
    };

    TPartialBlobId BlobId;
    TVector<TBlock> Blocks;

    TInstant Deadline = TInstant::Max();
    bool Async = false;

    TReadBlob(const TPartialBlobId& blobId, TVector<TBlock> blocks)
        : BlobId(blobId)
        , Blocks(std::move(blocks))
    {}
};

////////////////////////////////////////////////////////////////////////////////

struct TWriteBlob
{
    TPartialBlobId BlobId;
    TString BlobContent;

    TInstant Deadline = TInstant::Max();
    bool Async = false;

    TWriteBlob(const TPartialBlobId& blobId, TString blobContent)
        : BlobId(blobId)
        , BlobContent(std::move(blobContent))
    {}
};

////////////////////////////////////////////////////////////////////////////////

enum class EAddBlobMode
{
    Write,
    WriteBatch,
    Flush,
    FlushBytes,
    Compaction,
};

////////////////////////////////////////////////////////////////////////////////

enum class EDeleteCheckpointMode
{
    MarkCheckpointDeleted,
    RemoveCheckpointNodes,
    RemoveCheckpointBlobs,
    RemoveCheckpoint,
};

////////////////////////////////////////////////////////////////////////////////

struct TWriteRange
{
    ui64 NodeId = InvalidNodeId;
    ui64 MaxOffset = 0;
};

////////////////////////////////////////////////////////////////////////////////

struct TEvIndexTabletPrivate
{
    //
    // Operation completion
    //

    struct TOperationCompleted
    {
        TSet<ui32> MixedBlocksRanges;
        ui64 CommitId = 0;
    };

    //
    // CleanupSessions
    //

    struct TCleanupSessionsRequest
    {
    };

    struct TCleanupSessionsResponse
    {
    };

    using TCleanupSessionsCompleted = TEmpty;

    //
    // WriteBatch
    //

    struct TWriteBatchRequest
    {
    };

    struct TWriteBatchResponse
    {
    };

    using TWriteBatchCompleted = TOperationCompleted;

    //
    // ReadBlob
    //

    struct TReadBlobRequest
    {
        IBlockBufferPtr Buffer;
        TVector<TReadBlob> Blobs;
    };

    struct TReadBlobResponse
    {
    };

    using TReadBlobCompleted = TEmpty;

    //
    // WriteBlob
    //

    struct TWriteBlobRequest
    {
        TVector<TWriteBlob> Blobs;
    };

    struct TWriteBlobResponse
    {
    };

    struct TWriteBlobCompleted
    {
        struct TWriteRequestResult
        {
            NKikimr::TLogoBlobID BlobId;
            NKikimr::TStorageStatusFlags StorageStatusFlags;
            double ApproximateFreeSpaceShare = 0;
        };

        TVector<TWriteRequestResult> Results;
    };

    //
    // AddBlob
    //

    struct TAddBlobRequest
    {
        EAddBlobMode Mode;
        TVector<TMixedBlobMeta> SrcBlobs;
        TVector<TBlock> SrcBlocks;
        TVector<TMixedBlobMeta> MixedBlobs;
        TVector<TMergedBlobMeta> MergedBlobs;
        TVector<TWriteRange> WriteRanges;
    };

    struct TAddBlobResponse
    {
    };

    //
    // Flush
    //

    struct TFlushRequest
    {
    };

    struct TFlushResponse
    {
    };


    using TFlushCompleted = TOperationCompleted;

    //
    // FlushBytes
    //

    struct TFlushBytesRequest
    {
    };

    struct TFlushBytesResponse
    {
    };

    struct TFlushBytesCompleted
    {
        TCallContextPtr CallContext;
        TSet<ui32> MixedBlocksRanges;
        ui64 CollectCommitId = 0;
        ui64 ChunkId = 0;
    };

    //
    // Cleanup
    //

    struct TCleanupRequest
    {
        ui32 RangeId;

        TCleanupRequest(ui32 rangeId)
            : RangeId(rangeId)
        {}
    };

    struct TCleanupResponse
    {
    };

    //
    // Compaction
    //

    struct TCompactionRequest
    {
        const ui32 RangeId;
        const bool FilterNodes;

        TCompactionRequest(ui32 rangeId, bool filterNodes)
            : RangeId(rangeId)
            , FilterNodes(filterNodes)
        {}
    };

    struct TCompactionResponse
    {
    };

    using TCompactionCompleted = TOperationCompleted;

    //
    // ForcedCompaction
    //

    struct TForcedCompactionRequest
    {
        TVector<ui32> Ranges;

        TForcedCompactionRequest(TVector<ui32> ranges)
            : Ranges(std::move(ranges))
        {}
    };

    struct TForcedCompactionResponse
    {
    };

    using TForcedCompactionCompleted = TEmpty;

    //
    // DumpCompactionRange
    //

    struct TDumpCompactionRangeRequest
    {
        const ui32 RangeId;

        TDumpCompactionRangeRequest(ui32 rangeId)
            : RangeId(rangeId)
        {}
    };

    struct TDumpCompactionRangeResponse
    {
        const ui32 RangeId = 0;
        TVector<TMixedBlobMeta> Blobs;

        TDumpCompactionRangeResponse() = default;

        TDumpCompactionRangeResponse(ui32 rangeId, TVector<TMixedBlobMeta> blobs)
            : RangeId(rangeId)
            , Blobs(std::move(blobs))
        {}
    };

    using TDumpCompactionRangeCompleted = TEmpty;

    //
    // CollectGarbage
    //

    struct TCollectGarbageRequest
    {
    };

    struct TCollectGarbageResponse
    {
    };


    using TCollectGarbageCompleted = TOperationCompleted;

    //
    // DeleteGarbage
    //

    struct TDeleteGarbageRequest
    {
        ui64 CollectCommitId;
        TVector<TPartialBlobId> NewBlobs;
        TVector<TPartialBlobId> GarbageBlobs;

        TDeleteGarbageRequest(
                ui64 collectCommitId,
                TVector<TPartialBlobId> newBlobs,
                TVector<TPartialBlobId> garbageBlobs)
            : CollectCommitId(collectCommitId)
            , NewBlobs(std::move(newBlobs))
            , GarbageBlobs(std::move(garbageBlobs))
        {}
    };

    struct TDeleteGarbageResponse
    {
    };

    //
    // DeleteCheckpoint
    //

    struct TDeleteCheckpointRequest
    {
        TString CheckpointId;
        EDeleteCheckpointMode Mode;

        TDeleteCheckpointRequest(
                TString checkpointId,
                EDeleteCheckpointMode mode)
            : CheckpointId(std::move(checkpointId))
            , Mode(mode)
        {}
    };

    struct TDeleteCheckpointResponse
    {
    };

    using TDeleteCheckpointCompleted = TEmpty;

    //
    //  Truncate node
    //

    struct TTruncateRangeRequest
    {
        ui64 NodeId = 0;
        TByteRange Range;

        TTruncateRangeRequest(ui64 nodeId, TByteRange range)
            : NodeId(nodeId)
            , Range(range)
        {}
    };

    struct TTruncateRangeResponse
    {};

    //
    //  Truncate node
    //

    struct TTruncateRequest
    {
        ui64 NodeId = 0;
        TByteRange Range;

        TTruncateRequest(ui64 nodeId, TByteRange range)
            : NodeId(nodeId)
            , Range(range)
        {}
    };

    struct TTruncateResponse
    {
        ui64 NodeId = 0;
    };

    struct TTruncateCompleted
    {
        ui64 NodeId = 0;
    };

    //
    // Zero range
    //

    struct TZeroRangeRequest
    {
        ui64 NodeId = 0;
        TByteRange Range;

        TZeroRangeRequest(ui64 nodeId, TByteRange range)
            : NodeId(nodeId)
            , Range(range)
        {}
    };

    struct TZeroRangeResponse
    {
        ui64 NodeId = 0;
    };

    //
    // Filter alive nodes
    //

    struct TFilterAliveNodesRequest
    {
        TStackVec<ui64, 16> Nodes;

        TFilterAliveNodesRequest(TStackVec<ui64, 16> nodes)
            : Nodes(std::move(nodes))
        {}
    };

    struct TFilterAliveNodesResponse
    {
        TSet<ui64> Nodes;
    };

    //
    // Events declaration
    //

    enum EEvents
    {
        EvBegin = TFileStoreEventsPrivate::TABLET_START,

        FILESTORE_TABLET_REQUESTS_PRIVATE_SYNC(FILESTORE_DECLARE_EVENT_IDS)
        FILESTORE_TABLET_REQUESTS_PRIVATE_ASYNC(FILESTORE_DECLARE_PRIVATE_EVENT_IDS)

        EvUpdateCounters,
        EvUpdateLeakyBucketCounters,

        EvReadDataCompleted,
        EvWriteDataCompleted,

        EvEnd
    };

    static_assert(EvEnd < (int)TFileStoreEventsPrivate::TABLET_END,
        "EvEnd expected to be < TFileStoreEventsPrivate::TABLET_END");

    FILESTORE_TABLET_REQUESTS_PRIVATE_ASYNC(FILESTORE_DECLARE_PRIVATE_EVENTS)
    FILESTORE_TABLET_REQUESTS_PRIVATE_SYNC(FILESTORE_DECLARE_EVENTS)

    using TEvUpdateCounters = TRequestEvent<TEmpty, EvUpdateCounters>;
    using TEvUpdateLeakyBucketCounters = TRequestEvent<TEmpty, EvUpdateLeakyBucketCounters>;

    using TEvReadDataCompleted = TResponseEvent<TOperationCompleted, EvReadDataCompleted>;
    using TEvWriteDataCompleted = TResponseEvent<TOperationCompleted, EvWriteDataCompleted>;
};

}   // namespace NCloud::NFileStore::NStorage
