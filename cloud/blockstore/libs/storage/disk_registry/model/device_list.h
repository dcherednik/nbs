#pragma once

#include "public.h"

#include <cloud/blockstore/libs/storage/protos/disk.pb.h>
#include <cloud/storage/core/libs/common/error.h>

#include <util/datetime/base.h>
#include <util/generic/hash_set.h>
#include <util/generic/hash.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

namespace NCloud::NBlockStore::NStorage {

////////////////////////////////////////////////////////////////////////////////

class TDeviceList
{
    using TDeviceId = TString;
    using TDiskId = TString;
    using TNodeId = ui32;

    struct TNodeInfo
    {
        TNodeId NodeId = 0;
        ui64 FreeSpace = 0;
    };

    struct TRack
    {
        TString Id;
        TVector<TNodeInfo> Nodes;
        ui64 FreeSpace = 0;
        bool Preferred = false;
    };

    struct TFreeDevices
    {
        TString Rack;

        // sorted by {PoolKind, BlockSize}
        TVector<NProto::TDeviceConfig> Devices;
    };

    using TDeviceRange = std::tuple<
        TNodeId,
        TVector<NProto::TDeviceConfig>::const_iterator,
        TVector<NProto::TDeviceConfig>::const_iterator>;

private:
    THashMap<TDeviceId, NProto::TDeviceConfig> AllDevices;
    THashMap<TNodeId, TFreeDevices> FreeDevices;
    THashMap<TDeviceId, TDiskId> AllocatedDevices;
    THashSet<TDeviceId> DirtyDevices;
    THashMap<TDeviceId, NProto::TSuspendedDevice> SuspendedDevices;
    THashMap<NProto::EDevicePoolKind, TVector<TString>> PoolKind2PoolNames;

public:
    struct TAllocationQuery
    {
        THashSet<TString> ForbiddenRacks;
        THashSet<TString> PreferredRacks;

        ui32 LogicalBlockSize = 0;
        ui64 BlockCount = 0;

        TString PoolName;
        NProto::EDevicePoolKind PoolKind = NProto::DEVICE_POOL_KIND_DEFAULT;
        THashSet<ui32> NodeIds;

        ui64 GetTotalByteCount() const
        {
            return LogicalBlockSize * BlockCount;
        }
    };

    explicit TDeviceList(
        TVector<TDeviceId> dirtyDevices,
        TVector<NProto::TSuspendedDevice> suspendedDevices);

    size_t Size() const
    {
        return AllDevices.size();
    }

    void UpdateDevices(const NProto::TAgentConfig& agent, TNodeId prevNodeId);
    void UpdateDevices(const NProto::TAgentConfig& agent);
    void RemoveDevices(const NProto::TAgentConfig& agent);

    TNodeId FindNodeId(const TDeviceId& id) const;
    TString FindAgentId(const TDeviceId& id) const;

    TString FindRack(const TDeviceId& id) const;
    TDiskId FindDiskId(const TDeviceId& id) const;
    const NProto::TDeviceConfig* FindDevice(const TDeviceId& id) const;

    TVector<NProto::TDeviceConfig> GetBrokenDevices() const;
    TVector<NProto::TDeviceConfig> GetDirtyDevices() const;

    NProto::TDeviceConfig AllocateDevice(
        const TDiskId& diskId,
        const TAllocationQuery& query);
    TResultOrError<NProto::TDeviceConfig> AllocateSpecificDevice(
        const TDiskId& diskId,
        const TDeviceId& deviceId,
        const TAllocationQuery& query);
    bool IsAllocatedDevice(const TDeviceId& id) const;
    bool ValidateAllocationQuery(
        const TAllocationQuery& query,
        const TDeviceId& targetDeviceId);

    void MarkDeviceAllocated(const TDiskId& diskId, const TDeviceId& id);
    bool ReleaseDevice(const TDeviceId& id);

    TVector<NProto::TDeviceConfig> AllocateDevices(
        const TDiskId& diskId,
        const TAllocationQuery& query);

    bool CanAllocateDevices(const TAllocationQuery& query);

    bool MarkDeviceAsClean(const TDeviceId& uuid);
    void MarkDeviceAsDirty(const TDeviceId& uuid);

    bool IsDirtyDevice(const TDeviceId& uuid) const;
    NProto::EDeviceState GetDeviceState(const TDeviceId& uuid) const;

    void SuspendDevice(const TDeviceId& ids);
    bool ResumeDevice(const TDeviceId& id);
    bool IsSuspendedDevice(const TDeviceId& id) const;
    TVector<NProto::TSuspendedDevice> GetSuspendedDevices() const;

    ui64 GetDeviceByteCount(const TDeviceId& id) const;

private:
    TVector<TDeviceRange> CollectDevices(
        const TAllocationQuery& query,
        const TString& poolName);
    TVector<TDeviceRange> CollectDevices(const TAllocationQuery& query);
    TVector<TRack> SelectRacks(
        const TAllocationQuery& query,
        const TString& poolName) const;
    void RemoveDeviceFromFreeList(const TDeviceId& id);
};

////////////////////////////////////////////////////////////////////////////////

TVector<NProto::TDeviceConfig> FilterDevices(
    TVector<NProto::TDeviceConfig>& dirtyDevices,
    ui32 maxPerDeviceNameForDefaultPoolKind,
    ui32 maxPerDeviceNameForLocalPoolKind,
    ui32 maxPerDeviceNameForGlobalPoolKind);

}   // namespace NCloud::NBlockStore::NStorage
