#include "schemeshard__operation_part.h"
#include "schemeshard__operation_common.h"
#include "schemeshard_olap_types.h"
#include "schemeshard_impl.h"

#include <contrib/ydb/core/base/subdomain.h>
#include <contrib/ydb/core/tx/columnshard/columnshard.h>
#include <contrib/ydb/core/mind/hive/hive.h>

#include <util/random/shuffle.h>

namespace NKikimr::NSchemeShard {

namespace {

class TTableConstructorBase {
public:
    static constexpr ui32 DEFAULT_SHARDS_COUNT = 64;

private:
    ui32 ShardsCount = 0;
    TString Name;
    std::optional<NKikimrSchemeOp::TColumnDataLifeCycle> TtlSettings;
    std::optional<NKikimrSchemeOp::TColumnTableSharding> Sharding;

public:
    bool Deserialize(const NKikimrSchemeOp::TColumnTableDescription& description, IErrorCollector& errors) {
        Name = description.GetName();
        if (description.HasRESERVED_TtlSettingsPresetName() || description.HasRESERVED_TtlSettingsPresetId()) {
            errors.AddError("TTL presets are not supported");
            return false;
        }

        if (description.HasRESERVED_TtlSettingsPresetName() || description.HasRESERVED_TtlSettingsPresetId()) {
            errors.AddError("TTL presets are not supported");
            return false;
        }

        ShardsCount = description.GetColumnShardCount();

        if (description.HasSharding()) {
            Sharding = description.GetSharding();
        } else {
            Sharding = DefaultSharding();
        }

        if (!DoDeserialize(description, errors)) {
            return false;
        }

        if (description.HasTtlSettings()) {
            TtlSettings = description.GetTtlSettings();
            if (!GetSchema().ValidateTtlSettings(description.GetTtlSettings(), errors)) {
                return false;
            }
        }
        return true;
    }

    TColumnTableInfo::TPtr BuildTableInfo(IErrorCollector& errors) const {
        TColumnTableInfo::TPtr tableInfo = new TColumnTableInfo;
        tableInfo->AlterVersion = 1;

        BuildDescription(tableInfo->Description);
        tableInfo->Description.SetColumnShardCount(ShardsCount);
        tableInfo->Description.SetName(Name);
        if (TtlSettings) {
            tableInfo->Description.MutableTtlSettings()->CopyFrom(*TtlSettings);
            tableInfo->Description.MutableTtlSettings()->SetVersion(1);
        }
        if (!SetSharding(GetSchema(), tableInfo, errors)) {
            return nullptr;
        }
        return tableInfo;
    }

private:
    virtual void BuildDescription(NKikimrSchemeOp::TColumnTableDescription& description) const = 0;
    virtual bool DoDeserialize(const NKikimrSchemeOp::TColumnTableDescription& description, IErrorCollector& errors) = 0;
    virtual const TOlapSchema& GetSchema() const = 0;

    bool SetSharding(const TOlapSchema& schema, TColumnTableInfo::TPtr tableInfo, IErrorCollector& errors) const {
        if (!ShardsCount) {
            errors.AddError("Shards count is zero");
            return false;
        }
        if (!Sharding) {
            errors.AddError("Sharding is not set");
            return false;
        }
        tableInfo->Sharding = *Sharding;

        switch (tableInfo->Sharding.Method_case()) {
            case NKikimrSchemeOp::TColumnTableSharding::kRandomSharding: {
                // Random sharding implies non-unique primary key
                if (ShardsCount > 1) {
                    tableInfo->Sharding.SetUniquePrimaryKey(false);
                }
                break;
            }
            case NKikimrSchemeOp::TColumnTableSharding::kHashSharding: {
                auto& sharding = *tableInfo->Sharding.MutableHashSharding();
                if (sharding.ColumnsSize() == 0) {
                    sharding.MutableColumns()->CopyFrom(tableInfo->Description.GetSchema().GetKeyColumnNames());
                }
                if (ShardsCount > 1 && sharding.ColumnsSize() == 0) {
                    errors.AddError("Hash sharding requires a non-empty list of columns or primary key specified");
                    return false;
                }
                for (const TString& columnName : sharding.GetColumns()) {
                    auto* pColumn = schema.GetColumnByName(columnName);
                    if (!pColumn) {
                        errors.AddError(Sprintf("Hash sharding is using an unknown column '%s'", columnName.c_str()));
                        return false;
                    }
                    if (!pColumn->IsKeyColumn()) {
                        errors.AddError(Sprintf("Hash sharding is using a non-key column '%s'", columnName.c_str()));
                        return false;
                    }
                }
                sharding.SetUniqueShardKey(true);
                tableInfo->Sharding.SetUniquePrimaryKey(true);
                break;
            }
            default: {
                errors.AddError("Unsupported sharding method");
                return false;
            }
        }
        return true;
    }

    static NKikimrSchemeOp::TColumnTableSharding DefaultSharding() {
        NKikimrSchemeOp::TColumnTableSharding sharding;
        auto* hashSharding = sharding.MutableHashSharding();
        hashSharding->SetFunction(NKikimrSchemeOp::TColumnTableSharding::THashSharding::HASH_FUNCTION_MODULO_N);
        return sharding;
    }
};

class TOlapPresetConstructor : public TTableConstructorBase {
    ui32 PresetId = 0;
    TString PresetName = "default";
    const TOlapStoreInfo& StoreInfo;

public:
    TOlapPresetConstructor(const TOlapStoreInfo& storeInfo)
        : StoreInfo(storeInfo)
    {}

    bool DoDeserialize(const NKikimrSchemeOp::TColumnTableDescription& description, IErrorCollector& errors) override {
        if (description.GetColumnShardCount() > StoreInfo.GetColumnShards().size()) {
            errors.AddError(Sprintf("Cannot create table with %" PRIu32 " column shards, only %" PRIu32 " are available",
                description.GetColumnShardCount(), ui32(StoreInfo.GetColumnShards().size())));
            return false;
        }

        if (description.HasSchemaPresetId()) {
            PresetId = description.GetSchemaPresetId();
            if (!StoreInfo.SchemaPresets.contains(PresetId)) {
                errors.AddError(Sprintf("Specified schema preset %" PRIu32 " does not exist in tablestore", PresetId));
                return false;
            }
            PresetName = StoreInfo.SchemaPresets.at(PresetId).GetName();
        } else {
            if (description.HasSchemaPresetName()) {
                PresetName = description.GetSchemaPresetName();
            }
            if (!StoreInfo.SchemaPresetByName.contains(PresetName)) {
                errors.AddError(Sprintf("Specified schema preset '%s' does not exist in tablestore", PresetName.c_str()));
                return false;
            }
            PresetId = StoreInfo.SchemaPresetByName.at(PresetName);
            Y_ABORT_UNLESS(StoreInfo.SchemaPresets.contains(PresetId));
        }

        if (description.HasSchema()) {
            if (!GetSchema().Validate(description.GetSchema(), errors)) {
                return false;
            }
        }
        return true;
    }

private:
    void BuildDescription(NKikimrSchemeOp::TColumnTableDescription& description) const override {
        description.SetSchemaPresetId(PresetId);
        description.SetSchemaPresetName(PresetName);
    }

    const TOlapSchema& GetSchema() const override {
        return StoreInfo.SchemaPresets.at(PresetId);
    };
};

class TOlapTableConstructor : public TTableConstructorBase {
    TOlapSchema TableSchema;
    bool HasDataChannels = false;
private:
    bool DoDeserialize(const NKikimrSchemeOp::TColumnTableDescription& description, IErrorCollector& errors) override {
        if (description.HasSchemaPresetName() || description.HasSchemaPresetId()) {
            errors.AddError("Schema presets are not supported for standalone column tables");
            return false;
        }

        if (!description.HasSchema()) {
            errors.AddError("No schema for column table specified");
            return false;
        }

        HasDataChannels = description.GetStorageConfig().HasDataChannelCount();

        TOlapSchemaUpdate schemaDiff;
        if (!schemaDiff.Parse(description.GetSchema(), errors)) {
            return false;
        }

        if (!TableSchema.Update(schemaDiff, errors)) {
            return false;
        }
        return true;
    }

private:
    void BuildDescription(NKikimrSchemeOp::TColumnTableDescription& description) const override {
        if (HasDataChannels) {
            description.MutableStorageConfig()->SetDataChannelCount(1);
        }
        TableSchema.Serialize(*description.MutableSchema());
    }

    const TOlapSchema& GetSchema() const override {
        return TableSchema;
    };
};


class TConfigureParts: public TSubOperationState {
private:
    TOperationId OperationId;

    TString DebugHint() const override {
        return TStringBuilder()
                << "TCreateColumnTable TConfigureParts"
                << " operationId#" << OperationId;
    }

public:
    TConfigureParts(TOperationId id)
        : OperationId(id)
    {
        IgnoreMessages(DebugHint(), {TEvHive::TEvCreateTabletReply::EventType});
    }

    bool HandleReply(TEvColumnShard::TEvProposeTransactionResult::TPtr& ev, TOperationContext& context) override {
        return NTableState::CollectProposeTransactionResults(OperationId, ev, context);
    }

    bool ProgressState(TOperationContext& context) override {
        TTabletId ssId = context.SS->SelfTabletId();

        LOG_INFO_S(context.Ctx, NKikimrServices::FLAT_TX_SCHEMESHARD,
                   DebugHint() << " ProgressState"
                   << " at tabletId# " << ssId);

        TTxState* txState = context.SS->FindTxSafe(OperationId, TTxState::TxCreateColumnTable);

        TPathId pathId = txState->TargetPathId;
        TPath path = TPath::Init(pathId, context.SS);

        auto pendingInfo = context.SS->ColumnTables.TakeVerified(pathId);
        Y_ABORT_UNLESS(pendingInfo->AlterData);
        TColumnTableInfo::TPtr tableInfo = pendingInfo->AlterData;

        txState->ClearShardsInProgress();

        Y_ABORT_UNLESS(tableInfo->ColumnShards.empty() || tableInfo->OwnedColumnShards.empty());

        TString columnShardTxBody;
        auto seqNo = context.SS->StartRound(*txState);
        NKikimrTxColumnShard::TSchemaTxBody tx;
        context.SS->FillSeqNo(tx, seqNo);

        {
            NKikimrTxColumnShard::TCreateTable* create{};
            if (tableInfo->IsStandalone()) {
                Y_ABORT_UNLESS(tableInfo->ColumnShards.empty());
                Y_ABORT_UNLESS(tableInfo->Description.HasSchema());

                auto* init = tx.MutableInitShard();
                init->SetDataChannelCount(tableInfo->Description.GetStorageConfig().GetDataChannelCount());
                init->SetOwnerPathId(pathId.LocalPathId);
                init->SetOwnerPath(path.PathString());

                create = init->AddTables();
                create->MutableSchema()->CopyFrom(tableInfo->Description.GetSchema());
            } else {
                Y_ABORT_UNLESS(tableInfo->OwnedColumnShards.empty());
                Y_ABORT_UNLESS(!tableInfo->Description.HasSchema());
                Y_ABORT_UNLESS(tableInfo->Description.HasSchemaPresetId());

                create = tx.MutableEnsureTables()->AddTables();

                if (tableInfo->Description.HasSchemaPresetVersionAdj()) {
                    create->SetSchemaPresetVersionAdj(tableInfo->Description.GetSchemaPresetVersionAdj());
                }

                auto olapStorePath = path.FindOlapStore();
                Y_ABORT_UNLESS(olapStorePath, "Unexpected failure to find a tablestore");
                auto storeInfo = context.SS->OlapStores.at(olapStorePath->PathId);

                const ui32 presetId = tableInfo->Description.GetSchemaPresetId();
                Y_ABORT_UNLESS(storeInfo->SchemaPresets.contains(presetId),
                    "Failed to find schema preset %" PRIu32 " in a tablestore", presetId);
                auto& preset = storeInfo->SchemaPresets.at(presetId);
                preset.Serialize(*create->MutableSchemaPreset());
            }

            Y_ABORT_UNLESS(create);
            create->SetPathId(pathId.LocalPathId);

            if (tableInfo->Description.HasTtlSettings()) {
                create->MutableTtlSettings()->CopyFrom(tableInfo->Description.GetTtlSettings());
            }
        }

        Y_ABORT_UNLESS(tx.SerializeToString(&columnShardTxBody));

        for (auto& shard : txState->Shards) {
            TTabletId tabletId = context.SS->ShardInfos[shard.Idx].TabletID;

            if (shard.TabletType == ETabletType::ColumnShard) {
                auto event = std::make_unique<TEvColumnShard::TEvProposeTransaction>(
                    NKikimrTxColumnShard::TX_KIND_SCHEMA,
                    context.SS->TabletID(),
                    context.Ctx.SelfID,
                    ui64(OperationId.GetTxId()),
                    columnShardTxBody,
                    context.SS->SelectProcessingParams(txState->TargetPathId));

                context.OnComplete.BindMsgToPipe(OperationId, tabletId, shard.Idx, event.release());
            } else {
                LOG_ERROR_S(context.Ctx, NKikimrServices::FLAT_TX_SCHEMESHARD, DebugHint() << " unexpected tablet type");
            }

            LOG_DEBUG_S(context.Ctx, NKikimrServices::FLAT_TX_SCHEMESHARD,
                        DebugHint() << " ProgressState"
                                    << " Propose modify scheme on shard"
                                    << " tabletId: " << tabletId);
        }

        txState->UpdateShardsInProgress();

        return false;
    }
};

class TPropose: public TSubOperationState {
private:
    TOperationId OperationId;

    TString DebugHint() const override {
        return TStringBuilder()
                << "TCreateColumnTable TPropose"
                << " operationId#" << OperationId;
    }

public:
    TPropose(TOperationId id)
        : OperationId(id)
    {
        IgnoreMessages(DebugHint(),
            {TEvHive::TEvCreateTabletReply::EventType,
             TEvColumnShard::TEvProposeTransactionResult::EventType});
    }

    bool HandleReply(TEvPrivate::TEvOperationPlan::TPtr& ev, TOperationContext& context) override {
        TStepId step = TStepId(ev->Get()->StepId);
        TTabletId ssId = context.SS->SelfTabletId();

        LOG_INFO_S(context.Ctx, NKikimrServices::FLAT_TX_SCHEMESHARD,
                     DebugHint() << " HandleReply TEvOperationPlan"
                     << " at tablet: " << ssId
                     << ", stepId: " << step);

        TTxState* txState = context.SS->FindTx(OperationId);
        Y_ABORT_UNLESS(txState->TxType == TTxState::TxCreateColumnTable);

        TPathId pathId = txState->TargetPathId;
        TPathElement::TPtr path = context.SS->PathsById.at(pathId);

        NIceDb::TNiceDb db(context.GetDB());

        path->StepCreated = step;
        context.SS->PersistCreateStep(db, pathId, step);

        auto table = context.SS->ColumnTables.TakeAlterVerified(pathId);
        if (table->IsStandalone()) {
            Y_ABORT_UNLESS(table->ColumnShards.empty());
            auto currentLayout = TColumnTablesLayout::BuildTrivial(TColumnTablesLayout::ShardIdxToTabletId(table->OwnedColumnShards, *context.SS));
            auto layoutPolicy = std::make_shared<TOlapStoreInfo::TMinimalTablesCountLayout>();
            bool isNewGroup;
            Y_ABORT_UNLESS(table.InitShardingTablets(currentLayout, table->OwnedColumnShards.size(), layoutPolicy, isNewGroup));
        }

        context.SS->PersistColumnTableAlterRemove(db, pathId);
        context.SS->PersistColumnTable(db, pathId, *table);

        auto parentDir = context.SS->PathsById.at(path->ParentPathId);
        if (parentDir->IsLikeDirectory()) {
            ++parentDir->DirAlterVersion;
            context.SS->PersistPathDirAlterVersion(db, parentDir);
        }
        context.SS->ClearDescribePathCaches(parentDir);
        context.OnComplete.PublishToSchemeBoard(OperationId, parentDir->PathId);

        ++path->DirAlterVersion;
        context.SS->PersistPathDirAlterVersion(db, path);
        context.SS->ClearDescribePathCaches(path);
        context.OnComplete.PublishToSchemeBoard(OperationId, path->PathId);

        context.SS->ChangeTxState(db, OperationId, TTxState::ProposedWaitParts);
        return true;
    }

    bool ProgressState(TOperationContext& context) override {
        TTabletId ssId = context.SS->SelfTabletId();

        LOG_INFO_S(context.Ctx, NKikimrServices::FLAT_TX_SCHEMESHARD,
                     DebugHint() << " HandleReply ProgressState"
                     << " at tablet: " << ssId);

        TTxState* txState = context.SS->FindTx(OperationId);
        Y_ABORT_UNLESS(txState);
        Y_ABORT_UNLESS(txState->TxType == TTxState::TxCreateColumnTable);

        TSet<TTabletId> shardSet;
        for (const auto& shard : txState->Shards) {
            TShardIdx idx = shard.Idx;
            TTabletId tablet = context.SS->ShardInfos.at(idx).TabletID;
            shardSet.insert(tablet);
        }

        context.OnComplete.ProposeToCoordinator(OperationId, txState->TargetPathId, txState->MinStep, shardSet);
        return false;
    }
};

class TProposedWaitParts: public TSubOperationState {
private:
    TOperationId OperationId;

    TString DebugHint() const override {
        return TStringBuilder()
                << "TCreateColumnTable TProposedWaitParts"
                << " operationId#" << OperationId;
    }

public:
    TProposedWaitParts(TOperationId id)
        : OperationId(id)
    {
        IgnoreMessages(DebugHint(),
            {TEvHive::TEvCreateTabletReply::EventType,
             TEvColumnShard::TEvProposeTransactionResult::EventType,
             TEvPrivate::TEvOperationPlan::EventType});
    }

    bool HandleReply(TEvColumnShard::TEvNotifyTxCompletionResult::TPtr& ev, TOperationContext& context) override {
        TTxState* txState = context.SS->FindTx(OperationId);
        Y_ABORT_UNLESS(txState);
        Y_ABORT_UNLESS(txState->TxType == TTxState::TxCreateColumnTable);

        auto shardId = TTabletId(ev->Get()->Record.GetOrigin());
        auto shardIdx = context.SS->MustGetShardIdx(shardId);
        Y_ABORT_UNLESS(context.SS->ShardInfos.contains(shardIdx));

        txState->ShardsInProgress.erase(shardIdx);
        return txState->ShardsInProgress.empty();
    }

    bool HandleReply(TEvHive::TEvUpdateTabletsObjectReply::TPtr&, TOperationContext&) override {
        return false;
    }

    bool ProgressState(TOperationContext& context) override {
        TTabletId ssId = context.SS->SelfTabletId();

        LOG_INFO_S(context.Ctx, NKikimrServices::FLAT_TX_SCHEMESHARD,
                     DebugHint() << " ProgressState"
                     << " at tablet: " << ssId);

        TTxState* txState = context.SS->FindTx(OperationId);
        Y_ABORT_UNLESS(txState);
        Y_ABORT_UNLESS(txState->TxType == TTxState::TxCreateColumnTable);

        txState->ClearShardsInProgress();

        for (auto& shard : txState->Shards) {
            TTabletId tabletId = context.SS->ShardInfos[shard.Idx].TabletID;
            switch (shard.TabletType) {
                case ETabletType::ColumnShard: {
                    auto event = std::make_unique<TEvColumnShard::TEvNotifyTxCompletion>(ui64(OperationId.GetTxId()));

                    context.OnComplete.BindMsgToPipe(OperationId, tabletId, shard.Idx, event.release());
                    txState->ShardsInProgress.insert(shard.Idx);
                    break;
                }
                default: {
                    LOG_DEBUG_S(context.Ctx, NKikimrServices::FLAT_TX_SCHEMESHARD, DebugHint() << " unexpected tablet type");
                }
            }

            LOG_DEBUG_S(context.Ctx, NKikimrServices::FLAT_TX_SCHEMESHARD,
                        DebugHint() << " ProgressState"
                                    << " wait for NotifyTxCompletionResult"
                                    << " tabletId: " << tabletId);
        }

        if (txState->NeedUpdateObject) {
            auto event = std::make_unique<TEvHive::TEvUpdateTabletsObject>();
            for (const auto& shard : txState->Shards) {
                TTabletId tabletId = context.SS->ShardInfos[shard.Idx].TabletID;
                event->Record.AddTabletIds(tabletId.GetValue());
            }
            event->Record.SetObjectId(TSimpleRangeHash{}(event->Record.GetTabletIds()));
            event->Record.SetTxId(ui64(OperationId.GetTxId()));
            event->Record.SetTxPartId(OperationId.GetSubTxId());
            TPathId pathId = txState->TargetPathId;
            auto hiveToRequest = context.SS->ResolveHive(pathId, context.Ctx);
            context.OnComplete.BindMsgToPipe(OperationId, hiveToRequest, pathId, event.release());
        }

        return false;
    }
};

class TCreateColumnTable: public TSubOperation {
    static TTxState::ETxState NextState(bool inStore) {
        if (inStore) {
            return TTxState::ConfigureParts;
        }
        return TTxState::CreateParts;
    }

    TTxState::ETxState NextState(TTxState::ETxState state) const override {
        switch (state) {
        case TTxState::Waiting:
        case TTxState::CreateParts:
            return TTxState::ConfigureParts;
        case TTxState::ConfigureParts:
            return TTxState::Propose;
        case TTxState::Propose:
            return TTxState::ProposedWaitParts;
        case TTxState::ProposedWaitParts:
            return TTxState::Done;
        default:
            return TTxState::Invalid;
        }
    }

    TSubOperationState::TPtr SelectStateFunc(TTxState::ETxState state) override {
        using TPtr = TSubOperationState::TPtr;

        switch (state) {
        case TTxState::Waiting:
        case TTxState::CreateParts:
            return TPtr(new TCreateParts(OperationId));
        case TTxState::ConfigureParts:
            return TPtr(new TConfigureParts(OperationId));
        case TTxState::Propose:
            return TPtr(new TPropose(OperationId));
        case TTxState::ProposedWaitParts:
            return TPtr(new TProposedWaitParts(OperationId));
        case TTxState::Done:
            return TPtr(new TDone(OperationId));
        default:
            return nullptr;
        }
    }

public:
    using TSubOperation::TSubOperation;

    THolder<TProposeResponse> Propose(const TString& owner, TOperationContext& context) override {
        const TTabletId ssId = context.SS->SelfTabletId();

        const auto acceptExisted = !Transaction.GetFailOnExist();
        const TString& parentPathStr = Transaction.GetWorkingDir();

        // Copy CreateColumnTable for changes. Update defaut sharding if not set.
        auto createDescription = Transaction.GetCreateColumnTable();
        if (!createDescription.HasColumnShardCount()) {
            createDescription.SetColumnShardCount(TTableConstructorBase::DEFAULT_SHARDS_COUNT);
        }
        const TString& name = createDescription.GetName();
        const ui32 shardsCount = Max(ui32(1), createDescription.GetColumnShardCount());
        auto opTxId = OperationId.GetTxId();

        LOG_NOTICE_S(context.Ctx, NKikimrServices::FLAT_TX_SCHEMESHARD,
                     "TCreateColumnTable Propose"
                        << ", path: " << parentPathStr << "/" << name
                        << ", opId: " << OperationId
                        << ", at schemeshard: " << ssId);

        TEvSchemeShard::EStatus status = NKikimrScheme::StatusAccepted;
        auto result = MakeHolder<TProposeResponse>(status, ui64(opTxId), ui64(ssId));

        if (context.SS->IsServerlessDomain(TPath::Init(context.SS->RootPathId(), context.SS))) {
            if (AppData()->ColumnShardConfig.GetDisabledOnSchemeShard()) {
                result->SetError(NKikimrScheme::StatusPreconditionFailed,
                    "OLAP schema operations are not supported");
                return result;
            }
        }

        TOlapStoreInfo::TPtr storeInfo;
        NSchemeShard::TPath parentPath = NSchemeShard::TPath::Resolve(parentPathStr, context.SS);
        {
            NSchemeShard::TPath::TChecker checks = parentPath.Check();
            checks
                .NotUnderDomainUpgrade()
                .IsAtLocalSchemeShard()
                .IsResolved()
                .NotDeleted()
                .NotUnderDeleting()
                .IsCommonSensePath()
                .IsLikeDirectory();

            if (!checks) {
                result->SetError(checks.GetStatus(), checks.GetError());
                return result;
            }

            if (auto olapStorePath = parentPath.FindOlapStore()) {
                storeInfo = context.SS->OlapStores.at(olapStorePath->PathId);
                Y_ABORT_UNLESS(storeInfo, "Unexpected failure to find an tablestore info");

                NSchemeShard::TPath::TChecker storeChecks = olapStorePath.Check();
                storeChecks
                    .NotUnderDomainUpgrade()
                    .IsAtLocalSchemeShard()
                    .IsResolved()
                    .NotDeleted()
                    .NotUnderDeleting()
                    .IsOlapStore()
                    .NotUnderOperation();

                if (!storeChecks) {
                    result->SetError(checks.GetStatus(), checks.GetError());
                    return result;
                }
            }
        }

        const TString acl = Transaction.GetModifyACL().GetDiffACL();

        NSchemeShard::TPath dstPath = parentPath.Child(name);
        {
            NSchemeShard::TPath::TChecker checks = dstPath.Check();
            checks.IsAtLocalSchemeShard();
            if (dstPath.IsResolved()) {
                checks
                    .IsResolved()
                    .NotUnderDeleting()
                    .FailOnExist(TPathElement::EPathType::EPathTypeColumnTable, acceptExisted);
            } else {
                checks
                    .NotEmpty()
                    .NotResolved();
            }

            if (checks) {
                checks
                    .IsValidLeafName()
                    .DepthLimit()
                    .PathsLimit()
                    .ShardsLimit(storeInfo ? 0 : shardsCount)
                    .PathShardsLimit(storeInfo ? 0 : shardsCount)
                    .DirChildrenLimit()
                    .IsValidACL(acl);
            }

            if (!checks) {
                result->SetError(checks.GetStatus(), checks.GetError());
                if (dstPath.IsResolved()) {
                    result->SetPathCreateTxId(ui64(dstPath.Base()->CreateTxId));
                    result->SetPathId(dstPath.Base()->PathId.LocalPathId);
                }
                return result;
            }
        }

        TString errStr;

        if (!context.SS->CheckApplyIf(Transaction, errStr)) {
            result->SetError(NKikimrScheme::StatusPreconditionFailed, errStr);
            return result;
        }

        TProposeErrorCollector errors(*result);
        TColumnTableInfo::TPtr tableInfo;
        bool needUpdateObject = false;
        if (storeInfo) {
            TOlapPresetConstructor tableConstructor(*storeInfo);
            if (!tableConstructor.Deserialize(createDescription, errors)) {
                return result;
            }
            tableInfo = tableConstructor.BuildTableInfo(errors);
            if (tableInfo) {
                auto layoutPolicy = storeInfo->GetTablesLayoutPolicy();
                auto currentLayout = context.SS->ColumnTables.GetTablesLayout(
                    TColumnTablesLayout::ShardIdxToTabletId(storeInfo->GetColumnShards(), *context.SS));
                TTablesStorage::TTableCreateOperator createOperator(tableInfo);
                if (!createOperator.InitShardingTablets(currentLayout, shardsCount, layoutPolicy, needUpdateObject)) {
                    result->SetError(NKikimrScheme::StatusPreconditionFailed,
                        "cannot layout table by shards");
                    return result;
                }
            }
        } else {
            TOlapTableConstructor tableConstructor;
            if (!tableConstructor.Deserialize(createDescription, errors)) {
                return result;
            }
            tableInfo = tableConstructor.BuildTableInfo(errors);
        }

        if (!tableInfo) {
            return result;
        }

        dstPath.MaterializeLeaf(owner);
        result->SetPathId(dstPath.Base()->PathId.LocalPathId);

        context.SS->TabletCounters->Simple()[COUNTER_COLUMN_TABLE_COUNT].Add(1);

        TPathId pathId = dstPath.Base()->PathId;
        dstPath.Base()->CreateTxId = opTxId;
        dstPath.Base()->LastTxId = opTxId;
        dstPath.Base()->PathState = TPathElement::EPathState::EPathStateCreate;
        dstPath.Base()->PathType = TPathElement::EPathType::EPathTypeColumnTable;

        TTxState& txState = context.SS->CreateTx(OperationId, TTxState::TxCreateColumnTable, pathId);
        txState.NeedUpdateObject = needUpdateObject;

        if (storeInfo) {
            NIceDb::TNiceDb db(context.GetDB());

            auto olapStorePath = parentPath.FindOlapStore();

            txState.State = TTxState::ConfigureParts;
            txState.Shards.reserve(tableInfo->ColumnShards.size());

            for (ui64 columnShardId : tableInfo->ColumnShards) {
                auto tabletId = TTabletId(columnShardId);
                auto shardIdx = context.SS->TabletIdToShardIdx.at(tabletId);
                TShardInfo& shardInfo = context.SS->ShardInfos.at(shardIdx);
                txState.Shards.emplace_back(shardIdx, ETabletType::ColumnShard, TTxState::ConfigureParts);
                // N.B. we seem to only need CurrentTxId when creating/modifying tablets
                shardInfo.CurrentTxId = opTxId;
                context.SS->PersistShardTx(db, shardIdx, opTxId);
            }

            auto pending = context.SS->ColumnTables.BuildNew(pathId);
            pending->AlterData = tableInfo;
            pending->SetOlapStorePathId(olapStorePath->PathId);
            tableInfo->SetOlapStorePathId(olapStorePath->PathId);
            storeInfo->ColumnTables.insert(pathId);
            storeInfo->ColumnTablesUnderOperation.insert(pathId);
            context.SS->PersistColumnTable(db, pathId, *pending);
            context.SS->PersistColumnTableAlter(db, pathId, *tableInfo);
            context.SS->IncrementPathDbRefCount(pathId);

            if (parentPath.Base()->HasActiveChanges()) {
                TTxId parentTxId = parentPath.Base()->PlannedToCreate()
                    ? parentPath.Base()->CreateTxId : parentPath.Base()->LastTxId;
                context.OnComplete.Dependence(parentTxId, opTxId);
            }

            // Sequentially chain operations in the same store
            if (context.SS->Operations.contains(olapStorePath.Base()->LastTxId)) {
                context.OnComplete.Dependence(olapStorePath.Base()->LastTxId, opTxId);
            }
            olapStorePath.Base()->LastTxId = opTxId;
            context.SS->PersistLastTxId(db, olapStorePath.Base());
        } else {
            NKikimrSchemeOp::TColumnStorageConfig storageConfig; // default
            storageConfig.SetDataChannelCount(1);

            TChannelsBindings channelsBindings;
            if (!context.SS->GetOlapChannelsBindings(dstPath.GetPathIdForDomain(),
                                                     storageConfig, channelsBindings, errStr))
            {
                result->SetError(NKikimrScheme::StatusInvalidParameter, errStr);
                return result;
            }

            txState.State = TTxState::CreateParts;
            txState.Shards.reserve(shardsCount);

            TShardInfo columnShardInfo = TShardInfo::ColumnShardInfo(opTxId, pathId);
            columnShardInfo.BindedChannels = channelsBindings;

            tableInfo->StandaloneSharding = NKikimrSchemeOp::TColumnStoreSharding();
            Y_ABORT_UNLESS(tableInfo->OwnedColumnShards.empty());
            tableInfo->OwnedColumnShards.reserve(shardsCount);

            for (ui64 i = 0; i < shardsCount; ++i) {
                TShardIdx idx = context.SS->RegisterShardInfo(columnShardInfo);
                context.SS->TabletCounters->Simple()[COUNTER_COLUMN_SHARDS].Add(1);
                txState.Shards.emplace_back(idx, ETabletType::ColumnShard, TTxState::CreateParts);

                auto* shardInfoProto = tableInfo->StandaloneSharding->AddColumnShards();
                shardInfoProto->SetOwnerId(idx.GetOwnerId());
                shardInfoProto->SetLocalId(idx.GetLocalId().GetValue());

                tableInfo->OwnedColumnShards.emplace_back(std::move(idx));
            }

            context.SS->SetPartitioning(pathId, tableInfo);

            NIceDb::TNiceDb db(context.GetDB());
            for (auto shard : txState.Shards) {
                context.SS->PersistShardMapping(db, shard.Idx, InvalidTabletId, pathId, opTxId, shard.TabletType);
                context.SS->PersistChannelsBinding(db, shard.Idx, channelsBindings);
            }
            Y_ABORT_UNLESS(txState.Shards.size() == shardsCount);

            auto pending = context.SS->ColumnTables.BuildNew(pathId);
            pending->AlterData = tableInfo;

            context.SS->PersistColumnTable(db, pathId, *pending);
            context.SS->PersistColumnTableAlter(db, pathId, *tableInfo);
            context.SS->IncrementPathDbRefCount(pathId);

            if (parentPath.Base()->HasActiveChanges()) {
                TTxId parentTxId = parentPath.Base()->PlannedToCreate()
                    ? parentPath.Base()->CreateTxId : parentPath.Base()->LastTxId;
                context.OnComplete.Dependence(parentTxId, opTxId);
            }
        }

        NIceDb::TNiceDb db(context.GetDB());
        context.SS->PersistTxState(db, OperationId);
        context.SS->PersistPath(db, dstPath.Base()->PathId);

        context.OnComplete.ActivateTx(OperationId);

        if (!acl.empty()) {
            dstPath.Base()->ApplyACL(acl);
            context.SS->PersistACL(db, dstPath.Base());
        }

        context.SS->PersistUpdateNextPathId(db);
        context.SS->PersistUpdateNextShardIdx(db);

        ++parentPath.Base()->DirAlterVersion;
        context.SS->PersistPathDirAlterVersion(db, parentPath.Base());
        context.SS->ClearDescribePathCaches(parentPath.Base());
        context.OnComplete.PublishToSchemeBoard(OperationId, parentPath.Base()->PathId);

        context.SS->ClearDescribePathCaches(dstPath.Base());
        context.OnComplete.PublishToSchemeBoard(OperationId, dstPath.Base()->PathId);

        dstPath.DomainInfo()->IncPathsInside();
        if (!storeInfo) {
            dstPath.DomainInfo()->AddInternalShards(txState);
            dstPath.Base()->IncShardsInside(tableInfo->OwnedColumnShards.size());
        }
        parentPath.Base()->IncAliveChildren();

        SetState(NextState(!!storeInfo));
        return result;
    }

    void AbortPropose(TOperationContext&) override {
        Y_ABORT("no AbortPropose for TCreateColumnTable");
    }

    void AbortUnsafe(TTxId forceDropTxId, TOperationContext& context) override {
        LOG_NOTICE_S(context.Ctx, NKikimrServices::FLAT_TX_SCHEMESHARD,
                     "TCreateColumnTable AbortUnsafe"
                         << ", opId: " << OperationId
                         << ", forceDropId: " << forceDropTxId
                         << ", at schemeshard: " << context.SS->TabletID());

        context.OnComplete.DoneOperation(OperationId);
    }
};

}

ISubOperation::TPtr CreateNewColumnTable(TOperationId id, const TTxTransaction& tx) {
    return MakeSubOperation<TCreateColumnTable>(id, tx);
}

ISubOperation::TPtr CreateNewColumnTable(TOperationId id, TTxState::ETxState state) {
    Y_ABORT_UNLESS(state != TTxState::Invalid);
    return MakeSubOperation<TCreateColumnTable>(id, state);
}

}
