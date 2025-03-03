#include "tablet_state_impl.h"

#include <library/cpp/actors/core/actor.h>
#include <library/cpp/actors/core/log.h>

#include <util/random/random.h>

namespace NCloud::NFileStore::NStorage {

using namespace NActors;

namespace {

void Dump(
    IOutputStream& out,
    const TString& tag,
    TStringBuf operation,
    const TSession* session,
    size_t locksRemovedNumber,
    const TLockRange& range)
{
    out << "LockInfo " << tag << " " << operation
        << ": ClientId=" << session->GetClientId()
        << ", SessionId=" << session->GetSessionId()
        << ", LocksRemovedNumber=" << locksRemovedNumber
        << ", LockRange= " << range;
}

NProto::TSessionLock MakeSessionLock(
    ui64 handle,
    const TLockRange& range,
    const TString& sessionId,
    ui64 lockId)
{
    NProto::TSessionLock proto;
    proto.SetSessionId(sessionId);
    proto.SetLockId(lockId);
    proto.SetHandle(handle);
    proto.SetNodeId(range.NodeId);
    proto.SetOwner(range.OwnerId);
    proto.SetOffset(range.Offset);
    proto.SetLength(range.Length);
    proto.SetPid(range.Pid);
    proto.SetMode(static_cast<ui32>(range.LockMode));
    proto.SetLockOrigin(ConvertTo<NProto::ELockOrigin>(range.LockOrigin));
    return proto;
}

}   // namespace

////////////////////////////////////////////////////////////////////////////////
// Sessions

void TIndexTabletState::LoadSessions(
    TInstant idleSessionDeadline,
    const TVector<NProto::TSession>& sessions,
    const TVector<NProto::TSessionHandle>& handles,
    const TVector<NProto::TSessionLock>& locks,
    const TVector<NProto::TDupCacheEntry>& cacheEntries,
    const TVector<NProto::TSessionHistoryEntry>& sessionsHistory)
{
    for (const auto& proto: sessions) {
        LOG_INFO(*TlsActivationContext, TFileStoreComponents::TABLET,
            "%s restoring session c: %s, s: %s n: %lu m: %lu l: %lu",
            LogTag.c_str(),
            proto.GetClientId().c_str(),
            proto.GetSessionId().c_str(),
            proto.GetMaxSeqNo(),
            proto.GetMaxRwSeqNo(),
            proto.GetSessionState().size());

        auto* session = CreateSession(proto, idleSessionDeadline);
        TABLET_VERIFY(session);
    }

    for (const auto& proto: handles) {
        auto* session = FindSession(proto.GetSessionId());
        TABLET_VERIFY_C(session, "no session for " << proto.ShortDebugString());


        auto* handle = CreateHandle(session, proto);
        TABLET_VERIFY_C(handle, "failed to create handle " << proto.ShortDebugString());
    }

    for (const auto& proto: locks) {
        auto* session = FindSession(proto.GetSessionId());
        TABLET_VERIFY_C(session, "no session for " << proto.ShortDebugString());

        auto result = CreateLock(session, proto);
        TABLET_VERIFY_C(
            result.Succeeded(),
            "failed to acquire lock " << proto.ShortDebugString()
                                      << "with error: "
                                      << result.Error.GetMessage());
        TABLET_VERIFY_C(
            result.RemovedLockIds().empty(),
            "non empty removed locks " << proto.ShortDebugString());
    }

    TSession* session = nullptr;
    for (const auto& entry: cacheEntries) {
        if (!session || session->GetSessionId() != entry.GetSessionId()) {
            session = FindSession(entry.GetSessionId());
            TABLET_VERIFY_C(session, "no session for dup cache entry "
                << entry.ShortDebugString().c_str());
        }

        session->LoadDupCacheEntry(entry);
    }

    for (const auto& entry: sessionsHistory) {
        Impl->SessionHistoryList.push_back(entry);
        TSessionHistoryEntry::UpdateMaxEntryId(entry.GetEntryId());
    }
}

TSession* TIndexTabletState::CreateSession(
    TIndexTabletDatabase& db,
    const TString& clientId,
    const TString& sessionId,
    const TString& checkpointId,
    const TString& originFqdn,
    ui64 seqNo,
    bool readOnly,
    const TActorId& owner)
{
    LOG_INFO(*TlsActivationContext, TFileStoreComponents::TABLET,
        "%s creating session c: %s, s: %s",
        LogTag.c_str(),
        clientId.c_str(),
        sessionId.c_str());

    NProto::TSession proto;
    proto.SetClientId(clientId);
    proto.SetSessionId(sessionId);
    proto.SetCheckpointId(checkpointId);
    proto.SetOriginFqdn(originFqdn);
    proto.SetMaxSeqNo(seqNo);
    if (!readOnly) {
        proto.SetMaxRwSeqNo(seqNo);
    }

    db.WriteSession(proto);
    PushSessionHistoryEntry(
        db,
        TSessionHistoryEntry(proto, TSessionHistoryEntry::CREATE),
        SessionHistoryEntryCount);
    IncrementUsedSessionsCount(db);

    auto* session = CreateSession(proto, seqNo, readOnly, owner);
    TABLET_VERIFY(session);

    return session;
}

TSession* TIndexTabletState::CreateSession(
    const NProto::TSession& proto,
    TInstant deadline)
{
    auto session = std::make_unique<TSession>(proto);
    session->Deadline = deadline;

    Impl->OrphanSessions.PushBack(session.get());
    Impl->SessionById.emplace(session->GetSessionId(), session.get());
    Impl->SessionByClient.emplace(session->GetClientId(), session.get());

    return session.release();
}

TSession* TIndexTabletState::CreateSession(
    const NProto::TSession& proto,
    ui64 seqNo,
    bool readOnly,
    const TActorId& owner)
{
    auto session = std::make_unique<TSession>(proto);
    session->UpdateSubSession(seqNo, readOnly, owner);

    Impl->Sessions.PushBack(session.get());
    Impl->SessionById.emplace(session->GetSessionId(), session.get());
    Impl->SessionByOwner.emplace(owner, session.get());
    Impl->SessionByClient.emplace(session->GetClientId(), session.get());

    return session.release();
}

NActors::TActorId TIndexTabletState::RecoverSession(
    TSession* session,
    ui64 sessionSeqNo,
    bool readOnly,
    const TActorId& owner)
{
    auto oldOwner =
        session->UpdateSubSession(sessionSeqNo, readOnly, owner);
    if (oldOwner) {
        Impl->SessionByOwner.erase(oldOwner);
    }

    if (oldOwner != owner) {
        session->Deadline = {};

        session->Unlink();
        Impl->Sessions.PushBack(session);

        Impl->SessionByOwner.emplace(owner, session);
    }

    return oldOwner;
}

TSession* TIndexTabletState::FindSession(const TString& sessionId) const
{
    auto it = Impl->SessionById.find(sessionId);
    if (it != Impl->SessionById.end()) {
        return it->second;
    }

    return nullptr;
}

TSession* TIndexTabletState::FindSessionByClientId(const TString& clientId) const
{
    auto it = Impl->SessionByClient.find(clientId);
    if (it != Impl->SessionByClient.end()) {
        return it->second;
    }

    return nullptr;
}

TSession* TIndexTabletState::FindSession(
    const TString& clientId,
    const TString& sessionId,
    ui64 seqNo) const
{
    auto session = FindSession(sessionId);
    if (session &&
        session->IsValid() &&
        session->GetClientId() == clientId &&
        session->HasSeqNo(seqNo))
    {
        return session;
    }

    return nullptr;
}

void TIndexTabletState::OrphanSession(const TActorId& owner, TInstant deadline)
{
    auto it = Impl->SessionByOwner.find(owner);
    if (it == Impl->SessionByOwner.end()) {
        return; // not a session pipe
    }

    auto* session = it->second;

    LOG_INFO(*TlsActivationContext, TFileStoreComponents::TABLET,
        "%s orphaning session c: %s, s: %s",
        LogTag.c_str(),
        session->GetClientId().c_str(),
        session->GetSessionId().c_str());

    if (!session->DeleteSubSession(owner)) {
        session->Deadline = deadline;

        session->Unlink();
        Impl->OrphanSessions.PushBack(session);

        Impl->SessionByOwner.erase(it);
    }
}

void TIndexTabletState::ResetSession(
    TIndexTabletDatabase& db,
    TSession* session,
    const TMaybe<TString>& state)
{
    LOG_INFO(*TlsActivationContext, TFileStoreComponents::TABLET,
        "%s resetting session c: %s, s: %s",
        LogTag.c_str(),
        session->GetClientId().c_str(),
        session->GetSessionId().c_str());

    auto handle = session->Handles.begin();
    while (handle != session->Handles.end()) {
        DestroyHandle(db, &*(handle++));
    }

    auto lock = session->Locks.begin();
    while (lock != session->Locks.end()) {
        auto& cur = *(lock++);
        auto range = MakeLockRange(cur, cur.GetNodeId());
        ReleaseLock(db, session, range);
    }

    while (auto entryId = session->PopDupCacheEntry(0)) {
        db.DeleteSessionDupCacheEntry(session->GetSessionId(), entryId);
    }

    if (state) {
        session->SetSessionState(*state);
        db.WriteSession(*session);
        PushSessionHistoryEntry(
            db,
            TSessionHistoryEntry(*session, TSessionHistoryEntry::RESET),
            SessionHistoryEntryCount);
    }
}

void TIndexTabletState::RemoveSession(
    TIndexTabletDatabase& db,
    const TString& sessionId)
{
    auto* session = FindSession(sessionId);
    TABLET_VERIFY(session);

    // no need to update state before session deletion
    ResetSession(db, session, {});

    db.DeleteSession(sessionId);
    PushSessionHistoryEntry(
        db,
        TSessionHistoryEntry(*session, TSessionHistoryEntry::DELETE),
        SessionHistoryEntryCount);

    DecrementUsedSessionsCount(db);

    RemoveSession(session);
}

void TIndexTabletState::RemoveSession(TSession* session)
{
    LOG_INFO(*TlsActivationContext, TFileStoreComponents::TABLET,
        "%s removing session c: %s, s: %s",
        LogTag.c_str(),
        session->GetClientId().c_str(),
        session->GetSessionId().c_str());

    for (const auto& s: session->GetSubSessions()) {
        Impl->SessionByOwner.erase(s);
    }

    std::unique_ptr<TSession> holder(session);
    session->Unlink();

    Impl->SessionById.erase(session->GetSessionId());
    Impl->SessionByClient.erase(session->GetClientId());
}

TVector<TSession*> TIndexTabletState::GetTimeoutedSessions(TInstant now) const
{
    TVector<TSession*> result;
    for (auto& session: Impl->OrphanSessions) {
        if (session.Deadline < now) {
            result.push_back(&session);
        } else {
            break;
        }
    }

    return result;
}

TVector<TSession*> TIndexTabletState::GetSessionsToNotify(
    const NProto::TSessionEvent& event) const
{
    // TODO
    Y_UNUSED(event);

    TVector<TSession*> result;
    for (auto& session: Impl->Sessions) {
        if (session.NotifyEvents) {
            result.push_back(&session);
        }
    }

    return result;
}

const TSessionHistoryList& TIndexTabletState::GetSessionHistoryList() const {
    return Impl->SessionHistoryList;
}

void TIndexTabletState::PushSessionHistoryEntry(
    TIndexTabletDatabase& db,
    const TSessionHistoryEntry& entry,
    size_t maxEntriesCount)
{
    Impl->SessionHistoryList.push_back(entry);
    db.WriteSessionHistoryEntry(entry);
    ui64 entryToDelete = 0;
    if (Impl->SessionHistoryList.size() > maxEntriesCount) {
        entryToDelete = Impl->SessionHistoryList.front().GetEntryId();
        Impl->SessionHistoryList.pop_front();
    }
    if (entryToDelete) {
        db.DeleteSessionHistoryEntry(entryToDelete);
    }
}

////////////////////////////////////////////////////////////////////////////////
// Handles

ui64 TIndexTabletState::GenerateHandle() const
{
    ui64 h;
    do {
        h = RandomNumber<ui64>();
    } while (!h || Impl->HandleById.contains(h));

    return h;
}

TSessionHandle* TIndexTabletState::CreateHandle(
    TSession* session,
    const NProto::TSessionHandle& proto)
{
    auto handle = std::make_unique<TSessionHandle>(session, proto);

    session->Handles.PushBack(handle.get());
    Impl->HandleById.emplace(handle->GetHandle(), handle.get());
    Impl->NodeRefsByHandle[proto.GetNodeId()]++;

    return handle.release();
}

void TIndexTabletState::RemoveHandle(TSessionHandle* handle)
{
    std::unique_ptr<TSessionHandle> holder(handle);

    handle->Unlink();
    Impl->HandleById.erase(handle->GetHandle());

    auto it = Impl->NodeRefsByHandle.find(handle->GetNodeId());
    TABLET_VERIFY(it != Impl->NodeRefsByHandle.end());
    TABLET_VERIFY(it->second > 0);

    if (--(it->second) == 0) {
        Impl->NodeRefsByHandle.erase(it);
    }
}

TSessionHandle* TIndexTabletState::FindHandle(ui64 handle) const
{
    auto it = Impl->HandleById.find(handle);
    if (it != Impl->HandleById.end()) {
        return it->second;
    }

    return nullptr;
}

TSessionHandle* TIndexTabletState::CreateHandle(
    TIndexTabletDatabase& db,
    TSession* session,
    ui64 nodeId,
    ui64 commitId,
    ui32 flags)
{
    ui64 handleId = GenerateHandle();

    NProto::TSessionHandle proto;
    proto.SetSessionId(session->GetSessionId());
    proto.SetHandle(handleId);
    proto.SetNodeId(nodeId);
    proto.SetCommitId(commitId);
    proto.SetFlags(flags);

    db.WriteSessionHandle(proto);
    IncrementUsedHandlesCount(db);

    return CreateHandle(session, proto);
}

void TIndexTabletState::DestroyHandle(
    TIndexTabletDatabase& db,
    TSessionHandle* handle)
{
    db.DeleteSessionHandle(
        handle->GetSessionId(),
        handle->GetHandle());

    DecrementUsedHandlesCount(db);

    ReleaseLocks(db, handle->GetHandle());

    RemoveHandle(handle);
}

bool TIndexTabletState::HasOpenHandles(ui64 nodeId) const
{
    auto it = Impl->NodeRefsByHandle.find(nodeId);
    if (it != Impl->NodeRefsByHandle.end()) {
        TABLET_VERIFY(it->second > 0);
        return true;
    }

    return false;
}

////////////////////////////////////////////////////////////////////////////////
// Locks

TSessionLock* TIndexTabletState::FindLock(ui64 lockId) const
{
    auto it = Impl->LockById.find(lockId);
    if (it != Impl->LockById.end()) {
        return it->second;
    }

    return nullptr;
}

TRangeLockOperationResult TIndexTabletState::CreateLock(
    TSession* session,
    const NProto::TSessionLock& proto,
    const TLockRange* range)
{
    auto lock = std::make_unique<TSessionLock>(session, proto);

    session->Locks.PushBack(lock.get());
    Impl->LockById.emplace(lock->GetLockId(), lock.get());
    Impl->LocksByHandle.emplace(lock->GetHandle(), lock.get());

    const auto& rangeRef = range ? *range : ConvertTo<TLockRange>(proto);

    auto result = Impl->RangeLocks.Acquire(
        session->GetSessionId(),
        proto.GetLockId(),
        rangeRef);

    TABLET_VERIFY(lock.release());
    return result;
}

void TIndexTabletState::RemoveLock(TSessionLock* lock)
{
    std::unique_ptr<TSessionLock> holder(lock);

    lock->Unlink();
    Impl->LockById.erase(lock->GetLockId());

    auto [it, end] = Impl->LocksByHandle.equal_range(lock->GetHandle());
    it = std::find(it, end, std::pair<const ui64, TSessionLock*>(lock->GetHandle(), lock));
    TABLET_VERIFY_C(it != end, "failed to find lock by handle: " << lock->ShortDebugString());

    Impl->LocksByHandle.erase(it);
}

TRangeLockOperationResult TIndexTabletState::AcquireLock(
    TIndexTabletDatabase& db,
    TSession* session,
    ui64 handle,
    const TLockRange& range)
{
    const auto& sessionId = session->GetSessionId();

    ui64 lockId = IncrementLastLockId(db);

    auto proto = MakeSessionLock(handle, range, sessionId, lockId);

    IncrementUsedLocksCount(db);
    db.WriteSessionLock(proto);

    auto result = CreateLock(session, proto, &range);
    if (result.Failed()) {
        LOG_DEBUG(
            *TlsActivationContext,
            TFileStoreComponents::TABLET,
            result.Error.GetMessage());
        return result;
    }
    const TVector<ui64>& removedLocks = result.RemovedLockIds();

    TStringStream out;
    Dump(out, LogTag, "acquire", session, removedLocks.size(), range);
    LOG_DEBUG(*TlsActivationContext, TFileStoreComponents::TABLET, out.Str());

    for (ui64 removedLockId: removedLocks) {
        auto* removedLock = FindLock(removedLockId);
        TABLET_VERIFY(removedLock && removedLock->Session == session);

        db.DeleteSessionLock(sessionId, removedLockId);
        RemoveLock(removedLock);
    }

    DecrementUsedLocksCount(db, removedLocks.size());
    return result;
}

TRangeLockOperationResult TIndexTabletState::ReleaseLock(
    TIndexTabletDatabase& db,
    TSession* session,
    const TLockRange& range)
{
    const auto& sessionId = session->GetSessionId();

    auto result = Impl->RangeLocks.Release(sessionId, range);
    if (result.Failed()) {
        LOG_DEBUG(
            *TlsActivationContext,
            TFileStoreComponents::TABLET,
            result.Error.GetMessage());

        return result;
    }

    const TVector<ui64>& removedLocks = result.RemovedLockIds();
    if (removedLocks.empty()) {
        LOG_DEBUG(
            *TlsActivationContext,
            TFileStoreComponents::TABLET,
            "Failed to remove any locked range, will be treated as success");
    }

    TStringStream out;
    Dump(out, LogTag, "release", session, removedLocks.size(), range);
    LOG_DEBUG(*TlsActivationContext, TFileStoreComponents::TABLET, out.Str());

    for (ui64 removedLockId: removedLocks) {
        auto* removedLock = FindLock(removedLockId);
        TABLET_VERIFY(removedLock && removedLock->Session == session);

        db.DeleteSessionLock(sessionId, removedLockId);
        RemoveLock(removedLock);
    }

    DecrementUsedLocksCount(db, removedLocks.size());
    return result;
}

TRangeLockOperationResult TIndexTabletState::TestLock(
    TSession* session,
    const TSessionHandle* handle,
    const TLockRange& range) const
{
    if (!IsLockingAllowed(handle, range)) {
        return TRangeLockOperationResult(
            ErrorIncompatibleFileOpenMode(),
            range.LockMode);
    }
    return Impl->RangeLocks.Test(session->GetSessionId(), range);
}

void TIndexTabletState::ReleaseLocks(
    TIndexTabletDatabase& db,
    ui64 handle)
{
    TSmallVec<TSessionLock*> locks;
    auto [it, end] = Impl->LocksByHandle.equal_range(handle);
    for (; it != end; ++it) {
        locks.push_back(it->second);
    }

    for (const auto *lock: locks) {
        auto range = MakeLockRange(*lock, lock->GetNodeId());
        ReleaseLock(db, lock->Session, range);
    }
}

#define FILESTORE_IMPLEMENT_DUPCACHE(name, ...)                                \
void TIndexTabletState::AddDupCacheEntry(                                      \
    TIndexTabletDatabase& db,                                                  \
    TSession* session,                                                         \
    ui64 requestId,                                                            \
    const NProto::T##name##Response& response,                                 \
    ui32 maxEntries)                                                           \
{                                                                              \
    if (!requestId || !maxEntries) {                                           \
        return;                                                                \
    }                                                                          \
                                                                               \
    NProto::TDupCacheEntry entry;                                              \
    entry.SetSessionId(session->GetSessionId());                               \
    entry.SetEntryId(session->LastDupCacheEntryId++);                          \
    entry.SetRequestId(requestId);                                             \
    *entry.Mutable##name() = response;                                         \
                                                                               \
    db.WriteSessionDupCacheEntry(entry);                                       \
    session->AddDupCacheEntry(std::move(entry), false);                        \
                                                                               \
    while (auto entryId = session->PopDupCacheEntry(maxEntries)) {             \
        db.DeleteSessionDupCacheEntry(session->GetSessionId(), entryId);       \
    }                                                                          \
}                                                                              \
                                                                               \
void TIndexTabletState::GetDupCacheEntry(                                      \
    const TDupCacheEntry* entry,                                               \
    NProto::T##name##Response& response)                                       \
{                                                                              \
    if (entry->Commited && entry->Has##name()) {                               \
        response = entry->Get##name();                                         \
    } else if (!entry->Commited) {                                             \
        *response.MutableError() = ErrorDuplicate();                           \
    } else if (!entry->Has##name()) {                                          \
        *response.MutableError() = MakeError(                                  \
            E_ARGUMENT, "invalid request dup cache type");                     \
    }                                                                          \
}                                                                              \
// FILESTORE_IMPLEMENT_DUPCACHE

FILESTORE_DUPCACHE_REQUESTS(FILESTORE_IMPLEMENT_DUPCACHE)

#undef FILESTORE_IMPLEMENT_DUPCACHE

void TIndexTabletState::CommitDupCacheEntry(
    const TString& sessionId,
    ui64 requestId)
{
    if (auto session = FindSession(sessionId)) {
        session->CommitDupCacheEntry(requestId);
    }
}

}   // namespace NCloud::NFileStore::NStorage
