#include "columnshard.h"
#include <contrib/ydb/core/formats/arrow/serializer/full.h>
#include <contrib/ydb/core/testlib/cs_helper.h>

namespace NKikimr {
namespace NKqp {
    using namespace NYdb;

    TTestHelper::TTestHelper(const TKikimrSettings& settings)
        : Kikimr(settings)
        , TableClient(Kikimr.GetTableClient())
        , LongTxClient(Kikimr.GetDriver())
        , Session(TableClient.CreateSession().GetValueSync().GetSession())
    {}

    NKikimr::NKqp::TKikimrRunner& TTestHelper::GetKikimr() {
        return Kikimr;
    }

    NYdb::NTable::TSession& TTestHelper::GetSession() {
        return Session;
    }

    void TTestHelper::CreateTable(const TColumnTableBase& table) {
        std::cerr << (table.BuildQuery()) << std::endl;
        auto result = Session.ExecuteSchemeQuery(table.BuildQuery()).GetValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(result.GetStatus(), EStatus::SUCCESS, result.GetIssues().ToString());
    }

    void TTestHelper::InsertData(const TColumnTable& table, TTestHelper::TUpdatesBuilder& updates, const std::function<void()> onBeforeCommit /*= {}*/, const EStatus opStatus /*= EStatus::SUCCESS*/) {
        NLongTx::TLongTxBeginResult resBeginTx = LongTxClient.BeginWriteTx().GetValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(resBeginTx.Status().GetStatus(), EStatus::SUCCESS, resBeginTx.Status().GetIssues().ToString());

        auto txId = resBeginTx.GetResult().tx_id();
        auto batch = updates.BuildArrow();
        TString data = NArrow::NSerialization::TFullDataSerializer(arrow::ipc::IpcWriteOptions::Defaults()).Serialize(batch);

        NLongTx::TLongTxWriteResult resWrite =
            LongTxClient.Write(txId, table.GetName(), txId, data, Ydb::LongTx::Data::APACHE_ARROW).GetValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(resWrite.Status().GetStatus(), opStatus, resWrite.Status().GetIssues().ToString());

        if (onBeforeCommit) {
            onBeforeCommit();
        }

        NLongTx::TLongTxCommitResult resCommitTx = LongTxClient.CommitTx(txId).GetValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(resCommitTx.Status().GetStatus(), EStatus::SUCCESS, resCommitTx.Status().GetIssues().ToString());
    }

    void TTestHelper::BulkUpsert(const TColumnTable& table, TTestHelper::TUpdatesBuilder& updates, const Ydb::StatusIds_StatusCode& opStatus /*= Ydb::StatusIds::SUCCESS*/) {
        Y_UNUSED(opStatus);
        NKikimr::Tests::NCS::THelper helper(Kikimr.GetTestServer());
        auto batch = updates.BuildArrow();
        helper.SendDataViaActorSystem(table.GetName(), batch, opStatus);
    }

    void TTestHelper::BulkUpsert(const TColumnTable& table, std::shared_ptr<arrow::RecordBatch> batch, const Ydb::StatusIds_StatusCode& opStatus /*= Ydb::StatusIds::SUCCESS*/) {
        Y_UNUSED(opStatus);
        NKikimr::Tests::NCS::THelper helper(Kikimr.GetTestServer());
        helper.SendDataViaActorSystem(table.GetName(), batch, opStatus);
    }

    void TTestHelper::ReadData(const TString& query, const TString& expected, const EStatus opStatus /*= EStatus::SUCCESS*/) {
        auto it = TableClient.StreamExecuteScanQuery(query).GetValueSync();
        UNIT_ASSERT_VALUES_EQUAL_C(it.GetStatus(), EStatus::SUCCESS, it.GetIssues().ToString()); // Means stream successfully get
        TString result = StreamResultToYson(it, false, opStatus);
        if (opStatus == EStatus::SUCCESS) {
            UNIT_ASSERT_NO_DIFF(ReformatYson(result), ReformatYson(expected));
        }
    }

    void TTestHelper::RebootTablets(const TString& tableName) {
        auto runtime = Kikimr.GetTestServer().GetRuntime();
        TActorId sender = runtime->AllocateEdgeActor();
        TVector<ui64> shards;
        {
            auto describeResult = DescribeTable(&Kikimr.GetTestServer(), sender, tableName);
            for (auto shard : describeResult.GetPathDescription().GetColumnTableDescription().GetSharding().GetColumnShards()) {
                shards.push_back(shard);
            }
        }
        for (auto shard : shards) {
            RebootTablet(*runtime, shard, sender);
        }
    }


    TString TTestHelper::TColumnSchema::BuildQuery() const {
        auto str = TStringBuilder() << Name << " " << NScheme::GetTypeName(Type);
        if (!NullableFlag) {
            str << " NOT NULL";
        }
        return str;
    }


    TString TTestHelper::TColumnTableBase::BuildQuery() const {
        auto str = TStringBuilder() << "CREATE " << GetObjectType() << " `" << Name << "`";
        str << " (" << BuildColumnsStr(Schema) << ", PRIMARY KEY (" << JoinStrings(PrimaryKey, ", ") << "))";
        if (!Sharding.empty()) {
            str << " PARTITION BY HASH(" << JoinStrings(Sharding, ", ") << ")";
        }
        str << " WITH (STORE = COLUMN, AUTO_PARTITIONING_MIN_PARTITIONS_COUNT =" << MinPartitionsCount << ");";
        return str;
    }


    std::shared_ptr<arrow::Schema> TTestHelper::TColumnTableBase::GetArrowSchema(const TVector<TColumnSchema>& columns) {
        std::vector<std::shared_ptr<arrow::Field>> result;
        for (auto&& col : columns) {
            result.push_back(BuildField(col.GetName(), col.GetType(), col.IsNullable()));
        }
        return std::make_shared<arrow::Schema>(result);
    }


    TString TTestHelper::TColumnTableBase::BuildColumnsStr(const TVector<TColumnSchema>& clumns) const {
        TVector<TString> columnStr;
        for (auto&& c : clumns) {
            columnStr.push_back(c.BuildQuery());
        }
        return JoinStrings(columnStr, ", ");
    }


    std::shared_ptr<arrow::Field> TTestHelper::TColumnTableBase::BuildField(const TString name, const NScheme::TTypeId& typeId, bool nullable) const {
        switch (typeId) {
        case NScheme::NTypeIds::Bool:
            return arrow::field(name, arrow::boolean(), nullable);
        case NScheme::NTypeIds::Int8:
            return arrow::field(name, arrow::int8(), nullable);
        case NScheme::NTypeIds::Int16:
            return arrow::field(name, arrow::int16(), nullable);
        case NScheme::NTypeIds::Int32:
            return arrow::field(name, arrow::int32(), nullable);
        case NScheme::NTypeIds::Int64:
            return arrow::field(name, arrow::int64(), nullable);
        case NScheme::NTypeIds::Uint8:
            return arrow::field(name, arrow::uint8(), nullable);
        case NScheme::NTypeIds::Uint16:
            return arrow::field(name, arrow::uint16(), nullable);
        case NScheme::NTypeIds::Uint32:
            return arrow::field(name, arrow::uint32(), nullable);
        case NScheme::NTypeIds::Uint64:
            return arrow::field(name, arrow::uint64(), nullable);
        case NScheme::NTypeIds::Float:
            return arrow::field(name, arrow::float32(), nullable);
        case NScheme::NTypeIds::Double:
            return arrow::field(name, arrow::float64(), nullable);
        case NScheme::NTypeIds::String:
            return arrow::field(name, arrow::binary(), nullable);
        case NScheme::NTypeIds::Utf8:
            return arrow::field(name, arrow::utf8(), nullable);
        case NScheme::NTypeIds::Json:
            return arrow::field(name, arrow::utf8(), nullable);
        case NScheme::NTypeIds::Yson:
            return arrow::field(name, arrow::binary(), nullable);
        case NScheme::NTypeIds::Date:
            return arrow::field(name, arrow::uint16(), nullable);
        case NScheme::NTypeIds::Datetime:
            return arrow::field(name, arrow::uint32(), nullable);
        case NScheme::NTypeIds::Timestamp:
            return arrow::field(name, arrow::timestamp(arrow::TimeUnit::TimeUnit::MICRO), nullable);
        case NScheme::NTypeIds::Interval:
            return arrow::field(name, arrow::duration(arrow::TimeUnit::TimeUnit::MICRO), nullable);
        case NScheme::NTypeIds::JsonDocument:
            return arrow::field(name, arrow::binary(), nullable);
        }
        return nullptr;
    }


    TString TTestHelper::TColumnTable::GetObjectType() const {
        return "TABLE";
    }


    TString TTestHelper::TColumnTableStore::GetObjectType() const {
        return "TABLESTORE";
    }

}
}
