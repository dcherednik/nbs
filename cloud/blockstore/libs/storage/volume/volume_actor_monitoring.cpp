#include "volume_actor.h"

#include <cloud/blockstore/libs/diagnostics/config.h>
#include <cloud/blockstore/libs/storage/core/config.h>
#include <cloud/blockstore/libs/storage/partition_nonrepl/config.h>

#include <cloud/storage/core/libs/common/format.h>
#include <cloud/storage/core/libs/common/media.h>
#include <cloud/storage/core/libs/throttling/tablet_throttler.h>

#include <library/cpp/monlib/service/pages/templates.h>
#include <library/cpp/protobuf/util/pb_io.h>

#include <util/stream/str.h>
#include <util/string/join.h>

namespace NCloud::NBlockStore::NStorage {

using namespace NActors;
using namespace NMonitoringUtils;

namespace {

////////////////////////////////////////////////////////////////////////////////

IOutputStream& operator <<(
    IOutputStream& out,
    const TActiveCheckpointsType& checkpointType)
{
    return out << (checkpointType.Type == ECheckpointType::Light
        ? "no data (is light)"
        : checkpointType.Data == ECheckpointData::DataPresent
            ? "present"
            : "deleted");
}

IOutputStream& operator <<(
    IOutputStream& out,
    NProto::EVolumeIOMode ioMode)
{
    switch (ioMode) {
        case NProto::VOLUME_IO_OK:
            return out << "<font color=green>ok</font>";
        case NProto::VOLUME_IO_ERROR_READ_ONLY:
            return out << "<font color=red>read only</font>";
        default:
            return out
                << "(Unknown EVolumeIOMode value "
                << static_cast<int>(ioMode)
                << ")";
    }
}

IOutputStream& operator <<(
    IOutputStream& out,
    NProto::EVolumeAccessMode accessMode)
{
    switch (accessMode) {
        case NProto::VOLUME_ACCESS_READ_WRITE:
            return out << "Read/Write";
        case NProto::VOLUME_ACCESS_READ_ONLY:
            return out << "Read only";
        case NProto::VOLUME_ACCESS_REPAIR:
            return out << "Repair";
        case NProto::VOLUME_ACCESS_USER_READ_ONLY:
            return out << "User read only";
        default:
            Y_DEBUG_ABORT_UNLESS(false, "Unknown EVolumeAccessMode: %d", accessMode);
            return out << "Undefined";
    }
}

IOutputStream& operator <<(
    IOutputStream& out,
    ECheckpointRequestState checkpointRequestState)
{
    switch (checkpointRequestState) {
        case ECheckpointRequestState::Received:
            return out << "Received";
        case ECheckpointRequestState::Saved:
            return out << "Saved";
        case ECheckpointRequestState::Completed:
            return out << "Completed";
        case ECheckpointRequestState::Rejected:
            return out << "Rejected";
        default:
            return out
                << "(Unknown ECheckpointRequestState value "
                << static_cast<int>(checkpointRequestState)
                << ")";
    }
}

IOutputStream& operator <<(
    IOutputStream& out,
    ECheckpointRequestType checkpointRequestType)
{
    switch (checkpointRequestType) {
        case ECheckpointRequestType::Create:
            return out << "Create";
        case ECheckpointRequestType::Delete:
            return out << "Delete";
        case ECheckpointRequestType::DeleteData:
            return out << "DeleteData";
        default:
            return out
                << "(Unknown ECheckpointRequestType value "
                << static_cast<int>(checkpointRequestType)
                << ")";
    }
}

////////////////////////////////////////////////////////////////////////////////

void BuildVolumeRemoveClientButton(
    IOutputStream& out,
    ui64 id,
    const TString& diskId,
    const TString& clientId,
    const ui64 tabletId)
{
    out << "<form method='POST' name='RemoveClient_" << clientId << "'>"
        << "<input type='hidden' name='TabletID' value='" << tabletId << "'/>"
        << "<input type='hidden' name='ClientId' value='" << clientId << "'/>"
        << "<input type='hidden' name='Volume' value='" << diskId << "'/>"
        << "<input type='hidden' name='action' value='removeclient'/>"
        << "<input class='btn btn-primary' type='button' value='Remove' "
        << "data-toggle='modal' data-target='#volume-remove-client-"
        << id << "'/>"
        << "</form>" << Endl;

    BuildConfirmActionDialog(
        out,
        TStringBuilder() << "volume-remove-client-" << id,
        "Remove client",
        "Are you sure you want to remove client from volume?",
        TStringBuilder() << "volumeRemoveClient(\"" << clientId << "\");");
}

void BuildVolumeResetMountSeqNumberButton(
    IOutputStream& out,
    const TString& clientId,
    const ui64 tabletId)
{
    out << "<form method='POST' name='ResetMountSeqNumber_" << clientId << "'>"
        << "<input type='hidden' name='TabletID' value='" << tabletId << "'/>"
        << "<input type='hidden' name='ClientId' value='" << clientId << "'/>"
        << "<input type='hidden' name='action' value='resetmountseqnumber'/>"
        << "<input class='btn btn-primary' type='button' value='Reset' "
        << "data-toggle='modal' data-target='#volume-reset-seqnumber-"
        << clientId << "'/>"
        << "</form>" << Endl;

    BuildConfirmActionDialog(
        out,
        TStringBuilder() << "volume-reset-seqnumber-" << clientId,
        "Reset Mount Seq Number",
        "Are you sure you want to volume mount seq number?",
        TStringBuilder() << "resetMountSeqNumber(\"" << clientId << "\");");
}

void BuildHistoryNextButton(
    IOutputStream& out,
    THistoryLogKey key,
    ui64 tabletId)
{
    auto ts = key.Timestamp.MicroSeconds();
    if (ts) {
        --ts;
    }
    out << "<form method='GET' name='NextHistoryPage' style='display:inline-block'>"
        << "<input type='hidden' name='timestamp' value='" << ts << "'/>"
        << "<input type='hidden' name='next' value='true'/>"
        << "<input type='hidden' name='TabletID' value='" << tabletId << "'/>"
        << "<input class='btn btn-primary display:inline-block' type='submit' value='Next>>'/>"
        << "</form>" << Endl;
}

void BuildStartPartitionsButton(
    IOutputStream& out,
    const TString& diskId,
    const ui64 tabletId)
{
    out << "<form method='POST' name='StartPartitions'>"
        << "<input type='hidden' name='TabletID' value='" << tabletId << "'/>"
        << "<input type='hidden' name='Volume' value='" << diskId << "'/>"
        << "<input type='hidden' name='action' value='startpartitions'/>"
        << "<input class='btn btn-primary' type='button' value='Start Partitions' "
        << "data-toggle='modal' data-target='#start-partitions'/>"
        << "</form>" << Endl;

    BuildConfirmActionDialog(
        out,
        "start-partitions",
        "Start partitions",
        "Are you sure you want to start volume partitions?",
        TStringBuilder() << "startPartitions();");
}

void BuildChangeThrottlingPolicyButton(
    IOutputStream& out,
    const TString& diskId,
    const NProto::TVolumePerformanceProfile& pp,
    const ui64 tabletId)
{
    out << "<form method='POST' name='ChangeThrottlingPolicy'>"
        << "<input type='hidden' name='TabletID' value='" << tabletId << "'/>"
        << "<input type='hidden' name='Volume' value='" << diskId << "'/>"
        << "<input type='hidden' name='action' value='changethrottlingpolicy'/>"
        << "<input type='text' name='MaxReadIops' value='"
        << pp.GetMaxReadIops() << "'/>"
        << "<input type='text' name='MaxWriteIops' value='"
        << pp.GetMaxWriteIops() << "'/>"
        << "<input type='text' name='MaxReadBandwidth' value='"
        << pp.GetMaxReadBandwidth() << "'/>"
        << "<input type='text' name='MaxWriteBandwidth' value='"
        << pp.GetMaxWriteBandwidth() << "'/>"
        << "<input class='btn btn-primary' type='button'"
        << " value='Change Throttling Policy' "
        << "data-toggle='modal' data-target='#change-throttling-policy'/>"
        << "</form>" << Endl;

    BuildConfirmActionDialog(
        out,
        "change-throttling-policy",
        "Change throttling policy",
        "Are you sure you want to change throttling policy?",
        TStringBuilder() << "changeThrottlingPolicy();");
}

////////////////////////////////////////////////////////////////////////////////

void OutputClientInfo(
    IOutputStream& out,
    const TString& clientId,
    const TString& diskId,
    ui64 clientNo,
    ui64 tabletId,
    NProto::EVolumeMountMode mountMode,
    NProto::EVolumeAccessMode accessMode,
    ui64 disconnectTimestamp,
    ui64 lastActivityTimestamp,
    bool isStale,
    TVolumeClientState::TPipes pipes)
{
    HTML(out) {
        TABLER() {
            TABLED() { out << clientId; }
            TABLED() {
                out << accessMode;
            }
            TABLED() {
                if (mountMode == NProto::VOLUME_MOUNT_LOCAL) {
                    out << "Local";
                } else {
                    out << "Remote";
                }
            }
            TABLED() {
                auto time = disconnectTimestamp;
                if (time) {
                    out << TInstant::MicroSeconds(time).ToStringLocalUpToSeconds();
                }
            }
            TABLED() {
                auto time = lastActivityTimestamp;
                if (time) {
                    out << TInstant::MicroSeconds(time).ToStringLocalUpToSeconds();
                }
            }
            TABLED() {
                if (isStale) {
                    out << "Stale";
                } else {
                    out << "Actual";
                }
            }
            TABLED() {
                UL() {
                    for (const auto p: pipes) {
                        if (p.second.State != TVolumeClientState::EPipeState::DEACTIVATED) {
                            if (p.second.State == TVolumeClientState::EPipeState::WAIT_START) {
                                LI() {
                                    out << p.first << "[Wait]";
                                }
                            } else if (p.second.State == TVolumeClientState::EPipeState::ACTIVE) {
                                LI() {
                                    out << p.first << "[Active]";
                                }
                            }
                        }
                    }
                }
            }
            TABLED() {
                BuildVolumeRemoveClientButton(
                    out,
                    clientNo,
                    diskId,
                    clientId,
                    tabletId);
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

void OutputProgress(
    ui64 blocks,
    ui64 totalBlocks,
    ui32 blockSize,
    IOutputStream& out)
{
    out << blocks;
    out << " ("
        << FormatByteSize(blocks * blockSize)
        << ")";
    out << " / ";
    out << totalBlocks;
    out << " ("
        << FormatByteSize(totalBlocks * blockSize)
        << ")";
    out << " = ";
    out << (100 * blocks / totalBlocks) << "%";
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

bool TVolumeActor::CanChangeThrottlingPolicy() const
{
    const auto& volumeConfig = GetNewestConfig();
    return Config->IsChangeThrottlingPolicyFeatureEnabled(
        volumeConfig.GetCloudId(),
        volumeConfig.GetFolderId());
}

////////////////////////////////////////////////////////////////////////////////

void TVolumeActor::HandleHttpInfo(
    const NMon::TEvRemoteHttpInfo::TPtr& ev,
    const TActorContext& ctx)
{
    using THttpHandler = void(TVolumeActor::*)(
        const NActors::TActorContext&,
        const TCgiParameters&,
        TRequestInfoPtr);

    using THttpHandlers = THashMap<TString, THttpHandler>;
    using TActor = TVolumeActor;

    const THttpHandlers postActions {{
        {"removeclient",           &TActor::HandleHttpInfo_RemoveClient          },
        {"resetmountseqnumber",    &TActor::HandleHttpInfo_ResetMountSeqNumber   },
        {"createCheckpoint",       &TActor::HandleHttpInfo_CreateCheckpoint      },
        {"deleteCheckpoint",       &TActor::HandleHttpInfo_DeleteCheckpoint      },
        {"startpartitions",        &TActor::HandleHttpInfo_StartPartitions       },
        {"changethrottlingpolicy", &TActor::HandleHttpInfo_ChangeThrottlingPolicy},
    }};

    const THttpHandlers getActions {{
        {"rendernpinfo", &TActor::HandleHttpInfo_RenderNonreplPartitionInfo },
    }};

    auto* msg = ev->Get();

    LOG_DEBUG(ctx, TBlockStoreComponents::VOLUME,
        "[%lu] HTTP request: %s",
        TabletID(),
        msg->Query.Quote().data());

    auto requestInfo = CreateRequestInfo(
        ev->Sender,
        ev->Cookie,
        MakeIntrusive<TCallContext>());

    LWTRACK(
        RequestReceived_Volume,
        requestInfo->CallContext->LWOrbit,
        "HttpInfo",
        requestInfo->CallContext->RequestId);

    if (State) {
        auto methodType = GetHttpMethodType(*msg);
        auto params = GatherHttpParameters(*msg);
        const auto& action = params.Get("action");

        if (auto* handler = postActions.FindPtr(action)) {
            if (methodType != HTTP_METHOD_POST) {
                RejectHttpRequest(ctx, *requestInfo, "Wrong HTTP method");
                return;
            }

            std::invoke(*handler, this, ctx, params, std::move(requestInfo));
            return;
        }

        if (auto* handler = getActions.FindPtr(action)) {
            std::invoke(*handler, this, ctx, params, std::move(requestInfo));
            return;
        }

        const auto& activeTab = params.Get("tab");

        HandleHttpInfo_Default(
            ctx,
            State->GetHistory(),
            State->GetMetaHistory(),
            activeTab,
            params,
            requestInfo);

        return;
    }

    TStringStream out;
    DumpTabletNotReady(out);

    SendHttpResponse(ctx, *requestInfo, std::move(out.Str()));
}

void TVolumeActor::HandleHttpInfo_Default(
    const NActors::TActorContext& ctx,
    const TDeque<THistoryLogItem>& history,
    const TVector<TVolumeMetaHistoryItem>& metaHistory,
    const TStringBuf tabName,
    const TCgiParameters& params,
    TRequestInfoPtr requestInfo)
{
    const auto& timestamp = params.Get("timestamp");
    ui64 ts = 0;

    if (timestamp && TryFromString(timestamp, ts)) {
        auto cancelRoutine =
        [] (const TActorContext& ctx, TRequestInfo& requestInfo)
        {
            TStringStream out;

            DumpTabletNotReady(out);
            NCloud::Reply(
                ctx,
                requestInfo,
                std::make_unique<NMon::TEvRemoteHttpInfoRes>(
                    out.Str()));
        };

        requestInfo->CancelRoutine = cancelRoutine;

        ProcessReadHistory(
            ctx,
            std::move(requestInfo),
            TInstant::MicroSeconds(ts),
            Config->GetVolumeHistoryCacheSize(),
            true);
        return;
    }

    const char* overviewTabName = "Overview";
    const char* historyTabName = "History";
    const char* checkpointsTabName = "Checkpoints";
    const char* tracesTabName = "Traces";
    const char* storageConfigTabName = "StorageConfig";

    const char* activeTab = "tab-pane active";
    const char* inactiveTab = "tab-pane";

    const char* overviewTab = inactiveTab;
    const char* historyTab = inactiveTab;
    const char* checkpointsTab = inactiveTab;
    const char* tracesTab = inactiveTab;
    const char* storageConfigTab = inactiveTab;

    if (tabName.Empty() || tabName == overviewTabName) {
        overviewTab = activeTab;
    } else if (tabName == historyTabName) {
        historyTab = activeTab;
    } else if (tabName == checkpointsTabName) {
        checkpointsTab = activeTab;
    } else if (tabName == tracesTabName) {
        tracesTab = activeTab;
    } else if (tabName == storageConfigTabName) {
        storageConfigTab = activeTab;
    }

    TStringStream out;

    HTML(out) {
        DIV_CLASS_ID("container-fluid", "tabs") {
            BuildVolumeTabs(out);

            DIV_CLASS("tab-content") {
                DIV_CLASS_ID(overviewTab, overviewTabName) {
                    DumpDefaultHeader(out, *Info(), SelfId().NodeId(), *DiagnosticsConfig);
                    RenderHtmlInfo(out, ctx.Now());
                }

                DIV_CLASS_ID(historyTab, historyTabName) {
                    RenderHistory(history, metaHistory, out);
                }

                DIV_CLASS_ID(checkpointsTab, checkpointsTabName) {
                    RenderCheckpoints(out);
                }

                DIV_CLASS_ID(tracesTab, tracesTabName) {
                    RenderTraces(out);
                }

                DIV_CLASS_ID(storageConfigTab, storageConfigTabName) {
                    RenderStorageConfig(out);
                }
            }
        }
    }

    SendHttpResponse(ctx, *requestInfo, std::move(out.Str()));
}

////////////////////////////////////////////////////////////////////////////////

void TVolumeActor::RenderHistory(
    const TDeque<THistoryLogItem>& history,
    const TVector<TVolumeMetaHistoryItem>& metaHistory,
    IOutputStream& out) const
{
    using namespace NMonitoringUtils;

    HTML(out) {
        DIV_CLASS("row") {
            TAG(TH3) { out << "History"; }
            TABLE_SORTABLE_CLASS("table table-bordered") {
                TABLEHEAD() {
                    TABLER() {
                        TABLEH() { out << "Time"; }
                        TABLEH() { out << "Host"; }
                        TABLEH() { out << "Operation type"; }
                        TABLEH() { out << "Result"; }
                        TABLEH() { out << "Operation"; }
                    }
                }
                for (const auto& h: history) {
                    TABLER() {
                        TABLED() {
                            out << h.Key.Timestamp;
                        }
                        TABLED() {
                            out << h.Operation.GetTabletHost();
                        }
                        TABLED() {
                            TStringBuf type;
                            if (h.Operation.HasAdd()) {
                                type = "Add";
                            } else if (h.Operation.HasRemove()) {
                                type = "Remove";
                            } else {
                                type = "Update";
                            }
                            out << type;
                        }
                        TABLED() {
                            if (FAILED(h.Operation.GetError().GetCode())) {
                                SerializeToTextFormat(h.Operation.GetError(), out);
                            } else {
                                out << "OK";
                            }
                        }
                        TABLED() {
                            if (h.Operation.HasAdd()) {
                                SerializeToTextFormat(h.Operation.GetAdd(), out);
                            } else if (h.Operation.HasRemove()) {
                                SerializeToTextFormat(h.Operation.GetRemove(), out);
                            } else {
                                SerializeToTextFormat(h.Operation.GetUpdateVolumeMeta(), out);
                            }
                        }
                    }
                }
            }
            if (history.size()) {
                TABLE_CLASS("table") {
                    TABLER() {
                        TABLED() {
                            BuildHistoryNextButton(out, history.back().Key, TabletID());
                        }
                    }
                }
            }
        }

        DIV_CLASS("row") {
            TAG(TH3) { out << "MetaHistory"; }
            TABLE_SORTABLE_CLASS("table table-bordered") {
                TABLEHEAD() {
                    TABLER() {
                        TABLEH() { out << "Time"; }
                        TABLEH() { out << "Meta"; }
                    }
                }
                auto it = metaHistory.rbegin();
                while (it != metaHistory.rend()) {
                    const auto& item = *it;

                    TABLER() {
                        TABLED() {
                            out << item.Timestamp;
                        }
                        TABLED() {
                            item.Meta.PrintJSON(out);
                        }
                    }

                    ++it;
                }
            }
        }
    }
}

void TVolumeActor::RenderCheckpoints(IOutputStream& out) const
{
    using namespace NMonitoringUtils;

    HTML(out) {
        DIV_CLASS("row") {
            TAG(TH3) {
                BuildMenuButton(out, "checkpoint-add");
                out << "Checkpoints";
            }
            out << "<div class='collapse form-group' id='checkpoint-add'>";
            BuildCreateCheckpointButton(out, TabletID());
            out << "</div>";

            TABLE_SORTABLE_CLASS("table table-bordered") {
                TABLEHEAD() {
                    TABLER() {
                        TABLEH() { out << "CheckpointId"; }
                        TABLEH() { out << "DataType"; }
                        TABLEH() { out << "Delete"; }
                    }
                }
                for (const auto& [r, checkpointType]:
                    State->GetCheckpointStore().GetActiveCheckpoints())
                {
                    TABLER() {
                        TABLED() { out << r; }
                        TABLED() { out << checkpointType; }
                        TABLED() {
                            BuildDeleteCheckpointButton(
                                out,
                                TabletID(),
                                r);
                        }
                    }
                }
            }
        };
        DIV_CLASS("row") {
            TAG(TH3) { out << "CheckpointRequests"; }
            TABLE_SORTABLE_CLASS("table table-bordered") {
                TABLEHEAD() {
                    TABLER() {
                        TABLEH() { out << "RequestId"; }
                        TABLEH() { out << "CheckpointId"; }
                        TABLEH() { out << "Timestamp"; }
                        TABLEH() { out << "State"; }
                        TABLEH() { out << "Type"; }
                    }
                }
                for (const auto& r:
                     State->GetCheckpointStore().GetCheckpointRequests())
                {
                    TABLER() {
                        TABLED() { out << r.RequestId; }
                        TABLED() { out << r.CheckpointId; }
                        TABLED() { out << r.Timestamp; }
                        TABLED() { out << r.State; }
                        TABLED() { out << r.ReqType; }
                    }
                }
            }
        }
        out << R"___(
            <script type='text/javascript'>
            function createCheckpoint() {
                document.forms['CreateCheckpoint'].submit();
            }
            function deleteCheckpoint(checkpointId) {
                document.forms['DeleteCheckpoint_' + checkpointId].submit();
            }
            </script>
        )___";
    }
}

void TVolumeActor::RenderTraces(IOutputStream& out) const
{
    using namespace NMonitoringUtils;
    const auto& diskId = State->GetMeta().GetVolumeConfig().GetDiskId();
    HTML(out) {
        DIV_CLASS("row") {
            TAG(TH3) {
                out << "<a href=\"../tracelogs/slow?diskId=" << diskId << "\">"
                        "Slow logs for " << diskId << "</a><br>";
                out << "<a href=\"../tracelogs/random?diskId=" << diskId << "\">"
                       "Random samples for " << diskId << "</a>";
            }
        }
    }
}

void TVolumeActor::RenderStorageConfig(IOutputStream& out) const
{
    using namespace NMonitoringUtils;

    HTML(out) {
        DIV_CLASS("row") {
            TAG(TH3) {
                out << "StorageConfig";
            }
            TABLE_SORTABLE_CLASS("table table-bordered") {
                TABLEHEAD() {
                    TABLER() {
                        TABLEH() { out << "Field name"; }
                        TABLEH() { out << "Value"; }
                    }
                }
                const auto protoValues = Config->GetStorageConfigProto();
                constexpr i32 expectedNonRepeatedFieldIndex = -1;
                auto descriptor = protoValues.GetDescriptor();
                if (descriptor == nullptr) {
                    return;
                }

                const auto* reflection =
                    NProto::TStorageServiceConfig::GetReflection();

                for (int i = 0; i < descriptor->field_count(); ++i)
                {
                    TStringBuilder value;
                    const auto field_descriptor = descriptor->field(i);
                    if (field_descriptor->is_repeated()) {
                        const auto repeatedSize =
                            reflection->FieldSize(protoValues, field_descriptor);
                        if (!repeatedSize) {
                            continue;
                        }

                        for (int j = 0; j < repeatedSize; ++j) {
                            TString curValue;
                            google::protobuf::TextFormat::PrintFieldValueToString(
                                protoValues,
                                field_descriptor,
                                j,
                                &curValue);
                            value.append(curValue);
                            value.append("; ");
                        }
                    } else if (
                        reflection->HasField(protoValues, field_descriptor))
                    {
                        google::protobuf::TextFormat::PrintFieldValueToString(
                            protoValues,
                            field_descriptor,
                            expectedNonRepeatedFieldIndex,
                            &value);
                    } else {
                        continue;
                    }

                    TABLER() {
                        TABLED() {
                            out << field_descriptor->name();;
                        }
                        TABLED() {
                            out << value;
                        }
                    }
                }
            }
        }
    }
}

void TVolumeActor::RenderHtmlInfo(IOutputStream& out, TInstant now) const
{
    using namespace NMonitoringUtils;

    if (!State) {
        return;
    }

    const auto mediaKind = State->GetConfig().GetStorageMediaKind();

    HTML(out) {
        DIV_CLASS("row") {
            DIV_CLASS("col-md-6") {
                DumpSolomonVolumeLink(out, *DiagnosticsConfig, State->GetDiskId());
            }
        }

        DIV_CLASS("row") {
            DIV_CLASS("col-md-6") {
                RenderStatus(out);
            }
        }

        if (IsDiskRegistryMediaKind(mediaKind)) {
            DIV_CLASS("row") {
                DIV_CLASS("col-md-6") {
                    RenderMigrationStatus(out);
                }
            }
        }

        if (IsReliableDiskRegistryMediaKind(mediaKind)) {
            DIV_CLASS("row") {
                DIV_CLASS("col-md-6") {
                    RenderResyncStatus(out);
                }
            }
        }

        DIV_CLASS("row") {
            DIV_CLASS("col-md-6") {
                RenderCommonButtons(out);
            }
        }

        DIV_CLASS("row") {
            DIV_CLASS("col-md-6") {
                RenderConfig(out);
            }
        }

        DIV_CLASS("row") {
            RenderTabletList(out);
        }

        DIV_CLASS("row") {
            RenderClientList(out, now);
        }

        DIV_CLASS("row") {
            RenderMountSeqNumber(out);
        }
    }
}

void TVolumeActor::RenderTabletList(IOutputStream& out) const
{
    HTML(out) {
        TAG(TH3) { out << "Partitions"; }
        TABLE_SORTABLE_CLASS("table table-bordered") {
            TABLEHEAD() {
                TABLER() {
                    TABLEH() { out << "Partition"; }
                    TABLEH() { out << "Status"; }
                    TABLEH() { out << "Blocks Count"; }
                }
            }

            for (const auto& partition: State->GetPartitions()) {
                TABLER() {
                    TABLED() {
                        out << "<a href='../tablets?TabletID="
                            << partition.TabletId
                            << "'>"
                            << partition.TabletId
                            << "</a>";
                    }
                    TABLED() {
                        out << partition.GetStatus();
                    }
                    TABLED() {
                        out << partition.PartitionConfig.GetBlocksCount();
                    }
                }
            }

            const auto mediaKind = State->GetConfig().GetStorageMediaKind();
            if (IsDiskRegistryMediaKind(mediaKind)) {
                TABLER() {
                    TABLED() {
                        out << "<a href='?action=rendernpinfo"
                            << "&TabletID=" << TabletID()
                            << "'>"
                            << "nonreplicated partition"
                            << "</a>";
                    }
                    TABLED() {
                    }
                    TABLED() {
                        out << State->GetConfig().GetBlocksCount();
                    }
                }
            }
        }
    }
}

void TVolumeActor::RenderConfig(IOutputStream& out) const
{
    if (!State) {
        return;
    }

    const auto& volumeConfig = State->GetMeta().GetVolumeConfig();

    ui64 blocksCount = GetBlocksCount();
    ui32 blockSize = volumeConfig.GetBlockSize();
    const double blocksPerStripe = volumeConfig.GetBlocksPerStripe();
    const double stripes = blocksPerStripe
        ? double(blocksCount) / blocksPerStripe
        : 0;

    HTML(out) {
        TAG(TH3) { out << "Volume Config"; }
        TABLE_SORTABLE_CLASS("table table-condensed") {
            TABLEHEAD() {
                TABLER() {
                    TABLED() { out << "Disk Id"; }
                    TABLED() { out << volumeConfig.GetDiskId(); }
                }
                TABLER() {
                    TABLED() { out << "Base Disk Id"; }
                    TABLED() { out << volumeConfig.GetBaseDiskId(); }
                }
                TABLER() {
                    TABLED() { out << "Base Disk Checkpoint Id"; }
                    TABLED() { out << volumeConfig.GetBaseDiskCheckpointId(); }
                }
                TABLER() {
                    TABLED() { out << "Folder Id"; }
                    TABLED() { out << volumeConfig.GetFolderId(); }
                }
                TABLER() {
                    TABLED() { out << "Cloud Id"; }
                    TABLED() { out << volumeConfig.GetCloudId(); }
                }
                TABLER() {
                    TABLED() { out << "Project Id"; }
                    TABLED() { out << volumeConfig.GetProjectId(); }
                }

                TABLER() {
                    TABLED() { out << "Block size"; }
                    TABLED() { out << blockSize; }
                }

                TABLER() {
                    TABLED() { out << "Blocks count"; }
                    TABLED() {
                        out << blocksCount;
                        out << " ("
                            << FormatByteSize(blocksCount * blockSize)
                            << ")";
                    }
                }

                if (State->GetUsedBlocks()) {
                    const auto used = State->GetUsedBlocks()->Count();

                    TABLER() {
                        TABLED() { out << "Used blocks count"; }
                        TABLED() {
                            out << used;
                            out << " ("
                                << FormatByteSize(used * blockSize)
                                << ")";
                        }
                    }
                }

                TABLER() {
                    TABLED() { out << "BlocksPerStripe"; }
                    TABLED() {
                        out << blocksPerStripe;
                        out << " ("
                            << FormatByteSize(blocksPerStripe * blockSize)
                            << ")";
                    }
                }

                TABLER() {
                    TABLED() { out << "Stripes"; }
                    TABLED() { out << stripes << Endl; }
                }

                TABLER() {
                    TABLED() { out << "StripesPerPartition"; }
                    TABLED() {
                        out << (stripes / volumeConfig.PartitionsSize()) << Endl;
                    }
                }

                TABLER() {
                    TABLED() { out << "Number of channels"; }
                    TABLED() { out << volumeConfig.ExplicitChannelProfilesSize(); }
                }

                TABLER() {
                    TABLED() { out << "Storage media kind"; }
                    TABLED() {
                        out << NCloud::NProto::EStorageMediaKind_Name(
                            (NCloud::NProto::EStorageMediaKind)volumeConfig.GetStorageMediaKind());
                    }
                }

                TABLER() {
                    TABLED() { out << "Encryption mode"; }
                    TABLED() {
                        out << NProto::EEncryptionMode_Name(
                            (NProto::EEncryptionMode)volumeConfig.GetEncryptionDesc().GetMode());
                    }
                }

                TABLER() {
                    TABLED() { out << "Channel profile id"; }
                    TABLED() {
                        out << volumeConfig.GetChannelProfileId();
                    }
                }

                TABLER() {
                    TABLED() { out << "Partition tablet version"; }
                    TABLED() {
                        out << volumeConfig.GetTabletVersion();
                    }
                }

                TABLER() {
                    TABLED() { out << "Tags"; }
                    TABLED() {
                        out << volumeConfig.GetTagsStr();
                    }
                }

                TABLER() {
                    TABLED() { out << "UseRdma"; }
                    TABLED() {
                        out << State->GetUseRdma();
                    }
                }

                TABLER() {
                    TABLED() { out << "Throttler"; }
                    TABLED() {
                        const auto& tp = State->GetThrottlingPolicy();
                        TABLE_CLASS("table table-condensed") {
                            TABLEBODY() {
                                if (Throttler) {
                                    TABLER() {
                                        TABLED() { out << "PostponedQueueSize"; }
                                        TABLED() { out << Throttler->GetPostponedRequestsCount(); }
                                    }
                                }
                                TABLER() {
                                    TABLED() { out << "WriteCostMultiplier"; }
                                    TABLED() { out << tp.GetWriteCostMultiplier(); }
                                }
                                TABLER() {
                                    TABLED() { out << "PostponedQueueWeight"; }
                                    TABLED() { out << tp.CalculatePostponedWeight(); }
                                }
                                TABLER() {
                                    TABLED() { out << "BackpressureFeatures"; }
                                    TABLED() {
                                        const auto& bp = tp.GetCurrentBackpressure();
                                        TABLE_CLASS("table table-condensed") {
                                            TABLEBODY() {
                                                TABLER() {
                                                    TABLED() { out << "FreshIndexScore"; }
                                                    TABLED() { out << bp.FreshIndexScore; }
                                                }
                                                TABLER() {
                                                    TABLED() { out << "CompactionScore"; }
                                                    TABLED() { out << bp.CompactionScore; }
                                                }
                                                TABLER() {
                                                    TABLED() { out << "DiskSpaceScore"; }
                                                    TABLED() { out << bp.DiskSpaceScore; }
                                                }
                                                TABLER() {
                                                    TABLED() { out << "CleanupScore"; }
                                                    TABLED() { out << bp.CleanupScore; }
                                                }
                                            }
                                        }
                                    }
                                }
                                TABLER() {
                                    TABLED() { out << "ThrottlerParams"; }
                                    TABLED() {
                                        TABLE_CLASS("table table-condensed") {
                                            TABLEBODY() {
                                                TABLER() {
                                                    TABLED() { out << "ReadC1"; }
                                                    TABLED() { out << tp.C1(TVolumeThrottlingPolicy::EOpType::Read); }
                                                }
                                                TABLER() {
                                                    TABLED() { out << "ReadC2"; }
                                                    TABLED() { out << FormatByteSize(tp.C2(TVolumeThrottlingPolicy::EOpType::Read)); }
                                                }
                                                TABLER() {
                                                    TABLED() { out << "WriteC1"; }
                                                    TABLED() { out << tp.C1(TVolumeThrottlingPolicy::EOpType::Write); }
                                                }
                                                TABLER() {
                                                    TABLED() { out << "WriteC2"; }
                                                    TABLED() { out << FormatByteSize(tp.C2(TVolumeThrottlingPolicy::EOpType::Write)); }
                                                }
                                                TABLER() {
                                                    TABLED() { out << "DescribeC1"; }
                                                    TABLED() { out << tp.C1(TVolumeThrottlingPolicy::EOpType::Describe); }
                                                }
                                                TABLER() {
                                                    TABLED() { out << "DescribeC2"; }
                                                    TABLED() { out << FormatByteSize(tp.C2(TVolumeThrottlingPolicy::EOpType::Describe)); }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                TABLER() {
                    TABLED() { out << "ExplicitChannelProfiles"; }
                    TABLED() {
                        if (volumeConfig.ExplicitChannelProfilesSize()) {
                            out << "Yes";
                        } else {
                            out << "No";
                        }
                    }
                }
            }
        }
    }
}

void TVolumeActor::RenderClientList(IOutputStream& out, TInstant now) const
{
    if (!State) {
        return;
    }

    const auto& clients = State->GetClients();
    if (clients.empty()) {
        return;
    }

    const auto& diskId = State->GetDiskId();

    HTML(out) {
        TAG(TH3) { out << "Volume Clients"; }
        TABLE_SORTABLE_CLASS("table table-condensed") {
            TABLEHEAD() {
                TABLER() {
                    TABLEH() { out << "Client Id"; }
                    TABLEH() { out << "Access mode"; }
                    TABLEH() { out << "Mount mode"; }
                    TABLEH() { out << "Disconnect time"; }
                    TABLEH() { out << "Last activity time"; }
                    TABLEH() { out << "State"; }
                    TABLEH() { out << "Pipe server actor"; }
                    TABLEH() { out << "Remove"; }
                }
            }
            ui64 clientNo = 0;
            for (const auto& pair: clients) {
                const auto& clientId = pair.first;
                const auto& clientInfo = pair.second;
                const auto& volumeClientInfo = clientInfo.GetVolumeClientInfo();

                OutputClientInfo(
                    out,
                    clientId,
                    diskId,
                    clientNo++,
                    TabletID(),
                    volumeClientInfo.GetVolumeMountMode(),
                    volumeClientInfo.GetVolumeAccessMode(),
                    volumeClientInfo.GetDisconnectTimestamp(),
                    volumeClientInfo.GetLastActivityTimestamp(),
                    State->IsClientStale(clientInfo, now),
                    clientInfo.GetPipes());
            }
        }

        out << R"___(
                <script>
                function volumeRemoveClient(clientId) {
                    document.forms["RemoveClient_" + clientId].submit();
                }
                </script>
            )___";
    }
}

void TVolumeActor::RenderMountSeqNumber(IOutputStream& out) const
{
    if (!State) {
        return;
    }

    const auto& clients = State->GetClients();
    if (clients.empty()) {
        return;
    }

    HTML(out) {
        TAG(TH3) { out << "Volume Mount Sequence Number"; }
        TABLE_SORTABLE_CLASS("table table-condensed") {
            TABLEHEAD() {
                TABLER() {
                    TABLEH() { out << "Client Id"; }
                    TABLEH() { out << "Mount Generation"; }
                    TABLEH() { out << "Reset"; }
                }
            }

            TABLER() {
                const auto& rwClient = State->GetReadWriteAccessClientId();
                TABLED() { out << rwClient; }
                TABLED() { out << State->GetMountSeqNumber(); }
                TABLED() {
                    BuildVolumeResetMountSeqNumberButton(
                        out,
                        rwClient,
                        TabletID());
                }
            }
        }

        out << R"___(
                <script>
                function resetMountSeqNumber(clientId) {
                    document.forms["ResetMountSeqNumber_" + clientId].submit();
                }
                </script>
            )___";
    }
}

void TVolumeActor::RenderStatus(IOutputStream& out) const
{
    HTML(out) {
        TAG(TH3) {
            TString statusText = "offline";
            TString cssClass = "label-danger";

            auto status = GetVolumeStatus();
            if (status == TVolumeActor::STATUS_INACTIVE) {
                statusText = GetVolumeStatusString(status);
                cssClass = "label-default";
            } else if (status != TVolumeActor::STATUS_OFFLINE) {
                statusText = GetVolumeStatusString(status);
                cssClass = "label-success";
            }

            out << "Status:";

            SPAN_CLASS_STYLE("label " + cssClass, "margin-left:10px") {
                out << statusText;
            }
        }
    }
}

void TVolumeActor::RenderMigrationStatus(IOutputStream& out) const
{
    HTML(out) {
        bool active = State->GetMeta().GetMigrations().size()
            || State->GetMeta().GetFreshDeviceIds().size();
        TStringBuf label = State->GetMeta().GetFreshDeviceIds().empty()
            ? "Migration" : "Replication";

        TAG(TH3) {
            TString statusText = "inactive";
            TString cssClass = "label-default";

            if (active) {
                statusText = "in progress";
                cssClass = "label-success";
            }

            out << label << "Status:";

            SPAN_CLASS_STYLE("label " + cssClass, "margin-left:10px") {
                out << statusText;
            }
        }

        if (!active) {
            return;
        }

        const auto totalBlocks = GetBlocksCount();
        const auto migrationIndex = State->GetMeta().GetMigrationIndex();
        const auto blockSize = State->GetBlockSize();

        TABLE_SORTABLE_CLASS("table table-condensed") {
            TABLEHEAD() {
                TABLER() {
                    TABLED() { out << label << " index: "; }
                    TABLED() {
                        OutputProgress(
                            migrationIndex,
                            totalBlocks,
                            blockSize,
                            out);
                    }
                }
            }
        }
    }
}

void TVolumeActor::RenderResyncStatus(IOutputStream& out) const
{
    HTML(out) {
        TAG(TH3) {
            TString statusText = "inactive";
            TString cssClass = "label-default";

            if (State->IsMirrorResyncNeeded()) {
                statusText = "in progress";
                cssClass = "label-success";
            }

            out << "ResyncStatus:";

            SPAN_CLASS_STYLE("label " + cssClass, "margin-left:10px") {
                out << statusText;
            }
        }

        if (!State->IsMirrorResyncNeeded()) {
            return;
        }

        const auto totalBlocks = GetBlocksCount();
        const auto resyncIndex = State->GetMeta().GetResyncIndex();
        const auto blockSize = State->GetBlockSize();

        TABLE_SORTABLE_CLASS("table table-condensed") {
            TABLEHEAD() {
                TABLER() {
                    TABLED() { out << "Resync index: "; }
                    TABLED() {
                        OutputProgress(
                            resyncIndex,
                            totalBlocks,
                            blockSize,
                            out);
                    }
                }
            }
        }
    }
}

void TVolumeActor::RenderCommonButtons(IOutputStream& out) const
{
    if (!State) {
        return;
    }

    HTML(out) {
        TAG(TH3) { out << "Controls"; }
        TABLE_SORTABLE_CLASS("table table-condensed") {
            TABLER() {
                TABLED() {
                    BuildStartPartitionsButton(
                        out,
                        State->GetDiskId(),
                        TabletID());
                }
            }
        }

        out << R"___(
                <script>
                function startPartitions() {
                    document.forms["StartPartitions"].submit();
                }
                </script>
            )___";

        if (CanChangeThrottlingPolicy()) {
            TABLE_SORTABLE_CLASS("table table-condensed") {
                TABLER() {
                    TABLED() {
                        BuildChangeThrottlingPolicyButton(
                            out,
                            State->GetDiskId(),
                            State->GetConfig().GetPerformanceProfile(),
                            TabletID());
                    }
                }
            }

            out << R"___(
                    <script>
                    function changeThrottlingPolicy() {
                        document.forms["ChangeThrottlingPolicy"].submit();
                    }
                    </script>
                )___";
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

void TVolumeActor::HandleHttpInfo_StartPartitions(
    const TActorContext& ctx,
    const TCgiParameters& params,
    TRequestInfoPtr requestInfo)
{
    Y_UNUSED(params);

    StartPartitionsIfNeeded(ctx);

    SendHttpResponse(ctx, *requestInfo, "Start initiated", EAlertLevel::SUCCESS);
}

void TVolumeActor::HandleHttpInfo_ChangeThrottlingPolicy(
    const TActorContext& ctx,
    const TCgiParameters& params,
    TRequestInfoPtr requestInfo)
{
    if (!CanChangeThrottlingPolicy()) {
        SendHttpResponse(
            ctx,
            *requestInfo,
            "ThrottlingPolicy can't be changed for this volume",
            EAlertLevel::WARNING);

        return;
    }

    NProto::TVolumePerformanceProfile pp =
        State->GetConfig().GetPerformanceProfile();
    auto getParam = [&] (const TStringBuf name) {
        const auto& s = params.Get(name);
        return FromStringWithDefault(s, Max<ui32>());
    };
    const auto maxReadIops = getParam("MaxReadIops");
    const auto maxWriteIops = getParam("MaxWriteIops");
    const auto maxReadBandwidth = getParam("MaxReadBandwidth");
    const auto maxWriteBandwidth = getParam("MaxWriteBandwidth");

    pp.SetMaxReadIops(Min(pp.GetMaxReadIops(), maxReadIops));
    pp.SetMaxWriteIops(Min(pp.GetMaxWriteIops(), maxWriteIops));
    pp.SetMaxReadBandwidth(Min(pp.GetMaxReadBandwidth(), maxReadBandwidth));
    pp.SetMaxWriteBandwidth(Min(pp.GetMaxWriteBandwidth(), maxWriteBandwidth));

    State->ResetThrottlingPolicy(pp);

    SendHttpResponse(
        ctx,
        *requestInfo,
        "ThrottlingPolicy changed",
        EAlertLevel::SUCCESS);
}

void TVolumeActor::HandleHttpInfo_RenderNonreplPartitionInfo(
    const NActors::TActorContext& ctx,
    const TCgiParameters& params,
    TRequestInfoPtr requestInfo)
{
    Y_UNUSED(params);

    TStringStream out;

    auto meta = State->GetMeta();
    if (!State->GetNonreplicatedPartitionConfig()) {
        HTML(out) {
            out << "no config, allocation result: "
                << FormatError(StorageAllocationResult);
        }

        return;
    }

    auto config = *State->GetNonreplicatedPartitionConfig();

    HTML(out) {
        BODY() {
            TABLE_CLASS("table table-bordered") {
                TABLEHEAD() {
                    TABLER() {
                        TABLEH() { out << "Property"; }
                        TABLEH() { out << "Value"; }
                    }
                }
                TABLER() {
                    TABLED() { out << "BlockSize"; }
                    TABLED() { out << config.GetBlockSize(); }
                }
                TABLER() {
                    TABLED() { out << "Blocks"; }
                    TABLED() { out << config.GetBlockCount(); }
                }
                TABLER() {
                    TABLED() { out << "IOMode"; }
                    TABLED() { out << meta.GetIOMode(); }
                }
                TABLER() {
                    TABLED() { out << "IOModeTs"; }
                    TABLED() { out << meta.GetIOModeTs(); }
                }
                TABLER() {
                    TABLED() { out << "MuteIOErrors"; }
                    TABLED() { out << meta.GetMuteIOErrors(); }
                }
                TABLER() {
                    TABLED() { out << "MaxTimedOutDeviceStateDuration"; }
                    TABLED() {
                        out << config.GetMaxTimedOutDeviceStateDuration() ;
                        if (config.IsMaxTimedOutDeviceStateDurationOverridden()) {
                            out << "(overridden)";
                        }
                    }
                }

                auto& infos = State->GetPartitionStatInfos();
                if (infos && infos.front().LastCounters) {
                    const auto& counters = infos.front().LastCounters;
                    TABLER() {
                        TABLED() { out << "HasBrokenDevice"; }
                        TABLED() {
                            out << counters->Simple.HasBrokenDevice.Value;
                        }
                    }
                    TABLER() {
                        TABLED() { out << "HasBrokenDeviceSilent"; }
                        TABLED() {
                            out << counters->Simple.HasBrokenDeviceSilent.Value;
                        }
                    }
                }

                auto findMigration =
                    [&] (const TString& deviceId) -> const NProto::TDeviceConfig* {
                        for (const auto& migration: meta.GetMigrations()) {
                            if (migration.GetSourceDeviceId() == deviceId) {
                                return &migration.GetTargetDevice();
                            }
                        }

                        return nullptr;
                    };

                auto outputDevices = [&] (const TDevices& devices) {
                    TABLED() {
                        TABLE_CLASS("table table-bordered") {
                            TABLEHEAD() {
                                TABLER() {
                                    TABLEH() { out << "DeviceNo"; }
                                    TABLEH() { out << "Offset"; }
                                    TABLEH() { out << "NodeId"; }
                                    TABLEH() { out << "TransportId"; }
                                    TABLEH() { out << "RdmaEndpoint"; }
                                    TABLEH() { out << "DeviceName"; }
                                    TABLEH() { out << "DeviceUUID"; }
                                    TABLEH() { out << "BlockSize"; }
                                    TABLEH() { out << "Blocks"; }
                                    TABLEH() { out << "BlockRange"; }
                                }
                            }

                            ui64 currentOffset = 0;
                            ui64 currentBlockCount = 0;
                            auto outputDevice = [&] (const NProto::TDeviceConfig& d) {
                                TABLED() { out << d.GetNodeId(); }
                                TABLED() { out << d.GetTransportId(); }
                                TABLED() {
                                    auto e = d.GetRdmaEndpoint();
                                    out << e.GetHost() << ":" << e.GetPort();
                                }
                                TABLED() { out << d.GetDeviceName(); }
                                TABLED() {
                                    const bool isFresh = Find(
                                        meta.GetFreshDeviceIds().begin(),
                                        meta.GetFreshDeviceIds().end(),
                                        d.GetDeviceUUID()
                                    ) != meta.GetFreshDeviceIds().end();

                                    if (isFresh) {
                                        out << "<font color=blue>";
                                    }
                                    out << d.GetDeviceUUID();
                                    if (isFresh) {
                                        out << "</font>";
                                    }
                                }
                                TABLED() { out << d.GetBlockSize(); }
                                TABLED() { out << d.GetBlocksCount(); }
                                TABLED() {
                                    const auto currentRange =
                                        TBlockRange64::WithLength(
                                            currentBlockCount,
                                            d.GetBlocksCount());
                                    out << DescribeRange(currentRange);
                                }
                            };

                            for (int i = 0; i < devices.size(); ++i) {
                                const auto& device = devices[i];

                                TABLER() {
                                    TABLED() { out << i; }
                                    TABLED() {
                                        out << FormatByteSize(currentOffset);
                                    }
                                    outputDevice(device);
                                }

                                auto* m = findMigration(device.GetDeviceUUID());
                                if (m) {
                                    TABLER() {
                                        TABLED() { out << i << " migration"; }
                                        TABLED() {
                                            out << FormatByteSize(currentOffset);
                                        }
                                        outputDevice(*m);
                                    }
                                }

                                currentBlockCount += device.GetBlocksCount();
                                currentOffset +=
                                    device.GetBlocksCount() * device.GetBlockSize();
                            }
                        }
                    }
                };

                TABLER() {
                    TABLED() { out << "Devices"; }
                    outputDevices(config.GetDevices());
                }

                for (ui32 i = 0; i < meta.ReplicasSize(); ++i) {
                    TABLER() {
                        TABLED() {
                            out << "Devices (Replica " << (i + 1) << ")";
                        }
                        outputDevices(meta.GetReplicas(i).GetDevices());
                    }
                }
            }
        }
    }

    SendHttpResponse(ctx, *requestInfo, std::move(out.Str()));
}

////////////////////////////////////////////////////////////////////////////////

void TVolumeActor::RejectHttpRequest(
    const TActorContext& ctx,
    TRequestInfo& requestInfo,
    TString message)
{
    LOG_ERROR_S(ctx, TBlockStoreComponents::VOLUME, message);

    SendHttpResponse(ctx, requestInfo, std::move(message), EAlertLevel::DANGER);
}

void TVolumeActor::SendHttpResponse(
    const TActorContext& ctx,
    TRequestInfo& requestInfo,
    TString message,
    EAlertLevel alertLevel)
{
    TStringStream out;
    BuildTabletNotifyPageWithRedirect(out, message, TabletID(), alertLevel);

    SendHttpResponse(ctx, requestInfo, std::move(out.Str()));
}

void TVolumeActor::SendHttpResponse(
    const TActorContext& ctx,
    TRequestInfo& requestInfo,
    TString message)
{
    LWTRACK(
        ResponseSent_Volume,
        requestInfo.CallContext->LWOrbit,
        "HttpInfo",
        requestInfo.CallContext->RequestId);

    NCloud::Reply(
        ctx,
        requestInfo,
        std::make_unique<NMon::TEvRemoteHttpInfoRes>(std::move(message)));
}

}   // namespace NCloud::NBlockStore::NStorage
