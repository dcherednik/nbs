#include "column_portion_chunk.h"
#include <contrib/ydb/core/formats/arrow/common/validation.h>
#include <contrib/ydb/core/tx/columnshard/splitter/simple.h>
#include <contrib/ydb/core/tx/columnshard/engines/changes/counters/general.h>

namespace NKikimr::NOlap::NCompaction {

std::vector<NKikimr::NOlap::IPortionColumnChunk::TPtr> TChunkPreparation::DoInternalSplit(const TColumnSaver& saver, std::shared_ptr<NColumnShard::TSplitterCounters> counters, const std::vector<ui64>& splitSizes) const {
    auto loader = SchemaInfo->GetColumnLoader(SchemaInfo->GetFieldByColumnIdVerified(Record.ColumnId)->name());
    auto rb = NArrow::TStatusValidator::GetValid(loader->Apply(Data));

    auto chunks = TSimpleSplitter(saver, counters).SplitBySizes(rb, Data, splitSizes);
    std::vector<IPortionColumnChunk::TPtr> newChunks;
    for (auto&& i : chunks) {
        Y_ABORT_UNLESS(i.GetSlicedBatch()->num_columns() == 1);
        newChunks.emplace_back(std::make_shared<TChunkPreparation>(
            saver.Apply(i.GetSlicedBatch()), i.GetSlicedBatch()->column(0), ColumnId, SchemaInfo));
    }
    return newChunks;
}

std::shared_ptr<arrow::Array> TColumnPortion::AppendBlob(const TString& data, const TColumnRecord& columnChunk, ui32& remained) {
//    if (CurrentPortionRecords + columnChunk.GetMeta().GetNumRowsVerified() <= Context.GetPortionRowsCountLimit() &&
//        columnChunk.GetMeta().GetRawBytesVerified() < Context.GetChunkRawBytesLimit() &&
//        data.size() < Context.GetChunkPackedBytesLimit() &&
//        columnChunk.GetMeta().GetRawBytesVerified() > Context.GetStorePackedChunkSizeLimit() && Context.GetSaver().IsHardPacker() &&
//        Context.GetUseWholeChunksOptimization())
//    {
//        NChanges::TGeneralCompactionCounters::OnFullBlobAppend(columnChunk.BlobRange.GetBlobSize());
//        FlushBuffer();
//        Chunks.emplace_back(std::make_shared<TChunkPreparation>(data, columnChunk, Context.GetSchemaInfo()));
//        PackedSize += Chunks.back()->GetPackedSize();
//        CurrentPortionRecords += columnChunk.GetMeta().GetNumRowsVerified();
//        return nullptr;
//    } else {
        NChanges::TGeneralCompactionCounters::OnSplittedBlobAppend(columnChunk.BlobRange.GetBlobSize());
        auto batch = NArrow::TStatusValidator::GetValid(Context.GetLoader()->Apply(data));
        AFL_VERIFY(batch->num_columns() == 1);
        auto batchArray = batch->column(0);
        remained = AppendSlice(batchArray, 0, batch->num_rows());
        if (remained) {
            return batchArray;
        } else {
            return nullptr;
        }
//    }
}

ui32 TColumnPortion::AppendSlice(const std::shared_ptr<arrow::Array>& a, const ui32 startIndex, const ui32 length) {
    Y_ABORT_UNLESS(a);
    Y_ABORT_UNLESS(length);
    Y_ABORT_UNLESS(CurrentPortionRecords < Context.GetPortionRowsCountLimit());
    Y_ABORT_UNLESS(startIndex + length <= a->length());
    ui32 i = startIndex;
    for (; i < startIndex + length; ++i) {
        ui64 recordSize = 0;
        NArrow::Append(*Builder, *a, i, &recordSize);
        CurrentChunkRawSize += recordSize;
        PredictedPackedBytes += Context.GetColumnStat().GetPackedRecordSize();
        if (++CurrentPortionRecords == Context.GetPortionRowsCountLimit()) {
            FlushBuffer();
            ++i;
            break;
        }
        if (CurrentChunkRawSize >= Context.GetChunkRawBytesLimit() || PredictedPackedBytes >= Context.GetExpectedBlobPackedBytes()) {
            FlushBuffer();
        }
    }
    return startIndex + length - i;
}

void TColumnPortion::FlushBuffer() {
    if (Builder->length()) {
        auto newArrayChunk = NArrow::TStatusValidator::GetValid(Builder->Finish());
        Chunks.emplace_back(std::make_shared<TChunkPreparation>(Context.GetSaver().Apply(newArrayChunk, Context.GetField()), newArrayChunk, Context.GetColumnId(), Context.GetSchemaInfo()));
        Builder = Context.MakeBuilder();
        CurrentChunkRawSize = 0;
        PredictedPackedBytes = 0;
        PackedSize += Chunks.back()->GetPackedSize();
    }
}

}
