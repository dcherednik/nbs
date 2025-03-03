#pragma once

#include "public.h"
#include "features_config.h"

#include <cloud/blockstore/config/storage.pb.h>
#include <cloud/storage/core/protos/media.pb.h>

#include <util/datetime/base.h>
#include <util/generic/string.h>
#include <util/stream/output.h>

namespace NCloud::NBlockStore::NStorage {

////////////////////////////////////////////////////////////////////////////////

class TStorageConfig
{
private:
    struct TImpl;
    std::unique_ptr<TImpl> Impl;

public:
    TStorageConfig(
        NProto::TStorageServiceConfig storageServiceConfig,
        TFeaturesConfigPtr featuresConfig);
    ~TStorageConfig();

    TStorageConfig(const TStorageConfig& config);

    void SetFeaturesConfig(TFeaturesConfigPtr featuresConfig);

    void Register(NKikimr::TControlBoard& controlBoard);

    void Merge(const NProto::TStorageServiceConfig& storageServiceConfig);

    bool Equals(const TStorageConfig& other) const;

    struct TValueByName
    {
        enum class ENameStatus
        {
            NotFound,
            FoundInDefaults,
            FoundInProto
        };

        ENameStatus Status;
        TString Value;

        explicit TValueByName(ENameStatus status) : Status(status) {}

        TValueByName(const TString& value)
            : Status(ENameStatus::FoundInProto)
            , Value(value) {}
    };

    TValueByName GetValueByName(const TString& name) const;

    const NProto::TStorageServiceConfig& GetStorageConfigProto() const;

    TString GetSchemeShardDir() const;
    ui32 GetWriteBlobThreshold() const;
    ui32 GetWriteBlobThresholdSSD() const;
    ui32 GetFlushThreshold() const;
    ui32 GetFreshBlobCountFlushThreshold() const;
    ui32 GetFreshBlobByteCountFlushThreshold() const;
    ui32 GetFlushBlobSizeThreshold() const;
    bool GetFlushToDevNull() const;

    NProto::ECompactionType GetSSDCompactionType() const;
    NProto::ECompactionType GetHDDCompactionType() const;
    bool GetV1GarbageCompactionEnabled() const;
    ui32 GetCompactionGarbageThreshold() const;
    ui32 GetCompactionGarbageBlobLimit() const;
    ui32 GetCompactionGarbageBlockLimit() const;
    ui32 GetCompactionRangeGarbageThreshold() const;
    ui32 GetMaxAffectedBlocksPerCompaction() const;
    TDuration GetMaxCompactionDelay() const;
    TDuration GetMinCompactionDelay() const;
    TDuration GetMaxCompactionExecTimePerSecond() const;
    ui32 GetCompactionScoreHistorySize() const;
    ui32 GetCompactionScoreLimitForThrottling() const;
    ui64 GetTargetCompactionBytesPerOp() const;
    ui32 GetMaxSkippedBlobsDuringCompaction() const;
    bool GetIncrementalCompactionEnabled() const;
    ui32 GetCompactionCountPerRunIncreasingThreshold() const;
    ui32 GetCompactionCountPerRunDecreasingThreshold() const;
    ui32 GetCompactionRangeCountPerRun() const;
    ui32 GetMaxCompactionRangeCountPerRun() const;
    TDuration GetCompactionCountPerRunChangingPeriod() const;
    bool GetBatchCompactionEnabled() const;
    bool GetBlobPatchingEnabled() const;
    ui32 GetMaxDiffPercentageForBlobPatching() const;

    ui32 GetCleanupThreshold() const;
    ui32 GetUpdateBlobsThreshold() const;
    ui32 GetMaxBlobsToCleanup() const;
    TDuration GetMaxCleanupDelay() const;
    TDuration GetMinCleanupDelay() const;
    TDuration GetMaxCleanupExecTimePerSecond() const;
    ui32 GetCleanupScoreHistorySize() const;
    ui64 GetCleanupQueueBytesLimitForThrottling() const;

    ui32 GetCollectGarbageThreshold() const;
    bool GetDontEnqueueCollectGarbageUponPartitionStartup() const;
    TDuration GetHiveLockExpireTimeout() const;
    TDuration GetTabletRebootCoolDownIncrement() const;
    TDuration GetTabletRebootCoolDownMax() const;
    TDuration GetMinExternalBootRequestTimeout() const;
    TDuration GetExternalBootRequestTimeoutIncrement() const;
    TDuration GetMaxExternalBootRequestTimeout() const;
    bool GetDisableLocalService() const;
    ui32 GetPipeClientRetryCount() const;
    TDuration GetPipeClientMinRetryTime() const;
    TDuration GetPipeClientMaxRetryTime() const;
    TDuration GetCompactionRetryTimeout() const;
    TDuration GetCleanupRetryTimeout() const;
    ui64 GetMaxReadWriteRangeSize() const;
    ui32 GetMaxBlobRangeSize() const;
    ui32 GetMaxRangesPerBlob() const;
    ui32 GetMaxBlobSize() const;
    ui32 GetMaxIORequestsInFlight() const;
    ui32 GetMaxIORequestsInFlightSSD() const;
    bool GetAllowVersionInModifyScheme() const;
    TString GetServiceVersionInfo() const;

    TDuration GetInactiveClientsTimeout() const;
    TDuration GetClientRemountPeriod() const;
    TDuration GetInitialAddClientTimeout() const;
    TDuration GetLocalStartAddClientTimeout() const;
    TDuration GetAttachedDiskDestructionTimeout() const;

    ui32 GetAllocationUnitSSD() const;
    ui32 GetSSDUnitReadBandwidth() const;
    ui32 GetSSDUnitWriteBandwidth() const;
    ui32 GetSSDMaxReadBandwidth() const;
    ui32 GetSSDMaxWriteBandwidth() const;
    ui32 GetRealSSDUnitReadBandwidth() const;
    ui32 GetRealSSDUnitWriteBandwidth() const;
    ui32 GetSSDUnitReadIops() const;
    ui32 GetSSDUnitWriteIops() const;
    ui32 GetSSDMaxReadIops() const;
    ui32 GetSSDMaxWriteIops() const;
    ui32 GetRealSSDUnitReadIops() const;
    ui32 GetRealSSDUnitWriteIops() const;
    ui32 GetSSDMaxBlobsPerRange() const;
    ui32 GetSSDV2MaxBlobsPerRange() const;
    ui32 GetSSDMaxBlobsPerUnit() const;

    ui32 GetAllocationUnitHDD() const;
    ui32 GetHDDUnitReadBandwidth() const;
    ui32 GetHDDUnitWriteBandwidth() const;
    ui32 GetHDDMaxReadBandwidth() const;
    ui32 GetHDDMaxWriteBandwidth() const;
    ui32 GetRealHDDUnitReadBandwidth() const;
    ui32 GetRealHDDUnitWriteBandwidth() const;
    ui32 GetHDDUnitReadIops() const;
    ui32 GetHDDUnitWriteIops() const;
    ui32 GetHDDMaxReadIops() const;
    ui32 GetHDDMaxWriteIops() const;
    ui32 GetRealHDDUnitReadIops() const;
    ui32 GetRealHDDUnitWriteIops() const;
    ui32 GetHDDMaxBlobsPerRange() const;
    ui32 GetHDDV2MaxBlobsPerRange() const;
    ui32 GetHDDMaxBlobsPerUnit() const;

    ui32 GetAllocationUnitNonReplicatedSSD() const;
    ui32 GetNonReplicatedSSDUnitReadBandwidth() const;
    ui32 GetNonReplicatedSSDUnitWriteBandwidth() const;
    ui32 GetNonReplicatedSSDMaxReadBandwidth() const;
    ui32 GetNonReplicatedSSDMaxWriteBandwidth() const;
    ui32 GetNonReplicatedSSDUnitReadIops() const;
    ui32 GetNonReplicatedSSDUnitWriteIops() const;
    ui32 GetNonReplicatedSSDMaxReadIops() const;
    ui32 GetNonReplicatedSSDMaxWriteIops() const;

    ui32 GetAllocationUnitNonReplicatedHDD() const;
    ui32 GetNonReplicatedHDDUnitReadBandwidth() const;
    ui32 GetNonReplicatedHDDUnitWriteBandwidth() const;
    ui32 GetNonReplicatedHDDMaxReadBandwidth() const;
    ui32 GetNonReplicatedHDDMaxWriteBandwidth() const;
    ui32 GetNonReplicatedHDDUnitReadIops() const;
    ui32 GetNonReplicatedHDDUnitWriteIops() const;
    ui32 GetNonReplicatedHDDMaxReadIops() const;
    ui32 GetNonReplicatedHDDMaxWriteIops() const;
    TString GetNonReplicatedHDDPoolName() const;

    ui32 GetAllocationUnitMirror2SSD() const;
    ui32 GetMirror2SSDUnitReadBandwidth() const;
    ui32 GetMirror2SSDUnitWriteBandwidth() const;
    ui32 GetMirror2SSDMaxReadBandwidth() const;
    ui32 GetMirror2SSDMaxWriteBandwidth() const;
    ui32 GetMirror2SSDUnitReadIops() const;
    ui32 GetMirror2SSDUnitWriteIops() const;
    ui32 GetMirror2SSDMaxReadIops() const;
    ui32 GetMirror2SSDMaxWriteIops() const;

    ui32 GetMirror2DiskReplicaCount() const;

    ui32 GetAllocationUnitMirror3SSD() const;
    ui32 GetMirror3SSDUnitReadBandwidth() const;
    ui32 GetMirror3SSDUnitWriteBandwidth() const;
    ui32 GetMirror3SSDMaxReadBandwidth() const;
    ui32 GetMirror3SSDMaxWriteBandwidth() const;
    ui32 GetMirror3SSDUnitReadIops() const;
    ui32 GetMirror3SSDUnitWriteIops() const;
    ui32 GetMirror3SSDMaxReadIops() const;
    ui32 GetMirror3SSDMaxWriteIops() const;

    ui32 GetMirror3DiskReplicaCount() const;

    bool GetThrottlingEnabled() const;
    bool GetThrottlingEnabledSSD() const;
    ui32 GetThrottlingBurstPercentage() const;
    ui32 GetThrottlingMaxPostponedWeight() const;
    ui32 GetDefaultPostponedRequestWeight() const;
    TDuration GetThrottlingBoostTime() const;
    TDuration GetThrottlingBoostRefillTime() const;
    ui32 GetThrottlingSSDBoostUnits() const;
    ui32 GetThrottlingHDDBoostUnits() const;
    TDuration GetMaxThrottlerDelay() const;
    TDuration GetThrottlerStateWriteInterval() const;

    ui32 GetCompactionScoreThresholdForBackpressure() const;
    ui32 GetCompactionScoreLimitForBackpressure() const;
    ui32 GetCompactionScoreFeatureMaxValue() const;
    ui32 GetFreshByteCountThresholdForBackpressure() const;
    ui32 GetFreshByteCountLimitForBackpressure() const;
    ui32 GetFreshByteCountFeatureMaxValue() const;
    ui64 GetCleanupQueueBytesThresholdForBackpressure() const;
    ui64 GetCleanupQueueBytesLimitForBackpressure() const;
    ui32 GetCleanupQueueBytesFeatureMaxValue() const;
    ui32 GetMaxWriteCostMultiplier() const;
    ui32 GetFreshByteCountHardLimit() const;
    bool GetDiskSpaceScoreThrottlingEnabled() const;

    TDuration GetStatsUploadInterval() const;

    NCloud::NProto::EAuthorizationMode GetAuthorizationMode() const;

    TString GetHDDSystemChannelPoolKind() const;
    TString GetHDDLogChannelPoolKind() const;
    TString GetHDDIndexChannelPoolKind() const;
    TString GetHDDMixedChannelPoolKind() const;
    TString GetHDDMergedChannelPoolKind() const;
    TString GetHDDFreshChannelPoolKind() const;
    TString GetSSDSystemChannelPoolKind() const;
    TString GetSSDLogChannelPoolKind() const;
    TString GetSSDIndexChannelPoolKind() const;
    TString GetSSDMixedChannelPoolKind() const;
    TString GetSSDMergedChannelPoolKind() const;
    TString GetSSDFreshChannelPoolKind() const;
    TString GetHybridSystemChannelPoolKind() const;
    TString GetHybridLogChannelPoolKind() const;
    TString GetHybridIndexChannelPoolKind() const;
    TString GetHybridMixedChannelPoolKind() const;
    TString GetHybridMergedChannelPoolKind() const;
    TString GetHybridFreshChannelPoolKind() const;
    bool GetAllocateSeparateMixedChannels() const;
    bool GetPoolKindChangeAllowed() const;

    TString GetFolderId() const;

    ui32 GetChannelFreeSpaceThreshold() const;
    ui32 GetChannelMinFreeSpace() const;

    ui32 GetMinChannelCount() const;
    ui32 GetFreshChannelCount() const;

    ui32 GetZoneBlockCount() const;
    ui32 GetHotZoneRequestCountFactor() const;
    ui32 GetColdZoneRequestCountFactor() const;

    bool GetWriteRequestBatchingEnabled() const;

    bool GetFreshChannelWriteRequestsEnabled() const;

    ui32 GetBlockListCacheSizePercentage() const;

    bool GetBlockDigestsEnabled() const;
    bool GetUseTestBlockDigestGenerator() const;
    ui32 GetDigestedBlocksPercentage() const;

    TDuration GetIndexStructuresConversionAttemptInterval() const;

    TDuration GetNonReplicatedDiskRecyclingPeriod() const;
    TDuration GetNonReplicatedDiskRepairTimeout() const;
    TDuration GetNonReplicatedDiskSwitchToReadOnlyTimeout() const;
    TDuration GetAgentRequestTimeout() const;

    TDuration GetNonReplicatedAgentMinTimeout() const;
    TDuration GetNonReplicatedAgentMaxTimeout() const;
    TDuration GetNonReplicatedAgentDisconnectRecoveryInterval() const;
    double GetNonReplicatedAgentTimeoutGrowthFactor() const;

    NProto::EVolumePreemptionType GetVolumePreemptionType() const;
    ui32 GetPreemptionPushPercentage() const;
    ui32 GetPreemptionPullPercentage() const;

    bool IsBalancerFeatureEnabled(
        const TString& cloudId,
        const TString& folderId) const;
    bool IsIncrementalCompactionFeatureEnabled(
        const TString& cloudId,
        const TString& folderId) const;
    bool IsMultipartitionVolumesFeatureEnabled(
        const TString& cloudId,
        const TString& folderId) const;
    bool IsAllocateFreshChannelFeatureEnabled(
        const TString& cloudId,
        const TString& folderId) const;
    bool IsFreshChannelWriteRequestsFeatureEnabled(
        const TString& cloudId,
        const TString& folderId) const;
    bool IsMixedIndexCacheV1FeatureEnabled(
        const TString& cloudId,
        const TString& folderId) const;
    bool IsBatchCompactionFeatureEnabled(
        const TString& cloudId,
        const TString& folderId) const;
    bool IsBlobPatchingFeatureEnabled(
        const TString& cloudId,
        const TString& folderId) const;
    bool IsUseRdmaFeatureEnabled(
        const TString& cloudId,
        const TString& folderId) const;
    bool IsChangeThrottlingPolicyFeatureEnabled(
        const TString& cloudId,
        const TString& folderId) const;
    bool IsReplaceDeviceFeatureEnabled(
        const TString& cloudId,
        const TString& folderId) const;
    bool IsUseNonReplicatedHDDInsteadOfReplicatedFeatureEnabled(
        const TString& cloudId,
        const TString& folderId) const;

    TDuration GetMaxTimedOutDeviceStateDurationFeatureValue(
        const TString& cloudId,
        const TString& folderId) const;

    TString GetSSDSystemChannelPoolKindFeatureValue(
        const TString& cloudId,
        const TString& folderId) const;
    TString GetSSDLogChannelPoolKindFeatureValue(
        const TString& cloudId,
        const TString& folderId) const;
    TString GetSSDIndexChannelPoolKindFeatureValue(
        const TString& cloudId,
        const TString& folderId) const;
    TString GetSSDFreshChannelPoolKindFeatureValue(
        const TString& cloudId,
        const TString& folderId) const;

    ui32 GetDefaultTabletVersion() const;

    bool GetAcquireNonReplicatedDevices() const;

    ui32 GetNonReplicatedInflightLimit() const;
    TDuration GetMaxTimedOutDeviceStateDuration() const;

    ui32 GetMaxDisksInPlacementGroup() const;
    ui32 GetMaxPlacementPartitionCount() const;
    ui32 GetMaxDisksInPartitionPlacementGroup() const;

    ui32 GetMaxBrokenHddPlacementGroupPartitionsAfterDeviceRemoval() const;

    TDuration GetBrokenDiskDestructionDelay() const;

    TDuration GetAutomaticallyReplacedDevicesFreezePeriod() const;
    ui32 GetMaxAutomaticDeviceReplacementsPerHour() const;

    TDuration GetVolumeHistoryDuration() const;
    ui32 GetVolumeHistoryCacheSize() const;

    ui64 GetBytesPerPartition() const;
    ui64 GetBytesPerPartitionSSD() const;
    ui32 GetBytesPerStripe() const;
    ui32 GetMaxPartitionsPerVolume() const;
    bool GetMultipartitionVolumesEnabled() const;

    TDuration GetNonReplicatedInfraTimeout() const;
    TDuration GetNonReplicatedInfraUnavailableAgentTimeout() const;

    TDuration GetNonReplicatedMinRequestTimeoutSSD() const;
    TDuration GetNonReplicatedMaxRequestTimeoutSSD() const;
    TDuration GetNonReplicatedMinRequestTimeoutHDD() const;
    TDuration GetNonReplicatedMaxRequestTimeoutHDD() const;
    TDuration GetExpectedClientBackoffIncrement() const;

    TDuration GetDeletedCheckpointHistoryLifetime() const;
    bool GetNonReplicatedMigrationStartAllowed() const;
    bool GetNonReplicatedVolumeMigrationDisabled() const;
    ui32 GetMigrationIndexCachingInterval() const;
    ui32 GetMaxMigrationBandwidth() const;
    ui32 GetExpectedDiskAgentSize() const;
    ui32 GetMaxNonReplicatedDeviceMigrationsInProgress() const;

    bool GetMirroredMigrationStartAllowed() const;

    bool GetOptimizeForShortRanges() const;

    bool GetUserDataDebugDumpAllowed() const;

    bool GetRunV2SoftGcAtStartup() const;

    TDuration GetPlacementGroupAlertPeriod() const;

    bool GetEnableLoadActor() const;

    ui64 GetCpuMatBenchNsSystemThreshold() const;
    ui64 GetCpuMatBenchNsUserThreshold() const;
    ui32 GetCpuLackThreshold() const;
    TDuration GetInitialPullDelay() const;

    ui32 GetLogicalUsedBlocksUpdateBlockCount() const;

    bool GetDumpBlockCommitIdsIntoProfileLog() const;
    bool GetDumpBlobUpdatesIntoProfileLog() const;

    bool GetEnableConversionIntoMixedIndexV2() const;

    ui32 GetStatsUploadDiskCount() const;
    TDuration GetStatsUploadRetryTimeout() const;

    bool GetRemoteMountOnly() const;
    ui32 GetMaxLocalVolumes() const;

    TDuration GetDiskRegistryVolumeConfigUpdatePeriod() const;
    TDuration GetReassignRequestRetryTimeout() const;
    ui32 GetReassignChannelsPercentageThreshold() const;

    TString GetCommonSSDPoolKind() const;
    ui64 GetMaxSSDGroupWriteBandwidth() const;
    ui64 GetMaxSSDGroupReadBandwidth() const;
    ui64 GetMaxSSDGroupWriteIops() const;
    ui64 GetMaxSSDGroupReadIops() const;

    TString GetCommonHDDPoolKind() const;
    ui64 GetMaxHDDGroupWriteBandwidth() const;
    ui64 GetMaxHDDGroupReadBandwidth() const;
    ui64 GetMaxHDDGroupWriteIops() const;
    ui64 GetMaxHDDGroupReadIops() const;

    TString GetCommonOverlayPrefixPoolKind() const;

    bool GetMixedIndexCacheV1Enabled() const;
    ui32 GetMixedIndexCacheV1SizeSSD() const;

    ui32 GetMaxReadBlobErrorsBeforeSuicide() const;

    bool GetRejectMountOnAddClientTimeout() const;

    TDuration GetNonReplicatedVolumeNotificationTimeout() const;

    TDuration GetNonReplicatedSecureEraseTimeout() const;
    ui32 GetMaxDevicesToErasePerDeviceNameForDefaultPoolKind() const;
    ui32 GetMaxDevicesToErasePerDeviceNameForLocalPoolKind() const;
    ui32 GetMaxDevicesToErasePerDeviceNameForGlobalPoolKind() const;

    TString GetTabletBootInfoBackupFilePath() const;
    bool GetHiveProxyFallbackMode() const;
    TString GetPathDescriptionBackupFilePath() const;
    bool GetSSProxyFallbackMode() const;

    ui32 GetRdmaTargetPort() const;
    bool GetUseNonreplicatedRdmaActor() const;
    bool GetUseRdma() const;

    bool GetNonReplicatedDontSuspendDevices() const;
    TDuration GetAddClientRetryTimeoutIncrement() const;

    void Dump(IOutputStream& out) const;
    void DumpHtml(IOutputStream& out) const;

    ui32 GetDiskRegistrySplitTransactionCounter() const;

    TDuration GetDiskRegistryBackupPeriod() const;
    TString GetDiskRegistryBackupDirPath() const;

    ui32 GetMaxNonReplicatedDiskDeallocationRequests() const;

    TDuration GetDiskRegistryMetricsCachePeriod() const;

    TString GetDiskRegistryCountersHost() const;

    TDuration GetBalancerActionDelayInterval() const;

    bool GetUseMirrorResync() const;
    bool GetForceMirrorResync() const;
    ui32 GetResyncIndexCachingInterval() const;
    TDuration GetResyncAfterClientInactivityInterval() const;

    TString GetManuallyPreemptedVolumesFile() const;
    TDuration GetManuallyPreemptedVolumeLivenessCheckPeriod() const;
    bool GetDisableManuallyPreemptedVolumesTracking() const;

    TDuration GetPingMetricsHalfDecayInterval() const;

    bool GetDisableStartPartitionsForGc() const;

    bool GetAddingUnconfirmedBlobsEnabled() const;

    ui32 GetBlobCompressionRate() const;
    TString GetBlobCompressionCodec() const;

    bool GetSerialNumberValidationEnabled() const;

    TVector<TString> GetKnownSpareNodes() const;
    ui32 GetSpareNodeProbability() const;

    bool GetRejectLateRequestsAtDiskAgentEnabled() const;
    bool GetAssignIdToWriteAndZeroRequestsEnabled() const;

    TDuration GetAgentListExpiredParamsCleanupInterval() const;

    ui64 GetTenantHiveTabletId() const;
};

ui64 GetAllocationUnit(
    const TStorageConfig& config,
    NCloud::NProto::EStorageMediaKind mediaKind);

}   // namespace NCloud::NBlockStore::NStorage
