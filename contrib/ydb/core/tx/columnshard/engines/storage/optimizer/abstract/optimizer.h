#pragma once
#include <contrib/ydb/core/tx/columnshard/engines/portions/portion_info.h>
#include <contrib/ydb/core/formats/arrow/reader/read_filter_merger.h>
#include <library/cpp/object_factory/object_factory.h>
#include <contrib/ydb/core/base/appdata.h>

namespace NKikimr::NOlap {
struct TCompactionLimits;
class TGranuleMeta;
class TColumnEngineChanges;
}

namespace NKikimr::NOlap::NStorageOptimizer {

class TOptimizationPriority {
private:
    YDB_READONLY(i64, Level, 0);
    YDB_READONLY(i64, InternalLevelWeight, 0);
    TOptimizationPriority(const i64 level, const i64 levelWeight)
        : Level(level)
        , InternalLevelWeight(levelWeight) {

    }

public:
    bool operator<(const TOptimizationPriority& item) const {
        return std::tie(Level, InternalLevelWeight) < std::tie(item.Level, item.InternalLevelWeight);
    }

    bool IsZero() const {
        return !Level && !InternalLevelWeight;
    }

    TString DebugString() const {
        return TStringBuilder() << "(" << Level << "," << InternalLevelWeight << ")";
    }

    static TOptimizationPriority Critical(const i64 weight) {
        return TOptimizationPriority(10, weight);
    }

    static TOptimizationPriority Optimization(const i64 weight) {
        return TOptimizationPriority(0, weight);
    }

    static TOptimizationPriority Zero() {
        return TOptimizationPriority(0, 0);
    }

};

class IOptimizerPlanner {
private:
    const ui64 PathId;
    YDB_READONLY(TInstant, ActualizationInstant, TInstant::Zero());
protected:
    virtual void DoModifyPortions(const std::vector<std::shared_ptr<TPortionInfo>>& add, const std::vector<std::shared_ptr<TPortionInfo>>& remove) = 0;
    virtual std::shared_ptr<TColumnEngineChanges> DoGetOptimizationTask(const TCompactionLimits& limits, std::shared_ptr<TGranuleMeta> granule, const THashSet<TPortionAddress>& busyPortions) const = 0;
    virtual TOptimizationPriority DoGetUsefulMetric() const = 0;
    virtual void DoActualize(const TInstant currentInstant) = 0;
    virtual TString DoDebugString() const {
        return "";
    }
    virtual NJson::TJsonValue DoSerializeToJsonVisual() const {
        return NJson::JSON_NULL;
    }
    
public:
    using TFactory = NObjectFactory::TObjectFactory<IOptimizerPlanner, TString>;
    IOptimizerPlanner(const ui64 pathId)
        : PathId(pathId)
    {

    }

    class TModificationGuard: TNonCopyable {
    private:
        IOptimizerPlanner& Owner;
        std::vector<std::shared_ptr<TPortionInfo>> AddPortions;
        std::vector<std::shared_ptr<TPortionInfo>> RemovePortions;
    public:
        TModificationGuard& AddPortion(const std::shared_ptr<TPortionInfo>& portion) {
            if (AppData()->ColumnShardConfig.GetSkipOldGranules() && portion->GetDeprecatedGranuleId() > 0) {
                AFL_WARN(NKikimrServices::TX_COLUMNSHARD)("event", "skip_granule")("granule_id", portion->GetDeprecatedGranuleId());
                return *this;
            }
            AddPortions.emplace_back(portion);
            return*this;
        }

        TModificationGuard& RemovePortion(const std::shared_ptr<TPortionInfo>& portion) {
            if (AppData()->ColumnShardConfig.GetSkipOldGranules() && portion->GetDeprecatedGranuleId() > 0) {
                AFL_WARN(NKikimrServices::TX_COLUMNSHARD)("event", "skip_granule")("granule_id", portion->GetDeprecatedGranuleId());
                return *this;
            }
            RemovePortions.emplace_back(portion);
            return*this;
        }

        TModificationGuard(IOptimizerPlanner& owner)
            : Owner(owner)
        {
        }
        ~TModificationGuard() {
            Owner.ModifyPortions(AddPortions, RemovePortions);
        }
    };

    TModificationGuard StartModificationGuard() {
        return TModificationGuard(*this);
    }

    virtual ~IOptimizerPlanner() = default;
    TString DebugString() const {
        return DoDebugString();
    }

    virtual std::vector<NIndexedReader::TSortableBatchPosition> GetBucketPositions() const = 0;

    NJson::TJsonValue SerializeToJsonVisual() const {
        return DoSerializeToJsonVisual();
    }

    void ModifyPortions(const std::vector<std::shared_ptr<TPortionInfo>>& add, const std::vector<std::shared_ptr<TPortionInfo>>& remove) {
        NActors::TLogContextGuard g(NActors::TLogContextBuilder::Build(NKikimrServices::TX_COLUMNSHARD)("path_id", PathId));
        DoModifyPortions(add, remove);
    }

    std::shared_ptr<TColumnEngineChanges> GetOptimizationTask(const TCompactionLimits& limits, std::shared_ptr<TGranuleMeta> granule, const THashSet<TPortionAddress>& busyPortions) const;
    TOptimizationPriority GetUsefulMetric() const {
        return DoGetUsefulMetric();
    }
    void Actualize(const TInstant currentInstant) {
        ActualizationInstant = currentInstant;
        return DoActualize(currentInstant);
    }
};

} // namespace NKikimr::NOlap
