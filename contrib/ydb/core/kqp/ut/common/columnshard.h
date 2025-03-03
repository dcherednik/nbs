#pragma once

#include "kqp_ut_common.h"
#include <contrib/ydb/library/accessor/accessor.h>
#include <contrib/ydb/public/lib/scheme_types/scheme_type_id.h>
#include <contrib/ydb/public/sdk/cpp/client/draft/ydb_long_tx.h>
#include <contrib/ydb/public/sdk/cpp/client/ydb_table/table.h>
#include <contrib/ydb/public/sdk/cpp/client/ydb_types/status_codes.h>
#include <contrib/ydb/core/tx/columnshard/columnshard_ut_common.h>

#include <contrib/ydb/core/formats/arrow/simple_builder/filler.h>
#include <contrib/ydb/core/formats/arrow/simple_builder/array.h>
#include <contrib/ydb/core/formats/arrow/simple_builder/batch.h>

#include <contrib/libs/apache/arrow/cpp/src/arrow/type.h>

namespace NKikimr {
namespace NKqp {
    class TTestHelper {
    public:
        class TColumnSchema {
            YDB_ACCESSOR_DEF(TString, Name);
            YDB_ACCESSOR_DEF(NScheme::TTypeId, Type);
            YDB_FLAG_ACCESSOR(Nullable, true);
        public:
            TString BuildQuery() const;
        };

        using TUpdatesBuilder = NColumnShard::TTableUpdatesBuilder;

        class TColumnTableBase {
            YDB_ACCESSOR_DEF(TString, Name);
            YDB_ACCESSOR_DEF(TVector<TColumnSchema>, Schema);
            YDB_ACCESSOR_DEF(TVector<TString>, PrimaryKey);
            YDB_ACCESSOR_DEF(TVector<TString>, Sharding);
            YDB_ACCESSOR(ui32, MinPartitionsCount, 1);
        public:
            TString BuildQuery() const;
            std::shared_ptr<arrow::Schema> GetArrowSchema(const TVector<TColumnSchema>& columns);

        private:
            virtual TString GetObjectType() const = 0;
            TString BuildColumnsStr(const TVector<TColumnSchema>& clumns) const;
            std::shared_ptr<arrow::Field> BuildField(const TString name, const NScheme::TTypeId& typeId, bool nullable) const;
        };

        class TColumnTable : public TColumnTableBase {
        private:
            TString GetObjectType() const override;
        };

        class TColumnTableStore : public TColumnTableBase {
        private:
            TString GetObjectType() const override;
        };

    private:
        TKikimrRunner Kikimr;
        NYdb::NTable::TTableClient TableClient;
        NYdb::NLongTx::TClient LongTxClient;
        NYdb::NTable::TSession Session;

    public:
        TTestHelper(const TKikimrSettings& settings);
        TKikimrRunner& GetKikimr();
        NYdb::NTable::TSession& GetSession();
        void CreateTable(const TColumnTableBase& table);
        void InsertData(const TColumnTable& table, TTestHelper::TUpdatesBuilder& updates, const std::function<void()> onBeforeCommit = {}, const NYdb::EStatus opStatus = NYdb::EStatus::SUCCESS);
        void BulkUpsert(const TColumnTable& table, TTestHelper::TUpdatesBuilder& updates, const Ydb::StatusIds_StatusCode& opStatus = Ydb::StatusIds::SUCCESS);
        void BulkUpsert(const TColumnTable& table, std::shared_ptr<arrow::RecordBatch> batch, const Ydb::StatusIds_StatusCode& opStatus = Ydb::StatusIds::SUCCESS);
        void ReadData(const TString& query, const TString& expected, const NYdb::EStatus opStatus = NYdb::EStatus::SUCCESS);
        void RebootTablets(const TString& tableName);
    };

}
}
