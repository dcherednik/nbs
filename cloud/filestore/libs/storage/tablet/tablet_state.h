#pragma once

#include "public.h"

#include "tablet_database.h"
#include "tablet_tx.h"

#include "checkpoint.h"
#include "helpers.h"
#include "session.h"

#include <cloud/filestore/libs/storage/model/channel_data_kind.h>
#include <cloud/filestore/libs/storage/tablet/model/alloc.h>
#include <cloud/filestore/libs/storage/tablet/model/blob.h>
#include <cloud/filestore/libs/storage/tablet/model/block.h>
#include <cloud/filestore/libs/storage/tablet/model/channels.h>
#include <cloud/filestore/libs/storage/tablet/model/compaction_map.h>
#include <cloud/filestore/libs/storage/tablet/model/operation.h>
#include <cloud/filestore/libs/storage/tablet/model/public.h>
#include <cloud/filestore/libs/storage/tablet/model/range_locks.h>
#include <cloud/filestore/libs/storage/tablet/model/throttler_logger.h>
#include <cloud/filestore/libs/storage/tablet/model/truncate_queue.h>
#include <cloud/filestore/libs/storage/tablet/model/verify.h>
#include <cloud/filestore/libs/storage/tablet/protos/tablet.pb.h>

#include <cloud/storage/core/libs/common/error.h>
#include <cloud/storage/core/libs/tablet/model/commit.h>

#include <library/cpp/actors/core/actorid.h>

#include <util/datetime/base.h>
#include <util/generic/maybe.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

namespace NCloud::NFileStore::NProto {

////////////////////////////////////////////////////////////////////////////////

class TProfileLogRequestInfo;

} // namespace NCloud::NFileStore::NProto

namespace NCloud::NFileStore::NStorage {

////////////////////////////////////////////////////////////////////////////////

class TIndexTabletState
{
private:
    TFileStoreAllocRegistry AllocatorRegistry;

    struct TImpl;
    std::unique_ptr<TImpl> Impl;

    ui32 Generation = 0;
    ui32 LastStep = 0;
    ui32 LastCollectCounter = 0;
    bool StartupGcExecuted = false;

    NProto::TFileSystem FileSystem;
    NProto::TFileSystemStats FileSystemStats;
    NCloud::NProto::TTabletStorageInfo TabletStorageInfo;

    /*const*/ ui32 TruncateBlocksThreshold = 0;
    /*const*/ ui32 SessionHistoryEntryCount = 0;

    bool StateLoaded = false;

protected:
    TString LogTag;

public:
    TIndexTabletState();
    ~TIndexTabletState();

    void UpdateLogTag(TString tag);

    void LoadState(
        ui32 generation,
        const TStorageConfig& config,
        const NProto::TFileSystem& fileSystem,
        const NProto::TFileSystemStats& fileSystemStats,
        const NCloud::NProto::TTabletStorageInfo& tabletStorageInfo,
        const TThrottlerConfig& throttlerConfig);

    bool IsStateLoaded() const
    {
        return StateLoaded;
    }

    void CompleteStateLoad()
    {
        StateLoaded = true;
    }

    void UpdateConfig(
        TIndexTabletDatabase& db,
        const NProto::TFileSystem& fileSystem,
        const TThrottlerConfig& throttlerConfig);

    //
    // FileSystem
    //

public:
    const NProto::TFileSystem& GetFileSystem() const
    {
        return FileSystem;
    }

    TString GetFileSystemId() const
    {
        return FileSystem.GetFileSystemId();
    }

    ui32 GetGeneration() const
    {
        return Generation;
    }

    ui32 GetBlockSize() const
    {
        return FileSystem.GetBlockSize();
    }

    ui64 GetBlocksCount() const
    {
        return FileSystem.GetBlocksCount();
    }

    ui64 GetNodesCount() const
    {
        if (!FileSystem.GetNodesCount()) {
            return MaxNodes;
        }

        return FileSystem.GetNodesCount();
    }

    ui64 GetCurrentCommitId() const
    {
        return MakeCommitId(Generation, LastStep);
    }

    ui64 GenerateCommitId()
    {
        if (LastStep == Max<ui32>()) {
            return InvalidCommitId;
        }

        return MakeCommitId(Generation, ++LastStep);
    }

    ui64 GenerateEventId(TSession* session)
    {
        return MakeCommitId(Generation, ++session->LastEvent);
    }

    const NProto::TFileSystemStats& GetFileSystemStats() const
    {
        return FileSystemStats;
    }

    const NProto::TFileStorePerformanceProfile& GetPerformanceProfile() const;

    const TFileStoreAllocRegistry& GetFileStoreProfilingRegistry() const
    {
        return AllocatorRegistry;
    }

    IAllocator* GetAllocator(EAllocatorTag tag) const
    {
        return AllocatorRegistry.GetAllocator(tag);
    }

    //
    // FileSystem Stats
    //

public:
    void DumpStats(IOutputStream& os) const;

#define FILESTORE_DECLARE_COUNTER(name, ...)                                   \
public:                                                                        \
    ui64 Get##name() const                                                     \
    {                                                                          \
        return FileSystemStats.Get##name();                                    \
    }                                                                          \
private:                                                                       \
    void Set##name(TIndexTabletDatabase& db, ui64 value)                       \
    {                                                                          \
        FileSystemStats.Set##name(value);                                      \
        db.Write##name(value);                                                 \
    }                                                                          \
    ui64 Increment##name(TIndexTabletDatabase& db, size_t delta = 1)           \
    {                                                                          \
        ui64 value = SafeIncrement(FileSystemStats.Get##name(), delta);        \
        FileSystemStats.Set##name(value);                                      \
        db.Write##name(value);                                                 \
        return value;                                                          \
    }                                                                          \
    ui64 Decrement##name(TIndexTabletDatabase& db, size_t delta = 1)           \
    {                                                                          \
        ui64 value = SafeDecrement(FileSystemStats.Get##name(), delta);        \
        FileSystemStats.Set##name(value);                                      \
        db.Write##name(value);                                                 \
        return value;                                                          \
    }                                                                          \
// FILESTORE_DECLARE_COUNTER

FILESTORE_FILESYSTEM_STATS(FILESTORE_DECLARE_COUNTER)

#undef FILESTORE_DECLARE_COUNTER

    //
    // Throttling
    //

public:
    const TThrottlingPolicy& GetThrottlingPolicy() const;
    TThrottlingPolicy& AccessThrottlingPolicy();
    const TThrottlerConfig& GetThrottlingConfig() const;

    //
    // Channels
    //

public:
    ui64 GetTabletChannelCount() const;
    ui64 GetConfigChannelCount() const;

    TVector<ui32> GetChannels(EChannelDataKind kind) const;
    TVector<ui32> GetUnwritableChannels() const;
    TVector<ui32> GetChannelsToMove(ui32 percentageThreshold) const;

    void RegisterUnwritableChannel(ui32 channel);
    void RegisterChannelToMove(ui32 channel);

private:
    void LoadChannels();
    void UpdateChannels();

    //
    // Nodes
    //

public:
    ui64 CreateNode(
        TIndexTabletDatabase& db,
        ui64 commitId,
        const NProto::TNode& attrs);

    void UpdateNode(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        ui64 minCommitId,
        ui64 maxCommitId,
        const NProto::TNode& attrs,
        const NProto::TNode& prevAttrs);

    void RemoveNode(
        TIndexTabletDatabase& db,
        const TIndexTabletDatabase::TNode& node,
        ui64 minCommitId,
        ui64 maxCommitId);

    void UnlinkNode(
        TIndexTabletDatabase& db,
        ui64 parentNodeId,
        const TString& name,
        const TIndexTabletDatabase::TNode& node,
        ui64 minCommitId,
        ui64 maxCommitId);

    bool ReadNode(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        ui64 commitId,
        TMaybe<TIndexTabletDatabase::TNode>& node);

    void RewriteNode(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        ui64 minCommitId,
        ui64 maxCommitId,
        const NProto::TNode& attrs);

    bool HasSpaceLeft(
        const NProto::TNode& attrs,
        ui64 newSize) const;

    bool HasBlocksLeft(
        ui32 blocks) const;

private:
    void UpdateUsedBlocksCount(
        TIndexTabletDatabase& db,
        ui64 currentSize,
        ui64 prevSize);

    //
    // NodeAttrs
    //

public:
    ui64 CreateNodeAttr(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        ui64 commitId,
        const TString& name,
        const TString& value);

    ui64 UpdateNodeAttr(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        ui64 minCommitId,
        ui64 maxCommitId,
        const TIndexTabletDatabase::TNodeAttr& attr,
        const TString& newValue);

    void RemoveNodeAttr(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        ui64 minCommitId,
        ui64 maxCommitId,
        const TIndexTabletDatabase::TNodeAttr& attr);

    bool ReadNodeAttr(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        ui64 commitId,
        const TString& name,
        TMaybe<TIndexTabletDatabase::TNodeAttr>& attr);

    bool ReadNodeAttrs(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        ui64 commitId,
        TVector<TIndexTabletDatabase::TNodeAttr>& attrs);

    void RewriteNodeAttr(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        ui64 minCommitId,
        ui64 maxCommitId,
        const TIndexTabletDatabase::TNodeAttr& attr);

    //
    // NodeRefs
    //

public:
    void CreateNodeRef(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        ui64 commitId,
        const TString& childName,
        ui64 childNodeId);

    void RemoveNodeRef(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        ui64 minCommitId,
        ui64 maxCommitId,
        const TString& childName,
        ui64 prevChildNodeId);

    bool ReadNodeRef(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        ui64 commitId,
        const TString& name,
        TMaybe<TIndexTabletDatabase::TNodeRef>& ref);

    bool ReadNodeRefs(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        ui64 commitId,
        const TString& cookie,
        TVector<TIndexTabletDatabase::TNodeRef>& refs,
        ui32 maxBytes,
        TString* next = nullptr);

    bool PrechargeNodeRefs(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        const TString& cookie,
        ui32 bytesToPrecharge);

    void RewriteNodeRef(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        ui64 minCommitId,
        ui64 maxCommitId,
        const TString& childName,
        ui64 childNodeId);

    //
    // Sessions
    //

public:
    void LoadSessions(
        TInstant idleSessionDeadline,
        const TVector<NProto::TSession>& sessions,
        const TVector<NProto::TSessionHandle>& handles,
        const TVector<NProto::TSessionLock>& locks,
        const TVector<NProto::TDupCacheEntry>& cacheEntries,
        const TVector<NProto::TSessionHistoryEntry>& sessionsHistory);

    TSession* CreateSession(
        TIndexTabletDatabase& db,
        const TString& clientId,
        const TString& sessionId,
        const TString& checkpointId,
        const TString& originFqdn,
        ui64 seqNo,
        bool readOnly,
        const NActors::TActorId& owner);

    void RemoveSession(
        TIndexTabletDatabase& db,
        const TString& sessionId);

    TSession* FindSession(const TString& sessionId) const;
    TSession* FindSessionByClientId(const TString& clientId) const;
    TSession* FindSession(
        const TString& clientId,
        const TString& sessionId,
        ui64 SeqNo) const;

    NActors::TActorId RecoverSession(
        TSession* session,
        ui64 sessionSeqNo,
        bool readOnly,
        const NActors::TActorId& owner);
    void OrphanSession(const NActors::TActorId& owner, TInstant deadline);
    void ResetSession(TIndexTabletDatabase& db, TSession* session, const TMaybe<TString>& state);

    TVector<TSession*> GetTimeoutedSessions(TInstant now) const;
    TVector<TSession*> GetSessionsToNotify(const NProto::TSessionEvent& event) const;

    const TSessionHistoryList& GetSessionHistoryList() const;
    void PushSessionHistoryEntry(TIndexTabletDatabase& db, const TSessionHistoryEntry& entry, size_t maxEntriesCount = 100);

private:
    TSession* CreateSession(
        const NProto::TSession& proto,
        TInstant deadline);

    TSession* CreateSession(
        const NProto::TSession& proto,
        ui64 seqNo,
        bool readOnly,
        const NActors::TActorId& owner);

    void RemoveSession(TSession* session);

    //
    // Handles
    //

public:
    TSessionHandle* CreateHandle(
        TIndexTabletDatabase& db,
        TSession* session,
        ui64 nodeId,
        ui64 commitId,
        ui32 flags);

    void DestroyHandle(
        TIndexTabletDatabase& db,
        TSessionHandle* handle);

    TSessionHandle* FindHandle(ui64 handle) const;

    bool HasOpenHandles(ui64 nodeId) const;

private:
    ui64 GenerateHandle() const;

    TSessionHandle* CreateHandle(
        TSession* session,
        const NProto::TSessionHandle& proto);

    void RemoveHandle(TSessionHandle* handle);

    //
    // Locks
    //

public:
    TRangeLockOperationResult AcquireLock(
        TIndexTabletDatabase& db,
        TSession* session,
        ui64 handle,
        const TLockRange& range);

    TRangeLockOperationResult ReleaseLock(
        TIndexTabletDatabase& db,
        TSession* session,
        const TLockRange& range);

    TRangeLockOperationResult TestLock(
        TSession* session,
        const TSessionHandle* handle,
        const TLockRange& range) const;

    void ReleaseLocks(TIndexTabletDatabase& db, ui64 handle);

private:
    TSessionLock* FindLock(ui64 lockId) const;

    TRangeLockOperationResult CreateLock(
        TSession* session,
        const NProto::TSessionLock& proto,
        const TLockRange* range = nullptr);

    void RemoveLock(TSessionLock* lock);

    //
    // DupCache
    //

#define FILESTORE_DECLARE_DUPCACHE(name, ...)                                   \
public:                                                                         \
    void AddDupCacheEntry(                                                      \
        TIndexTabletDatabase& db,                                               \
        TSession* session,                                                      \
        ui64 requestId,                                                         \
        const NProto::T##name##Response& response,                              \
        ui32 maxEntries);                                                       \
                                                                                \
    void GetDupCacheEntry(                                                      \
        const TDupCacheEntry* entry,                                            \
        NProto::T##name##Response& response);                                   \
// FILESTORE_DECLARE_DUPCACHE

FILESTORE_DUPCACHE_REQUESTS(FILESTORE_DECLARE_DUPCACHE)

#undef FILESTORE_DECLARE_DUPCACHE

    void CommitDupCacheEntry(
        const TString& sessionId,
        ui64 requestId);

    //
    // Writes
    //

public:
    bool EnqueueWriteBatch(std::unique_ptr<TWriteRequest> request);
    TWriteRequestList DequeueWriteBatch();

    bool GenerateBlobId(
        ui64 commitId,
        ui32 blobSize,
        ui32 blobIndex,
        TPartialBlobId* blobId) const;

    struct TBackpressureThresholds
    {
        ui64 Flush;
        ui64 FlushBytes;
        ui64 CompactionScore;
        ui64 CleanupScore;

        TBackpressureThresholds(
                const ui64 flush,
                const ui64 flushBytes,
                const ui64 compactionScore,
                const ui64 cleanupScore)
            : Flush(flush)
            , FlushBytes(flushBytes)
            , CompactionScore(compactionScore)
            , CleanupScore(cleanupScore)
        {
        }
    };

    bool IsWriteAllowed(const TBackpressureThresholds& thresholds) const;

    //
    // FreshBytes
    //

public:
    void LoadFreshBytes(
        const TVector<TIndexTabletDatabase::TFreshBytesEntry>& bytes);

    void FindFreshBytes(
        IFreshBytesVisitor& visitor,
        ui64 nodeId,
        ui64 commitId,
        TByteRange byteRange) const;

    void WriteFreshBytes(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        ui64 commitId,
        ui64 offset,
        TStringBuf data);

    void WriteFreshBytesDeletionMarker(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        ui64 commitId,
        ui64 offset,
        ui64 len);

    TFlushBytesCleanupInfo StartFlushBytes(TVector<TBytes>* bytes);
    void FinishFlushBytes(
        TIndexTabletDatabase& db,
        ui64 chunkId,
        NProto::TProfileLogRequestInfo& profileLogRequest);

    //
    // FreshBlocks
    //

public:
    void LoadFreshBlocks(
        const TVector<TIndexTabletDatabase::TFreshBlock>& blocks);

    void FindFreshBlocks(IFreshBlockVisitor& visitor) const;

    void FindFreshBlocks(
        IFreshBlockVisitor& visitor,
        ui64 nodeId,
        ui64 commitId,
        ui32 blockIndex,
        ui32 blocksCount) const;

    TMaybe<TFreshBlock> FindFreshBlock(
        ui64 nodeId,
        ui64 commitId,
        ui32 blockIndex) const;

    void WriteFreshBlock(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        ui64 commitId,
        ui32 blockIndex,
        TStringBuf blockData);

    void MarkFreshBlocksDeleted(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        ui64 commitId,
        ui32 blockIndex,
        ui32 blocksCount);

    void DeleteFreshBlocks(
        TIndexTabletDatabase& db,
        const TVector<TBlock>& blocks);

    //
    // MixedBlocks
    //

public:
    bool LoadMixedBlocks(TIndexTabletDatabase& db, ui32 rangeId);
    void ReleaseMixedBlocks(ui32 rangeId);
    void ReleaseMixedBlocks(const TSet<ui32>& ranges);

    void FindMixedBlocks(
        IMixedBlockVisitor& visitor,
        ui64 nodeId,
        ui64 commitId,
        ui32 blockIndex,
        ui32 blocksCount) const;

    void WriteMixedBlocks(
        TIndexTabletDatabase& db,
        const TPartialBlobId& blobId,
        const TBlock& block,
        ui32 blocksCount);

    bool WriteMixedBlocks(
        TIndexTabletDatabase& db,
        const TPartialBlobId& blobId,
        /*const*/ TVector<TBlock>& blocks);

    void DeleteMixedBlocks(
        TIndexTabletDatabase& db,
        const TPartialBlobId& blobId,
        const TVector<TBlock>& blocks);

    TVector<TMixedBlobMeta> GetBlobsForCompaction(ui32 rangeId) const;

    TMixedBlobMeta FindBlob(ui32 rangeId, TPartialBlobId blobId) const;

    void MarkMixedBlocksDeleted(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        ui64 commitId,
        ui32 blockIndex,
        ui32 blocksCount);

    void CleanupMixedBlockDeletions(
        TIndexTabletDatabase& db,
        ui32 rangeId,
        NProto::TProfileLogRequestInfo& profileLogRequest);

    void UpdateBlockLists(
        TIndexTabletDatabase& db,
        TMixedBlobMeta& blob);

    void RewriteMixedBlocks(
        TIndexTabletDatabase& db,
        ui32 rangeId,
        /*const*/ TMixedBlobMeta& blob,
        const TMixedBlobStats& blobStats);

    ui32 GetMixedRangeIndex(ui64 nodeId, ui32 blockIndex) const;
    ui32 GetMixedRangeIndex(ui64 nodeId, ui32 blockIndex, ui32 blocksCount) const;
    ui32 GetMixedRangeIndex(const TVector<TBlock>& blocks) const;
    const IBlockLocation2RangeIndex& GetRangeIdHasher() const;

private:
    bool WriteMixedBlocks(
        TIndexTabletDatabase& db,
        ui32 rangeId,
        const TPartialBlobId& blobId,
        /*const*/ TVector<TBlock>& blocks);

    void DeleteMixedBlocks(
        TIndexTabletDatabase& db,
        ui32 rangeId,
        const TPartialBlobId& blobId,
        const TVector<TBlock>& blocks);

    //
    // Garbage
    //

public:
    ui32 NextCollectCounter()
    {
        return ++LastCollectCounter;
    }

    void SetStartupGcExecuted()
    {
        StartupGcExecuted = true;
    }

    bool GetStartupGcExecuted() const
    {
        return StartupGcExecuted;
    }

    void AcquireCollectBarrier(ui64 commitId);
    void ReleaseCollectBarrier(ui64 commitId);

    ui64 GetCollectCommitId() const;

    void LoadGarbage(
        const TVector<TPartialBlobId>& newBlobs,
        const TVector<TPartialBlobId>& garbageBlobs);

    TVector<TPartialBlobId> GetNewBlobs(ui64 collectCommitId) const;
    TVector<TPartialBlobId> GetGarbageBlobs(ui64 collectCommitId) const;

    void DeleteGarbage(
        TIndexTabletDatabase& db,
        ui64 collectCommitId,
        const TVector<TPartialBlobId>& newBlobs,
        const TVector<TPartialBlobId>& garbageBlobs);

private:
    void AddNewBlob(TIndexTabletDatabase& db, const TPartialBlobId& blobId);
    void AddGarbageBlob(TIndexTabletDatabase& db, const TPartialBlobId& blobId);

    //
    // Checkpoints
    //

public:
    void LoadCheckpoints(const TVector<NProto::TCheckpoint>& checkpoints);

    TVector<TCheckpoint*> GetCheckpoints() const;

    TCheckpoint* FindCheckpoint(const TString& checkpointId) const;

    ui64 GetReadCommitId(const TString& checkpointId) const;

    TCheckpoint* CreateCheckpoint(
        TIndexTabletDatabase& db,
        const TString& checkpointId,
        ui64 nodeId,
        ui64 commitId);

    void MarkCheckpointDeleted(
        TIndexTabletDatabase& db,
        TCheckpoint* checkpoint);

    void RemoveCheckpointNodes(
        TIndexTabletDatabase& db,
        TCheckpoint* checkpoint,
        const TVector<ui64>& nodes);

    void RemoveCheckpointBlob(
        TIndexTabletDatabase& db,
        TCheckpoint* checkpoint,
        ui32 rangeId,
        const TPartialBlobId& blobId);

    void RemoveCheckpoint(
        TIndexTabletDatabase& db,
        TCheckpoint* checkpoint);

private:
    void AddCheckpointNode(
        TIndexTabletDatabase& db,
        ui64 checkpointId,
        ui64 nodeId);

    void AddCheckpointBlob(
        TIndexTabletDatabase& db,
        ui64 checkpointId,
        ui32 rangeId,
        const TPartialBlobId& blobId);

    //
    // Background operations
    //

public:
    TOperationState FlushState;
    TOperationState BlobIndexOpState;
    TOperationState CollectGarbageState;

    TBlobIndexOpQueue BlobIndexOps;

    //
    // Compaction map
    //

public:
    void UpdateCompactionMap(ui32 rangeId, ui32 blobsCount, ui32 deletionsCount);

    TCompactionStats GetCompactionStats(ui32 rangeId) const;
    TCompactionCounter GetRangeToCompact() const;
    TCompactionCounter GetRangeToCleanup() const;

    TCompactionMapStats GetCompactionMapStats(ui32 topSize) const;

    TVector<ui32> GetNonEmptyCompactionRanges() const;
    TVector<TCompactionRangeInfo> GetTopRangesByCompactionScore(ui32 topSize) const;
    TVector<TCompactionRangeInfo> GetTopRangesByCleanupScore(ui32 topSize) const;

    void LoadCompactionMap(const TVector<TCompactionRangeInfo>& compactionMap);

    //
    // Forced Compaction
    //

public:
    struct TForcedCompactionState
    {
        const TVector<ui32> RangesToCompact;

        TInstant StartTime = TInstant::Now();
        std::atomic<ui32> Current = 0;

        TForcedCompactionState(TVector<ui32> ranges)
            : RangesToCompact(std::move(ranges))
        {}

        bool Progress()
        {
            return ++Current < RangesToCompact.size();
        }

        ui32 GetCurrentRange() const
        {
            return RangesToCompact[Current.load()];
        }
    };

    using TForcedCompactionStatePtr = std::shared_ptr<TForcedCompactionState>;

private:
    TVector<TVector<ui32>> PendingForcedCompactions;
    TForcedCompactionStatePtr ForcedCompactionState;

public:
    void EnqueueForcedCompaction(TVector<ui32> ranges);
    TVector<ui32> DequeueForcedCompaction();

    void StartForcedCompaction(TVector<ui32> ranges);
    void CompleteForcedCompaction();

    const TForcedCompactionStatePtr& GetForcedCompactionState() const
    {
        return ForcedCompactionState;
    }

    bool IsForcedCompactionRunning() const
    {
        return (bool)ForcedCompactionState;
    }

    //
    // Truncate operations
    //

public:
    void EnqueueTruncateOp(ui64 nodeId, TByteRange range);
    TTruncateQueue::TEntry DequeueTruncateOp();
    bool HasPendingTruncateOps() const;

    void CompleteTruncateOp(ui64 nodeId);
    bool HasActiveTruncateOp(ui64 nodeId) const;

    void AddTruncate(TIndexTabletDatabase& db, ui64 nodeId, TByteRange range);
    void DeleteTruncate(TIndexTabletDatabase& db, ui64 nodeId);

    void Truncate(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        ui64 commitId,
        ui64 currentSize,
        ui64 targetSize);

    // Call this function only as a part of Truncate(...). The node size must be
    // changed after this call (this function guarantees that range will be
    // completely deleted). This function:
    // - aligns up range in the tail;
    // - deletes all blocks in NEW range;
    // - writes fresh bytes (zeroes) on unaligned head, if range.Offset != 0.
    void TruncateRange(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        ui64 commitId,
        TByteRange range);

    // Call this function only if you need to zero range without further
    // resizing the node. This function:
    // - writes fresh bytes (zeroes) on unaligned head, if any;
    // - writes fresh bytes (zeroes) on unaligned tail, if any.
    void ZeroRange(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        ui64 commitId,
        TByteRange range);

private:
    void DeleteRange(
        TIndexTabletDatabase& db,
        ui64 nodeId,
        ui64 commitId,
        const TByteRange& range);
};

}   // namespace NCloud::NFileStore::NStorage
