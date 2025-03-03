#pragma once
#include "chunks.h"
#include "stats.h"
#include "scheme_info.h"
#include "column_info.h"
#include "blob_info.h"
#include <contrib/ydb/core/tx/columnshard/counters/indexation.h>
#include <contrib/ydb/core/tx/columnshard/engines/scheme/column_features.h>
#include <contrib/ydb/core/tx/columnshard/engines/scheme/abstract_scheme.h>
#include <contrib/ydb/core/tx/columnshard/engines/storage/granule.h>

#include <contrib/libs/apache/arrow/cpp/src/arrow/record_batch.h>

namespace NKikimr::NOlap {

template <class TContainer>
class TArrayView {
private:
    typename TContainer::iterator Begin;
    typename TContainer::iterator End;
public:
    TArrayView(typename TContainer::iterator itBegin, typename TContainer::iterator itEnd)
        : Begin(itBegin)
        , End(itEnd) {

    }

    typename TContainer::iterator begin() {
        return Begin;
    }

    typename TContainer::iterator end() {
        return End;
    }

    typename TContainer::value_type& front() {
        return *Begin;
    }

    typename TContainer::value_type& operator[](const size_t index) {
        return *(Begin + index);
    }

    size_t size() {
        return End - Begin;
    }
};

template <class TObject>
using TVectorView = TArrayView<std::vector<TObject>>;

class TDefaultSchemaDetails: public ISchemaDetailInfo {
private:
    ISnapshotSchema::TPtr Schema;
    const TSaverContext Context;
    std::shared_ptr<TSerializationStats> Stats;
public:
    TDefaultSchemaDetails(ISnapshotSchema::TPtr schema, const TSaverContext& context, const std::shared_ptr<TSerializationStats>& stats)
        : Schema(schema)
        , Context(context)
        , Stats(stats)
    {
        AFL_VERIFY(Stats);
    }
    virtual std::shared_ptr<arrow::Field> GetField(const ui32 columnId) const override {
        return Schema->GetFieldByColumnId(columnId);
    }
    virtual bool NeedMinMaxForColumn(const ui32 columnId) const override {
        return Schema->GetIndexInfo().GetMinMaxIdxColumns().contains(columnId);
    }
    virtual bool IsSortedColumn(const ui32 columnId) const override {
        return Schema->GetIndexInfo().IsSortedColumn(columnId);
    }

    virtual std::optional<TColumnSerializationStat> GetColumnSerializationStats(const ui32 columnId) const override {
        return Stats->GetColumnInfo(columnId);
    }
    virtual std::optional<TBatchSerializationStat> GetBatchSerializationStats(const std::shared_ptr<arrow::RecordBatch>& rb) const override {
        return Stats->GetStatsForRecordBatch(rb);
    }
    virtual ui32 GetColumnId(const std::string& fieldName) const override {
        return Schema->GetColumnId(fieldName);
    }
    virtual TColumnSaver GetColumnSaver(const ui32 columnId) const override {
        return Schema->GetColumnSaver(columnId, Context);
    }
};

class TGeneralSerializedSlice {
protected:
    std::vector<TSplittedColumn> Columns;
    ui64 Size = 0;
    ui32 RecordsCount = 0;
    ISchemaDetailInfo::TPtr Schema;
    std::shared_ptr<NColumnShard::TSplitterCounters> Counters;
    TSplitSettings Settings;
    TGeneralSerializedSlice() = default;

    const TSplittedColumn& GetColumnVerified(const std::string& fieldName) const {
        for (auto&& i : Columns) {
            if (i.GetField()->name() == fieldName) {
                return i;
            }
        }
        Y_ABORT_UNLESS(false);
        return Columns.front();
    }
public:
    std::shared_ptr<arrow::RecordBatch> GetFirstLastPKBatch(const std::shared_ptr<arrow::Schema>& pkSchema) const {
        std::vector<std::shared_ptr<arrow::Array>> pkColumns;
        for (auto&& i : pkSchema->fields()) {
            auto aBuilder = NArrow::MakeBuilder(i);
            const TSplittedColumn& splittedColumn = GetColumnVerified(i->name());
            NArrow::TStatusValidator::Validate(aBuilder->AppendScalar(*splittedColumn.GetFirstScalar()));
            NArrow::TStatusValidator::Validate(aBuilder->AppendScalar(*splittedColumn.GetLastScalar()));
            pkColumns.emplace_back(NArrow::TStatusValidator::GetValid(aBuilder->Finish()));
        }
        return arrow::RecordBatch::Make(pkSchema, 2, pkColumns);
    }

    ui64 GetSize() const {
        return Size;
    }
    ui32 GetRecordsCount() const {
        return RecordsCount;
    }

    std::vector<std::vector<IPortionColumnChunk::TPtr>> GroupChunksByBlobs() {
        std::vector<std::vector<IPortionColumnChunk::TPtr>> result;
        std::vector<TSplittedBlob> blobs;
        GroupBlobs(blobs);
        for (auto&& i : blobs) {
            result.emplace_back(i.GetChunks());
        }
        return result;
    }

    explicit TGeneralSerializedSlice(TVectorView<TGeneralSerializedSlice>&& objects) {
        Y_ABORT_UNLESS(objects.size());
        std::swap(*this, objects.front());
        for (ui32 i = 1; i < objects.size(); ++i) {
            MergeSlice(std::move(objects[i]));
        }
    }
    TGeneralSerializedSlice(const std::map<ui32, std::vector<IPortionColumnChunk::TPtr>>& data, ISchemaDetailInfo::TPtr schema, std::shared_ptr<NColumnShard::TSplitterCounters> counters, const TSplitSettings& settings);
    TGeneralSerializedSlice(ISchemaDetailInfo::TPtr schema, std::shared_ptr<NColumnShard::TSplitterCounters> counters, const TSplitSettings& settings);

    void MergeSlice(TGeneralSerializedSlice&& slice);

    bool GroupBlobs(std::vector<TSplittedBlob>& blobs);

    bool operator<(const TGeneralSerializedSlice& item) const {
        return Size < item.Size;
    }
};

class TBatchSerializedSlice: public TGeneralSerializedSlice {
private:
    using TBase = TGeneralSerializedSlice;
    YDB_READONLY_DEF(std::shared_ptr<arrow::RecordBatch>, Batch);
public:
    TBatchSerializedSlice(std::shared_ptr<arrow::RecordBatch> batch, ISchemaDetailInfo::TPtr schema, std::shared_ptr<NColumnShard::TSplitterCounters> counters, const TSplitSettings& settings);

    explicit TBatchSerializedSlice(TVectorView<TBatchSerializedSlice>&& objects)
    {
        Y_ABORT_UNLESS(objects.size());
        std::swap(*this, objects.front());
        for (ui32 i = 1; i < objects.size(); ++i) {
            MergeSlice(std::move(objects[i]));
        }
    }
    void MergeSlice(TBatchSerializedSlice&& slice) {
        Batch = NArrow::CombineBatches({Batch, slice.Batch});
        TBase::MergeSlice(std::move(slice));
    }
};

}
