#include "config.h"

#include <library/cpp/monlib/service/pages/templates.h>

#include <util/generic/size_literals.h>

#include <google/protobuf/text_format.h>

namespace NCloud::NFileStore::NStorage {

namespace {

////////////////////////////////////////////////////////////////////////////////

#define FILESTORE_STORAGE_CONFIG(xxx)                                          \
    xxx(SchemeShardDir,                TString,   "/Root"                     )\
                                                                               \
    xxx(PipeClientRetryCount,          ui32,      4                           )\
    xxx(PipeClientMinRetryTime,        TDuration, TDuration::Seconds(1)       )\
    xxx(PipeClientMaxRetryTime,        TDuration, TDuration::Seconds(4)       )\
                                                                               \
    xxx(EstablishSessionTimeout,       TDuration, TDuration::Seconds(30)      )\
    xxx(IdleSessionTimeout,            TDuration, TDuration::Seconds(30)      )\
                                                                               \
    xxx(WriteBatchEnabled,             bool,      false                       )\
    xxx(WriteBatchTimeout,             TDuration, TDuration::MilliSeconds(0)  )\
    xxx(WriteBlobThreshold,            ui32,      128_KB                      )\
                                                                               \
    xxx(MaxBlobSize,                   ui32,      4_MB                        )\
    xxx(FlushThreshold,                ui32,      4_MB                        )\
    xxx(CleanupThreshold,              ui32,      512                         )\
    xxx(CompactionThreshold,           ui32,      20                          )\
    xxx(CollectGarbageThreshold,       ui32,      4_MB                        )\
    xxx(FlushBytesThreshold,           ui32,      4_MB                        )\
    xxx(MaxDeleteGarbageBlobsPerTx,    ui32,      16384                       )\
    xxx(MaxBlocksPerTruncateTx,        ui32,      0 /*TODO: 8388608 32gb/4kb*/)\
    xxx(MaxTruncateTxInflight,         ui32,      10                          )\
    xxx(CompactionRetryTimeout,        TDuration, TDuration::Seconds(1)       )\
                                                                               \
    xxx(FlushThresholdForBackpressure,      ui32,      128_MB                 )\
    xxx(CleanupThresholdForBackpressure,    ui32,      32768                  )\
    xxx(CompactionThresholdForBackpressure, ui32,      200                    )\
    xxx(FlushBytesThresholdForBackpressure, ui32,      128_MB                 )\
                                                                               \
    xxx(HDDSystemChannelPoolKind,      TString,   "rot"                       )\
    xxx(HDDLogChannelPoolKind,         TString,   "rot"                       )\
    xxx(HDDIndexChannelPoolKind,       TString,   "rot"                       )\
    xxx(HDDFreshChannelPoolKind,       TString,   "rot"                       )\
    xxx(HDDMixedChannelPoolKind,       TString,   "rot"                       )\
                                                                               \
    xxx(SSDSystemChannelPoolKind,      TString,   "ssd"                       )\
    xxx(SSDLogChannelPoolKind,         TString,   "ssd"                       )\
    xxx(SSDIndexChannelPoolKind,       TString,   "ssd"                       )\
    xxx(SSDFreshChannelPoolKind,       TString,   "ssd"                       )\
    xxx(SSDMixedChannelPoolKind,       TString,   "ssd"                       )\
                                                                               \
    xxx(HybridSystemChannelPoolKind,   TString,   "ssd"                       )\
    xxx(HybridLogChannelPoolKind,      TString,   "ssd"                       )\
    xxx(HybridIndexChannelPoolKind,    TString,   "ssd"                       )\
    xxx(HybridFreshChannelPoolKind,    TString,   "ssd"                       )\
    xxx(HybridMixedChannelPoolKind,    TString,   "rot"                       )\
                                                                               \
    xxx(AllocationUnitSSD,                 ui32,       32                     )\
    xxx(SSDUnitReadBandwidth,              ui32,       15                     )\
    xxx(SSDUnitWriteBandwidth,             ui32,       15                     )\
    xxx(SSDMaxReadBandwidth,               ui32,       450                    )\
    xxx(SSDMaxWriteBandwidth,              ui32,       450                    )\
    xxx(SSDUnitReadIops,                   ui32,       400                    )\
    xxx(SSDUnitWriteIops,                  ui32,       1000                   )\
    xxx(SSDMaxReadIops,                    ui32,       12000                  )\
    xxx(SSDMaxWriteIops,                   ui32,       40000                  )\
    xxx(SSDThrottlingEnabled,              bool,       false                  )\
    xxx(SSDBoostTime,                      TDuration,  TDuration::Zero()      )\
    xxx(SSDBoostRefillTime,                TDuration,  TDuration::Zero()      )\
    xxx(SSDUnitBoost,                      ui32,       0                      )\
    xxx(SSDBurstPercentage,                ui32,       15                     )\
    xxx(SSDDefaultPostponedRequestWeight,  ui32,       4_KB                   )\
    xxx(SSDMaxPostponedWeight,             ui32,       128_MB                 )\
    xxx(SSDMaxWriteCostMultiplier,         ui32,       10                     )\
    xxx(SSDMaxPostponedTime,               TDuration,  TDuration::Seconds(20) )\
    xxx(SSDMaxPostponedCount,              ui32,       512                    )\
                                                                               \
    xxx(AllocationUnitHDD,                 ui32,       256                    )\
    xxx(HDDUnitReadBandwidth,              ui32,       30                     )\
    xxx(HDDUnitWriteBandwidth,             ui32,       30                     )\
    xxx(HDDMaxReadBandwidth,               ui32,       240                    )\
    xxx(HDDMaxWriteBandwidth,              ui32,       240                    )\
    xxx(HDDUnitReadIops,                   ui32,       100                    )\
    xxx(HDDUnitWriteIops,                  ui32,       300                    )\
    xxx(HDDMaxReadIops,                    ui32,       300                    )\
    xxx(HDDMaxWriteIops,                   ui32,       11000                  )\
    xxx(HDDThrottlingEnabled,              bool,       false                  )\
    xxx(HDDBoostTime,                      TDuration,  TDuration::Minutes(30) )\
    xxx(HDDBoostRefillTime,                TDuration,  TDuration::Hours(12)   )\
    xxx(HDDUnitBoost,                      ui32,       4                      )\
    xxx(HDDBurstPercentage,                ui32,       10                     )\
    xxx(HDDDefaultPostponedRequestWeight,  ui32,       4_KB                   )\
    xxx(HDDMaxPostponedWeight,             ui32,       128_MB                 )\
    xxx(HDDMaxWriteCostMultiplier,         ui32,       20                     )\
    xxx(HDDMaxPostponedTime,               TDuration,  TDuration::Seconds(20) )\
    xxx(HDDMaxPostponedCount,              ui32,       1024                   )\
                                                                               \
    xxx(HDDMediaKindOverride,          ui32,      2        /*HYBRID*/         )\
    xxx(MinChannelCount,               ui32,      4                           )\
                                                                               \
    xxx(DefaultNodesLimit,             ui32,      4194304                     )\
    xxx(SizeToNodesRatio,              ui32,      65536    /*mke2fs huge*/    )\
                                                                               \
    xxx(MaxResponseBytes,              ui32,      4_MB                        )\
    xxx(MaxResponseEntries,            ui32,      1000                        )\
                                                                               \
    xxx(DupCacheEntryCount,            ui32,      1024/*iod 128 but NBS-4016*/)\
    xxx(DisableLocalService,           bool,      false                       )\
    xxx(EnableCollectGarbageAtStart,   bool,      false                       )\
                                                                               \
    xxx(TabletBootInfoBackupFilePath,  TString,   ""                          )\
    xxx(HiveProxyFallbackMode,         bool,      false                       )\
                                                                               \
    xxx(ThrottlingEnabled,             bool,      false                       )\
                                                                               \
    xxx(ReassignChannelsPercentageThreshold,       ui32,      10              )\
                                                                               \
    xxx(CpuLackThreshold,                          ui32,      70              )\
                                                                               \
    xxx(SessionHistoryEntryCount,                  ui32,      100             )\
                                                                               \
    xxx(TenantHiveTabletId,                        ui64,       0              )\
    xxx(FolderId,                                  TString,    {}             )\
    xxx(AuthorizationMode,                                                     \
            NCloud::NProto::EAuthorizationMode,                                \
            NCloud::NProto::AUTHORIZATION_IGNORE                              )\
// FILESTORE_STORAGE_CONFIG

#define FILESTORE_DECLARE_CONFIG(name, type, value)                            \
    Y_DECLARE_UNUSED static const type Default##name = value;                  \
// FILESTORE_DECLARE_CONFIG

FILESTORE_STORAGE_CONFIG(FILESTORE_DECLARE_CONFIG)

#undef FILESTORE_DECLARE_CONFIG

////////////////////////////////////////////////////////////////////////////////

template <typename T>
bool IsEmpty(const T& t)
{
    return !t;
}

template <typename TTarget, typename TSource>
TTarget ConvertValue(const TSource& value)
{
    return static_cast<TTarget>(value);
}

template <>
TDuration ConvertValue<TDuration, ui32>(const ui32& value)
{
    return TDuration::MilliSeconds(value);
}

IOutputStream& operator <<(
    IOutputStream& out,
    NCloud::NProto::EAuthorizationMode mode)
{
    return out << EAuthorizationMode_Name(mode);
}

template <typename T>
void DumpImpl(const T& t, IOutputStream& os)
{
    os << t;
}

}   // namespace

////////////////////////////////////////////////////////////////////////////////

#define FILESTORE_CONFIG_GETTER(name, type, ...)                               \
type TStorageConfig::Get##name() const                                         \
{                                                                              \
    const auto value = ProtoConfig.Get##name();                                \
    return !IsEmpty(value) ? ConvertValue<type>(value) : Default##name;        \
}                                                                              \
// FILESTORE_CONFIG_GETTER

FILESTORE_STORAGE_CONFIG(FILESTORE_CONFIG_GETTER)

#undef FILESTORE_CONFIG_GETTER

void TStorageConfig::Dump(IOutputStream& out) const
{
#define FILESTORE_DUMP_CONFIG(name, ...)                                       \
    out << #name << ": ";                                                      \
    DumpImpl(Get##name(), out);                                                \
    out << Endl;                                                               \
// FILESTORE_DUMP_CONFIG

    FILESTORE_STORAGE_CONFIG(FILESTORE_DUMP_CONFIG);

#undef FILESTORE_DUMP_CONFIG
}

void TStorageConfig::DumpHtml(IOutputStream& out) const
{
#define FILESTORE_DUMP_CONFIG(name, ...)                                       \
    TABLER() {                                                                 \
        TABLED() { out << #name; }                                             \
        TABLED() { DumpImpl(Get##name(), out); }                               \
    }                                                                          \
// FILESTORE_DUMP_CONFIG

    HTML(out) {
        TABLE_CLASS("table table-condensed") {
            TABLEBODY() {
                FILESTORE_STORAGE_CONFIG(FILESTORE_DUMP_CONFIG);
            }
        }
    }

#undef FILESTORE_DUMP_CONFIG
}

////////////////////////////////////////////////////////////////////////////////

void TStorageConfig::Merge(
    const NProto::TStorageConfig& storageConfig)
{
    ProtoConfig.MergeFrom(storageConfig);
}

TStorageConfig::TValueByName TStorageConfig::GetValueByName(
    const TString& name) const
{
    auto descriptor =
        ProtoConfig.GetDescriptor()->FindFieldByName(name);
    using TStatus = TValueByName::ENameStatus;

    if (descriptor == nullptr) {
        return TValueByName(TStatus::NotFound);
    }

    const auto* reflection = NProto::TStorageConfig::GetReflection();
    if (!reflection->HasField(ProtoConfig, descriptor)) {
        return TValueByName(TStatus::FoundInDefaults);
    }

    TString value;
    google::protobuf::TextFormat::PrintFieldValueToString(
        ProtoConfig,
        descriptor,
        -1,
        &value);

    return TValueByName(value);
}

}   // namespace NCloud::NFileStore::NStorage
