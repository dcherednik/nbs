#pragma once

#include "blob_cache.h"
#include "blobs_reader/task.h"
#include "engines/reader/conveyor_task.h"
#include "resources/memory.h"
#include <contrib/ydb/core/formats/arrow/size_calcer.h>
#include <contrib/ydb/core/tx/program/program.h>
#include <contrib/libs/apache/arrow/cpp/src/arrow/record_batch.h>

namespace NKikimr::NOlap {
// Represents a batch of rows produced by ASC or DESC scan with applied filters and partial aggregation
class TPartialReadResult {
private:
    YDB_READONLY_DEF(std::vector<std::shared_ptr<NResourceBroker::NSubscribe::TResourcesGuard>>, ResourcesGuards);
    NArrow::TShardedRecordBatch ResultBatch;

    // This 1-row batch contains the last key that was read while producing the ResultBatch.
    // NOTE: it might be different from the Key of last row in ResulBatch in case of filtering/aggregation/limit
    std::shared_ptr<arrow::RecordBatch> LastReadKey;

public:
    void Cut(const ui32 limit) {
        ResultBatch.Cut(limit);
    }

    const arrow::RecordBatch& GetResultBatch() const {
        return *ResultBatch.GetRecordBatch();
    }

    const std::shared_ptr<arrow::RecordBatch>& GetResultBatchPtrVerified() const {
        return ResultBatch.GetRecordBatch();
    }

    const std::shared_ptr<NResourceBroker::NSubscribe::TResourcesGuard>& GetResourcesGuardOnly() const {
        AFL_VERIFY(ResourcesGuards.size() == 1);
        AFL_VERIFY(!!ResourcesGuards.front());
        return ResourcesGuards.front();
    }

    ui64 GetMemorySize() const {
        return ResultBatch.GetMemorySize();
    }

    ui64 GetRecordsCount() const {
        return ResultBatch.GetRecordsCount();
    }

    static std::vector<TPartialReadResult> SplitResults(std::vector<TPartialReadResult>&& resultsExt, const ui32 maxRecordsInResult, const bool mergePartsToMax);

    const NArrow::TShardedRecordBatch& GetShardedBatch() const {
        return ResultBatch;
    }

    const std::shared_ptr<arrow::RecordBatch>& GetLastReadKey() const {
        return LastReadKey;
    }

    std::string ErrorString;

    explicit TPartialReadResult(
        const std::vector<std::shared_ptr<NResourceBroker::NSubscribe::TResourcesGuard>>& resourcesGuards,
        const NArrow::TShardedRecordBatch& batch, std::shared_ptr<arrow::RecordBatch> lastKey)
        : ResourcesGuards(resourcesGuards)
        , ResultBatch(batch)
        , LastReadKey(lastKey) {
        for (auto&& i : ResourcesGuards) {
            AFL_VERIFY(i);
        }
        Y_ABORT_UNLESS(ResultBatch.GetRecordsCount());
        Y_ABORT_UNLESS(LastReadKey);
        Y_ABORT_UNLESS(LastReadKey->num_rows() == 1);
    }

    explicit TPartialReadResult(
        const std::shared_ptr<NResourceBroker::NSubscribe::TResourcesGuard>& resourcesGuards,
        const NArrow::TShardedRecordBatch& batch, std::shared_ptr<arrow::RecordBatch> lastKey)
        : TPartialReadResult(std::vector<std::shared_ptr<NResourceBroker::NSubscribe::TResourcesGuard>>({resourcesGuards}), batch, lastKey) {
        AFL_VERIFY(resourcesGuards);
    }

    explicit TPartialReadResult(const NArrow::TShardedRecordBatch& batch, std::shared_ptr<arrow::RecordBatch> lastKey)
        : TPartialReadResult(std::vector<std::shared_ptr<NResourceBroker::NSubscribe::TResourcesGuard>>(), batch, lastKey) {
    }
};
}

namespace NKikimr::NColumnShard {

class TScanIteratorBase {
public:
    virtual ~TScanIteratorBase() = default;

    virtual void Apply(IDataTasksProcessor::ITask::TPtr /*processor*/) {

    }
    virtual std::optional<ui32> GetAvailableResultsCount() const {
        return {};
    }
    virtual bool Finished() const = 0;
    virtual std::optional<NOlap::TPartialReadResult> GetBatch() = 0;
    virtual void PrepareResults() {

    }
    virtual bool ReadNextInterval() { return false; }
    virtual TString DebugString(const bool verbose = false) const {
        Y_UNUSED(verbose);
        return "NO_DATA";
    }
};

}
