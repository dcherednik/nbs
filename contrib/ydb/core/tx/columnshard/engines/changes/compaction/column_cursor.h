#pragma once
#include "merged_column.h"
#include <contrib/ydb/core/tx/columnshard/splitter/chunks.h>
#include <contrib/ydb/core/tx/columnshard/engines/portions/column_record.h>
#include <contrib/ydb/core/tx/columnshard/engines/scheme/column_features.h>
#include <contrib/ydb/core/tx/columnshard/engines/portions/with_blobs.h>
#include <contrib/libs/apache/arrow/cpp/src/arrow/record_batch.h>

namespace NKikimr::NOlap::NCompaction {

class TPortionColumnCursor {
private:
    std::vector<IPortionColumnChunk::TPtr> BlobChunks;
    std::vector<const TColumnRecord*> ColumnChunks;
    std::optional<ui32> RecordIndexStart;
    YDB_READONLY(ui32, RecordIndexFinish, 0);
    ui32 ChunkRecordIndexStartPosition = 0;
    ui32 ChunkIdx = 0;
    IPortionColumnChunk::TPtr CurrentBlobChunk;
    const TColumnRecord* CurrentColumnChunk = nullptr;
    std::shared_ptr<arrow::Array> CurrentArray;
    std::shared_ptr<TColumnLoader> ColumnLoader;

    const std::shared_ptr<arrow::Array>& GetCurrentArray();

    bool NextChunk();

public:
    ~TPortionColumnCursor() {
        AFL_VERIFY(!RecordIndexStart || ChunkIdx == ColumnChunks.size())("chunk", ChunkIdx)
            ("size", ColumnChunks.size())("start", RecordIndexStart.value_or(9999999))("finish", RecordIndexFinish)
            ("max", CurrentColumnChunk ? CurrentColumnChunk->GetMeta().GetNumRowsVerified() : -1)("current_start_position", ChunkRecordIndexStartPosition);
    }

    bool Next(const ui32 portionRecordIdx, TMergedColumn& column);

    bool Fetch(TMergedColumn& column);

    TPortionColumnCursor(const std::vector<IPortionColumnChunk::TPtr>& columnChunks, const std::vector<const TColumnRecord*>& records, const std::shared_ptr<TColumnLoader> loader)
        : BlobChunks(columnChunks)
        , ColumnChunks(records)
        , ColumnLoader(loader)
    {
        Y_ABORT_UNLESS(BlobChunks.size());
        Y_ABORT_UNLESS(ColumnChunks.size() == BlobChunks.size());
        CurrentBlobChunk = BlobChunks.front();
        CurrentColumnChunk = ColumnChunks.front();
    }
};

}
