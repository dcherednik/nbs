#pragma once

#include "public.h"

#include "rdma_target.h"
#include "storage_with_stats.h"

#include <cloud/blockstore/libs/common/public.h>
#include <cloud/blockstore/libs/diagnostics/public.h>
#include <cloud/blockstore/libs/nvme/public.h>
#include <cloud/blockstore/libs/rdma/iface/public.h>
#include <cloud/blockstore/libs/service/public.h>
#include <cloud/blockstore/libs/service/storage.h>
#include <cloud/blockstore/libs/spdk/iface/public.h>
#include <cloud/blockstore/libs/storage/core/public.h>
#include <cloud/blockstore/libs/storage/disk_agent/model/device_client.h>
#include <cloud/blockstore/libs/storage/disk_agent/model/device_guard.h>
#include <cloud/blockstore/libs/storage/protos/disk.pb.h>

#include <cloud/storage/core/libs/common/error.h>

#include <library/cpp/threading/future/future.h>

#include <util/generic/hash.h>
#include <util/generic/vector.h>

namespace NCloud::NBlockStore::NStorage {

////////////////////////////////////////////////////////////////////////////////

class TDiskAgentState
{
private:
    struct TDeviceState
    {
        NProto::TDeviceConfig Config;
        std::shared_ptr<TStorageAdapter> StorageAdapter;

        TStorageIoStatsPtr Stats;
    };

private:
    const TDiskAgentConfigPtr AgentConfig;
    const NSpdk::ISpdkEnvPtr Spdk;
    const ICachingAllocatorPtr Allocator;
    const IStorageProviderPtr StorageProvider;
    const IProfileLogPtr ProfileLog;
    const IBlockDigestGeneratorPtr BlockDigestGenerator;

    ILoggingServicePtr Logging;
    NSpdk::ISpdkTargetPtr SpdkTarget;
    NRdma::IServerPtr RdmaServer;
    IRdmaTargetPtr RdmaTarget;
    NNvme::INvmeManagerPtr NvmeManager;

    THashMap<TString, TDeviceState> Devices;
    TDeviceClientPtr DeviceClient;

    ui32 InitErrorsCount = 0;

public:
    TDiskAgentState(
        TDiskAgentConfigPtr agentConfig,
        NSpdk::ISpdkEnvPtr spdk,
        ICachingAllocatorPtr allocator,
        IStorageProviderPtr storageProvider,
        IProfileLogPtr profileLog,
        IBlockDigestGeneratorPtr blockDigestGenerator,
        ILoggingServicePtr logging,
        NRdma::IServerPtr rdmaServer,
        NNvme::INvmeManagerPtr nvmeManager);

    struct TInitializeResult
    {
        TVector<NProto::TDeviceConfig> Configs;
        TVector<TString> Errors;
        TDeviceGuard Guard;
    };

    NThreading::TFuture<TInitializeResult> Initialize(
        TRdmaTargetConfig rdmaTargetConfig);

    NThreading::TFuture<NProto::TAgentStats> CollectStats();

    NThreading::TFuture<NProto::TReadDeviceBlocksResponse> Read(
        TInstant now,
        NProto::TReadDeviceBlocksRequest request);

    NThreading::TFuture<NProto::TWriteDeviceBlocksResponse> Write(
        TInstant now,
        NProto::TWriteDeviceBlocksRequest request);

    NThreading::TFuture<NProto::TZeroDeviceBlocksResponse> WriteZeroes(
        TInstant now,
        NProto::TZeroDeviceBlocksRequest request);

    NThreading::TFuture<NProto::TError> SecureErase(const TString& uuid, TInstant now);

    NThreading::TFuture<NProto::TChecksumDeviceBlocksResponse> Checksum(
        TInstant now,
        NProto::TChecksumDeviceBlocksRequest request);

    void CheckIOTimeouts(TInstant now);

    TString GetDeviceName(const TString& uuid) const;

    TVector<NProto::TDeviceConfig> GetDevices() const;

    TDeviceClient::TSessionInfo GetWriterSession(const TString& uuid) const;
    TVector<TDeviceClient::TSessionInfo> GetReaderSessions(
        const TString& uuid) const;

    void AcquireDevices(
        const TVector<TString>& uuids,
        const TString& clientId,
        TInstant now,
        NProto::EVolumeAccessMode accessMode,
        ui64 mountSeqNumber,
        const TString& diskId,
        ui32 volumeGeneration);

    void ReleaseDevices(
        const TVector<TString>& uuids,
        const TString& clientId,
        const TString& diskId,
        ui32 volumeGeneration);

    void DisableDevice(const TString& uuid);
    void EnableDevice(const TString& uuid);
    bool IsDeviceDisabled(const TString& uuid) const;
    void ReportDisabledDeviceError(const TString& uuid);

    void StopTarget();

private:
    const TDeviceState& GetDeviceState(
        const TString& uuid,
        const TString& clientId,
        const NProto::EVolumeAccessMode accessMode) const;

    const TDeviceState& GetDeviceStateImpl(
        const TString& uuid,
        const TString& clientId,
        const NProto::EVolumeAccessMode accessMode) const;
    const TDeviceState& GetDeviceStateImpl(const TString& uuid) const;

    template <typename T>
    void WriteProfileLog(
        TInstant now,
        const TString& uuid,
        const T& req,
        ui32 blockSize,
        ESysRequestType requestType);

    NThreading::TFuture<TInitializeResult> InitSpdkStorage();
    NThreading::TFuture<TInitializeResult> InitAioStorage();

    void InitRdmaTarget(TRdmaTargetConfig rdmaTargetConfig);
};

}   // namespace NCloud::NBlockStore::NStorage
