#include "columnshard_ut_common.h"
#include <contrib/ydb/core/kqp/compute_actor/kqp_compute_events.h>
#include <contrib/ydb/core/base/blobstorage.h>
#include <util/string/printf.h>
#include <arrow/api.h>
#include <arrow/ipc/reader.h>
#include <contrib/ydb/library/yverify_stream/yverify_stream.h>
#include <contrib/ydb/core/tx/columnshard/engines/changes/with_appended.h>
#include <contrib/ydb/core/tx/columnshard/engines/changes/compaction.h>
#include <contrib/ydb/core/tx/columnshard/engines/changes/cleanup.h>
#include <contrib/ydb/core/tx/columnshard/operations/write_data.h>
#include <contrib/ydb/core/tx/columnshard/hooks/abstract/abstract.h>
#include <contrib/ydb/core/tx/columnshard/hooks/testing/controller.h>
#include <library/cpp/actors/protos/unittests.pb.h>
#include <contrib/ydb/core/formats/arrow/simple_builder/filler.h>
#include <contrib/ydb/core/formats/arrow/simple_builder/array.h>
#include <contrib/ydb/core/formats/arrow/simple_builder/batch.h>
#include <util/string/join.h>

namespace NKikimr {

using namespace NColumnShard;
using namespace Tests;
using namespace NTxUT;

namespace
{

namespace NTypeIds = NScheme::NTypeIds;
using TTypeId = NScheme::TTypeId;
using TTypeInfo = NScheme::TTypeInfo;

using TDefaultTestsController = NKikimr::NYDBTest::NColumnShard::TController;
class TDisableCompactionController: public NKikimr::NYDBTest::NColumnShard::TController {
protected:
    virtual bool DoOnStartCompaction(std::shared_ptr<NOlap::TColumnEngineChanges>& changes) {
        changes = nullptr;
        return true;
    }
public:
};

template <typename TKey = ui64>
bool DataHas(const std::vector<TString>& blobs, const TString& srtSchema, std::pair<ui64, ui64> range,
             bool requireUniq = false, const std::string& columnName = "timestamp") {
    static constexpr const bool isStrKey = std::is_same_v<TKey, std::string>;

    THashMap<TKey, ui32> keys;
    for (size_t i = range.first; i < range.second; ++i) {
        if constexpr (isStrKey) {
            keys.emplace(ToString(i), 0);
        } else {
            keys.emplace(i, 0);
        }
    }

    auto schema = NArrow::DeserializeSchema(srtSchema);
    //Cerr << "Got schema: " << schema->ToString() << "\n";

    for (auto& blob : blobs) {
        auto batch = NArrow::DeserializeBatch(blob, schema);
        UNIT_ASSERT(batch);
        //Cerr << "Got batch: " << batch->ToString() << "\n";

        std::shared_ptr<arrow::Array> array = batch->GetColumnByName(columnName);
        UNIT_ASSERT(array);

        for (int i = 0; i < array->length(); ++i) {
            TKey value{};

            NArrow::SwitchType(array->type_id(), [&](const auto& type) {
                using TWrap = std::decay_t<decltype(type)>;
                using TArray = typename arrow::TypeTraits<typename TWrap::T>::ArrayType;

                if constexpr (isStrKey && arrow::has_string_view<typename TWrap::T>()) {
                    value = static_cast<const TArray&>(*array).GetString(i);
                    return true;
                }
                if constexpr (!isStrKey && arrow::has_c_type<typename TWrap::T>()) {
                    auto& column = static_cast<const TArray&>(*array);
                    value = column.Value(i);
                    return true;
                }
                UNIT_ASSERT(false);
                return false;
            });

            ++keys[value];
        }
    }

    for (auto& [key, count] : keys) {
        if (!count) {
            Cerr << "No key: " << key << "\n";
            return false;
        }
        if (requireUniq && count > 1) {
            Cerr << "Not unique key: " << key << " (count: " << count << ")\n";
            return false;
        }
    }

    return true;
}

template <typename TKey = ui64>
bool DataHasOnly(const std::vector<TString>& blobs, const TString& srtSchema, std::pair<ui64, ui64> range) {
    static constexpr const bool isStrKey = std::is_same_v<TKey, std::string>;

    THashSet<TKey> keys;
    for (size_t i = range.first; i < range.second; ++i) {
        if constexpr (isStrKey) {
            keys.emplace(ToString(i));
        } else {
            keys.emplace(i);
        }
    }

    auto schema = NArrow::DeserializeSchema(srtSchema);
    for (auto& blob : blobs) {
        auto batch = NArrow::DeserializeBatch(blob, schema);
        UNIT_ASSERT(batch);

        std::shared_ptr<arrow::Array> array = batch->GetColumnByName("timestamp");
        UNIT_ASSERT(array);

        for (int i = 0; i < array->length(); ++i) {
            ui64 value{};

            NArrow::SwitchType(array->type_id(), [&](const auto& type) {
                using TWrap = std::decay_t<decltype(type)>;
                using TArray = typename arrow::TypeTraits<typename TWrap::T>::ArrayType;

                if constexpr (isStrKey && arrow::has_string_view<typename TWrap::T>()) {
                    value = static_cast<const TArray&>(*array).GetView(i);
                    return true;
                }
                if constexpr (!isStrKey && arrow::has_c_type<typename TWrap::T>()) {
                    auto& column = static_cast<const TArray&>(*array);
                    value = column.Value(i);
                    return true;
                }
                UNIT_ASSERT(false);
                return false;
            });

            if (!keys.contains(value)) {
                Cerr << "Unexpected key: " << value << "\n";
                return false;
            }
        }
    }

    return true;
}

template <typename TArrowType>
bool CheckTypedIntValues(const std::shared_ptr<arrow::Array>& array, const std::vector<int64_t>& expected) {
    using TArray = typename arrow::TypeTraits<TArrowType>::ArrayType;

    UNIT_ASSERT(array);
    UNIT_ASSERT_VALUES_EQUAL(array->length(), (int)expected.size());

    auto& column = dynamic_cast<const TArray&>(*array);

    for (int i = 0; i < column.length(); ++i) {
        auto value = column.Value(i);
        UNIT_ASSERT_VALUES_EQUAL(value, expected[i]);
    }
    return true;
}

template <typename TArrowType>
bool CheckTypedStrValues(const std::shared_ptr<arrow::Array>& array, const std::vector<std::string>& expected) {
    using TArray = typename arrow::TypeTraits<TArrowType>::ArrayType;

    UNIT_ASSERT(array);
    UNIT_ASSERT_VALUES_EQUAL(array->length(), (int)expected.size());

    auto& column = dynamic_cast<const TArray&>(*array);

    for (int i = 0; i < column.length(); ++i) {
        auto value = column.GetString(i);
        UNIT_ASSERT_VALUES_EQUAL(value, expected[i]);
    }
    return true;
}

bool CheckIntValues(const std::shared_ptr<arrow::Array>& array, const std::vector<int64_t>& expected) {
    UNIT_ASSERT(array);

    std::vector<std::string> expectedStr;
    expectedStr.reserve(expected.size());
    for (auto& val : expected) {
        expectedStr.push_back(ToString(val));
    }

    switch (array->type()->id()) {
        case arrow::Type::UINT8:
            return CheckTypedIntValues<arrow::UInt8Type>(array, expected);
        case arrow::Type::UINT16:
            return CheckTypedIntValues<arrow::UInt16Type>(array, expected);
        case arrow::Type::UINT32:
            return CheckTypedIntValues<arrow::UInt32Type>(array, expected);
        case arrow::Type::UINT64:
            return CheckTypedIntValues<arrow::UInt64Type>(array, expected);
        case arrow::Type::INT8:
            return CheckTypedIntValues<arrow::Int8Type>(array, expected);
        case arrow::Type::INT16:
            return CheckTypedIntValues<arrow::Int16Type>(array, expected);
        case arrow::Type::INT32:
            return CheckTypedIntValues<arrow::Int32Type>(array, expected);
        case arrow::Type::INT64:
            return CheckTypedIntValues<arrow::Int64Type>(array, expected);

        case arrow::Type::TIMESTAMP:
            return CheckTypedIntValues<arrow::TimestampType>(array, expected);
        case arrow::Type::DURATION:
            return CheckTypedIntValues<arrow::DurationType>(array, expected);

        case arrow::Type::FLOAT:
            return CheckTypedIntValues<arrow::FloatType>(array, expected);
        case arrow::Type::DOUBLE:
            return CheckTypedIntValues<arrow::DoubleType>(array, expected);

        case arrow::Type::STRING:
            return CheckTypedStrValues<arrow::StringType>(array, expectedStr);
        case arrow::Type::BINARY:
            return CheckTypedStrValues<arrow::BinaryType>(array, expectedStr);
        case arrow::Type::FIXED_SIZE_BINARY:
            return CheckTypedStrValues<arrow::FixedSizeBinaryType>(array, expectedStr);

        default:
            Cerr << "type : " << array->type()->ToString() << "\n";
            UNIT_ASSERT(false);
            break;
    }
    return true;
}

bool CheckOrdered(const TString& blob, const TString& srtSchema) {
    auto schema = NArrow::DeserializeSchema(srtSchema);
    auto batch = NArrow::DeserializeBatch(blob, schema);
    UNIT_ASSERT(batch);

    std::shared_ptr<arrow::Array> array = batch->GetColumnByName("timestamp");
    UNIT_ASSERT(array);
    if (!array->length()) {
        return true;
    }

    ui64 prev{};
    TString strPrev;
    for (int i = 0; i < array->length(); ++i) {
        ui64 value{};
        TString strValue;

        NArrow::SwitchType(array->type_id(), [&](const auto& type) {
            using TWrap = std::decay_t<decltype(type)>;
            using TArray = typename arrow::TypeTraits<typename TWrap::T>::ArrayType;

            if constexpr (arrow::has_c_type<typename TWrap::T>()) {
                auto& column = static_cast<const TArray&>(*array);
                value = column.Value(i);
                return true;
            }
            if constexpr (arrow::is_base_binary_type<typename TWrap::T>()) {
                auto v = static_cast<const TArray&>(*array).GetView(i);
                strValue = TString(v.data(), v.size());
                return true;
            }

            Cerr << array->type()->ToString() << "\n";
            UNIT_ASSERT(false);
            return false;
        });

        if (arrow::is_base_binary_like(array->type_id())) {
            if (!i) {
                strPrev = strValue;
                continue;
            }

            if (strPrev > strValue) {
                Cerr << "Unordered: " << strPrev << " " << strValue << "\n";
                return false;
            }
        } else {
            if (!i) {
                prev = value;
                continue;
            }

            if (prev > value) {
                Cerr << "Unordered: " << prev << " " << value << "\n";
                return false;
            }
        }
    }
    return true;
}

bool CheckColumns(const std::shared_ptr<arrow::RecordBatch>& batch, const std::vector<TString>& colNames, size_t rowsCount) {
    UNIT_ASSERT(batch);
    UNIT_ASSERT_VALUES_EQUAL((ui64)batch->num_columns(), colNames.size());
    UNIT_ASSERT_VALUES_EQUAL((ui64)batch->num_rows(), rowsCount);
    UNIT_ASSERT(batch->ValidateFull().ok());

    for (size_t i = 0; i < colNames.size(); ++i) {
        auto batchColName = batch->schema()->field(i)->name();
        if (batchColName != colNames[i]) {
            Cerr << "Incorrect order of columns. Expected: `" << colNames[i] << "` got: `" << batchColName << "`.\n";
            Cerr << "Batch schema: " << batch->schema()->ToString() << "\n";
            return false;
        }
    }

    return true;
}

bool CheckColumns(const TString& blob, const NKikimrTxColumnShard::TMetadata& meta, const std::vector<TString>& colNames,
                  size_t rowsCount = 100) {
    auto schema = NArrow::DeserializeSchema(meta.GetSchema());
    auto batch = NArrow::DeserializeBatch(blob, schema);

    return CheckColumns(batch, colNames, rowsCount);
}

std::vector<TString> ReadManyResults(TTestBasicRuntime& runtime, TString& schema,
                                     NKikimrTxColumnShard::TMetadata& meta, ui32 expected = 1000) {
    std::vector<TString> readData;
    TAutoPtr<IEventHandle> handle;
    bool finished = false;
    for (ui32 i = 0; i < expected; ++i) {
        auto event = runtime.GrabEdgeEvent<TEvColumnShard::TEvReadResult>(handle);
        UNIT_ASSERT(event);

        auto& resRead = Proto(event);
        UNIT_ASSERT_EQUAL(resRead.GetOrigin(), TTestTxConfig::TxTablet0);
        UNIT_ASSERT_EQUAL(resRead.GetTxInitiator(), TTestTxConfig::TxTablet1);
        UNIT_ASSERT_EQUAL(resRead.GetStatus(), NKikimrTxColumnShard::EResultStatus::SUCCESS);
        UNIT_ASSERT(resRead.GetData().size() > 0);
        //UNIT_ASSERT_EQUAL(resRead.GetBatch(), 0);
        //Cerr << "GOT BATCH " << resRead.GetBatch() << " data size " << resRead.GetData().size() << "\n";
        if (resRead.GetFinished()) {
            expected = resRead.GetBatch() + 1;
            meta = resRead.GetMeta();
            finished = true;
        }
        readData.push_back(resRead.GetData());

        if (schema.empty()) {
            schema = resRead.GetMeta().GetSchema();
        }
        UNIT_ASSERT(CheckOrdered(resRead.GetData(), schema));
    }
    UNIT_ASSERT(finished);
    return readData;
}

void TestWrite(const TestTableDescription& table) {
    TTestBasicRuntime runtime;
    TTester::Setup(runtime);

    TActorId sender = runtime.AllocateEdgeActor();
    CreateTestBootstrapper(runtime, CreateTestTabletInfo(TTestTxConfig::TxTablet0, TTabletTypes::ColumnShard), &CreateColumnShard);

    TDispatchOptions options;
    options.FinalEvents.push_back(TDispatchOptions::TFinalEventCondition(TEvTablet::EvBoot));
    runtime.DispatchEvents(options);

    //
    ui64 writeId = 0;
    ui64 tableId = 1;

    SetupSchema(runtime, sender, tableId, table);

    const std::vector<std::pair<TString, TTypeInfo>>& ydbSchema = table.Schema;

    bool ok = WriteData(runtime, sender, writeId++, tableId, MakeTestBlob({0, 100}, ydbSchema), ydbSchema);
    UNIT_ASSERT(ok);

    std::vector<std::pair<TString, TTypeInfo>> schema = ydbSchema;

    // no data

    ok = WriteData(runtime, sender, writeId++, tableId, TString(), ydbSchema);
    UNIT_ASSERT(!ok);

    // null column in PK

    TTestBlobOptions optsNulls;
    optsNulls.NullColumns.emplace("timestamp");
    ok = WriteData(runtime, sender, writeId++, tableId, MakeTestBlob({0, 100}, ydbSchema, optsNulls), ydbSchema);
    UNIT_ASSERT(!ok);

    // missing columns

    schema.resize(4);
    ok = WriteData(runtime, sender, writeId++, tableId, MakeTestBlob({0, 100}, schema), schema);
    UNIT_ASSERT(ok);

    // wrong first key column type (with supported layout: Int64 vs Timestamp)
    // It fails only if we specify source schema. No way to detect it from serialized batch data.

    schema = ydbSchema;
    schema[0].second = TTypeInfo(NTypeIds::Int64);
    ok = WriteData(runtime, sender, writeId++, tableId, MakeTestBlob({0, 100}, schema),
                   schema);
    UNIT_ASSERT(!ok);

    // wrong type (no additional schema - fails in case of wrong layout)

    for (size_t i = 0; i < ydbSchema.size(); ++i) {
        schema = ydbSchema;
        schema[i].second = TTypeInfo(NTypeIds::Int8);
        ok = WriteData(runtime, sender, writeId++, tableId, MakeTestBlob({0, 100}, schema), schema);
        UNIT_ASSERT(!ok);
    }

    // wrong type (with additional schema)

    for (size_t i = 0; i < ydbSchema.size(); ++i) {
        schema = ydbSchema;
        schema[i].second = TTypeInfo(NTypeIds::Int64);
        ok = WriteData(runtime, sender, writeId++, tableId, MakeTestBlob({0, 100}, schema), schema);
        UNIT_ASSERT(ok == (ydbSchema[i].second == TTypeInfo(NTypeIds::Int64)));
    }

    schema = ydbSchema;
    schema[1].second = TTypeInfo(NTypeIds::Utf8);
    schema[5].second = TTypeInfo(NTypeIds::Int32);
    ok = WriteData(runtime, sender, writeId++, tableId, MakeTestBlob({0, 100}, schema), schema);
    UNIT_ASSERT(!ok);

    // reordered columns

    THashMap<TString, TTypeInfo> remap(ydbSchema.begin(), ydbSchema.end());

    schema.resize(0);
    for (auto& [name, typeInfo] : remap) {
        schema.push_back({name, typeInfo});
    }

    ok = WriteData(runtime, sender, writeId++, tableId, MakeTestBlob({0, 100}, schema), schema);
    UNIT_ASSERT(ok);

    // too much data

    TString bigData = MakeTestBlob({0, 150 * 1000}, ydbSchema);
    UNIT_ASSERT(bigData.size() > NColumnShard::TLimits::GetMaxBlobSize());
    UNIT_ASSERT(bigData.size() < NColumnShard::TLimits::GetMaxBlobSize() + 2 * 1024 * 1024);
    ok = WriteData(runtime, sender, writeId++, tableId, bigData, ydbSchema);
    UNIT_ASSERT(!ok);
}

void TestWriteOverload(const TestTableDescription& table) {
    TTestBasicRuntime runtime;
    TTester::Setup(runtime);

    TActorId sender = runtime.AllocateEdgeActor();
    CreateTestBootstrapper(runtime, CreateTestTabletInfo(TTestTxConfig::TxTablet0, TTabletTypes::ColumnShard), &CreateColumnShard);

    TDispatchOptions options;
    options.FinalEvents.push_back(TDispatchOptions::TFinalEventCondition(TEvTablet::EvBoot));
    runtime.DispatchEvents(options);

    //
    ui64 writeId = 0;
    ui64 tableId = 1;

    SetupSchema(runtime, sender, tableId, table);

    TString testBlob = MakeTestBlob({0, 100 * 1000}, table.Schema);
    UNIT_ASSERT(testBlob.size() > NOlap::TCompactionLimits::MAX_BLOB_SIZE / 2);
    UNIT_ASSERT(testBlob.size() < NOlap::TCompactionLimits::MAX_BLOB_SIZE);

    const ui64 overloadSize = NColumnShard::TSettings::OverloadWritesSizeInFlight;
    ui32 toCatch = overloadSize / testBlob.size() + 1;
    UNIT_ASSERT_VALUES_EQUAL(toCatch, 22);
    TDeque<TAutoPtr<IEventHandle>> capturedWrites;

    auto captureEvents = [&](TTestActorRuntimeBase&, TAutoPtr<IEventHandle>& ev) {
        if (auto* msg = TryGetPrivateEvent<NColumnShard::TEvPrivate::TEvWriteBlobsResult>(ev)) {
            Cerr << "CATCH TEvWrite, status " << msg->GetPutResult().GetPutStatus() << Endl;
            if (toCatch && msg->GetPutResult().GetPutStatus() != NKikimrProto::UNKNOWN) {
                capturedWrites.push_back(ev.Release());
                --toCatch;
                return true;
            } else {
                return false;
            }
        }
        return false;
    };

    auto resendOneCaptured = [&]() {
        UNIT_ASSERT(capturedWrites.size());
        Cerr << "RESEND TEvWrite" << Endl;
        runtime.Send(capturedWrites.front().Release());
        capturedWrites.pop_front();
    };

    runtime.SetEventFilter(captureEvents);

    const ui32 toSend = toCatch + 1;
    for (ui32 i = 0; i < toSend; ++i) {
        UNIT_ASSERT(WriteData(runtime, sender, ++writeId, tableId, testBlob, table.Schema, false));
    }

    UNIT_ASSERT_VALUES_EQUAL(WaitWriteResult(runtime, TTestTxConfig::TxTablet0), (ui32)NKikimrTxColumnShard::EResultStatus::OVERLOADED);

    while (capturedWrites.size()) {
        resendOneCaptured();
        UNIT_ASSERT_VALUES_EQUAL(WaitWriteResult(runtime, TTestTxConfig::TxTablet0), (ui32)NKikimrTxColumnShard::EResultStatus::SUCCESS);
    }

    UNIT_ASSERT(WriteData(runtime, sender, ++writeId, tableId, testBlob, table.Schema)); // OK after overload
}

// TODO: Improve test. It does not catch KIKIMR-14890
void TestWriteReadDup(const TestTableDescription& table = {}) {
    TTestBasicRuntime runtime;
    TTester::Setup(runtime);
    auto csDefaultControllerGuard = NKikimr::NYDBTest::TControllers::RegisterCSControllerGuard<TDefaultTestsController>();

    TActorId sender = runtime.AllocateEdgeActor();
    CreateTestBootstrapper(runtime, CreateTestTabletInfo(TTestTxConfig::TxTablet0, TTabletTypes::ColumnShard), &CreateColumnShard);

    TDispatchOptions options;
    options.FinalEvents.push_back(TDispatchOptions::TFinalEventCondition(TEvTablet::EvBoot));
    runtime.DispatchEvents(options);

    //

    ui64 metaShard = TTestTxConfig::TxTablet1;
    ui64 writeId = 0;
    ui64 tableId = 1;

    auto ydbSchema = table.Schema;
    SetupSchema(runtime, sender, tableId);

    constexpr ui32 numRows = 10;
    std::pair<ui64, ui64> portion = {10, 10 + numRows};
    auto testData = MakeTestBlob(portion, ydbSchema);
    TAutoPtr<IEventHandle> handle;

    ui64 txId = 0;
    ui64 initPlanStep = 100;
    for (ui64 planStep = initPlanStep; planStep < initPlanStep + 50; ++planStep) {
        TSet<ui64> txIds;
        for (ui32 i = 0; i <= 5; ++i) {
            std::vector<ui64> writeIds;
            UNIT_ASSERT(WriteData(runtime, sender, ++writeId, tableId, testData, ydbSchema, true, &writeIds));
            ProposeCommit(runtime, sender, ++txId, writeIds);
            txIds.insert(txId);
        }
        PlanCommit(runtime, sender, planStep, txIds);

        // read
        if (planStep != initPlanStep) {
            ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender,
                        new TEvColumnShard::TEvRead(sender, metaShard, planStep-1, Max<ui64>(), tableId));

            TString schema;
            NKikimrTxColumnShard::TMetadata meta;
            std::vector<TString> readData = ReadManyResults(runtime, schema, meta);
            UNIT_ASSERT(DataHas(readData, schema, portion, true));
        }
    }
}

void TestWriteReadLongTxDup() {
    TTestBasicRuntime runtime;
    TTester::Setup(runtime);
    auto csDefaultControllerGuard = NKikimr::NYDBTest::TControllers::RegisterCSControllerGuard<TDefaultTestsController>();

    TActorId sender = runtime.AllocateEdgeActor();
    CreateTestBootstrapper(runtime, CreateTestTabletInfo(TTestTxConfig::TxTablet0, TTabletTypes::ColumnShard), &CreateColumnShard);

    TDispatchOptions options;
    options.FinalEvents.push_back(TDispatchOptions::TFinalEventCondition(TEvTablet::EvBoot));
    runtime.DispatchEvents(options);

    //

    ui64 tableId = 1;
    auto ydbSchema = TTestSchema::YdbSchema();
    SetupSchema(runtime, sender, tableId);

    constexpr ui32 numRows = 10;
    std::pair<ui64, ui64> portion = {10, 10 + numRows};

    NLongTxService::TLongTxId longTxId;
    UNIT_ASSERT(longTxId.ParseString("ydb://long-tx/01ezvvxjdk2hd4vdgjs68knvp8?node_id=1"));

    ui64 txId = 0;
    ui64 planStep = 100;
    std::optional<ui64> writeId;

    // Only the first blob with dedup pair {longTx, dedupId} should be inserted
    // Others should return OK (write retries emulation)
    for (ui32 i = 0; i < 4; ++i) {
        auto data = MakeTestBlob({portion.first + i, portion.second + i}, ydbSchema);
        UNIT_ASSERT(data.size() < NColumnShard::TLimits::MIN_BYTES_TO_INSERT);

        auto writeIdOpt = WriteData(runtime, sender, longTxId, tableId, 1, data, ydbSchema);
        UNIT_ASSERT(writeIdOpt);
        if (!i) {
            writeId = *writeIdOpt;
        }
        UNIT_ASSERT_EQUAL(*writeIdOpt, *writeId);
    }

    ProposeCommit(runtime, sender, ++txId, {*writeId});
    TSet<ui64> txIds = {txId};
    PlanCommit(runtime, sender, planStep, txIds);

    // read
    TAutoPtr<IEventHandle> handle;
    {
        ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender,
                    new TEvColumnShard::TEvRead(sender, 0, planStep, txId, tableId));
        auto event = runtime.GrabEdgeEvent<TEvColumnShard::TEvReadResult>(handle);
        UNIT_ASSERT(event);

        auto& resRead = Proto(event);
        UNIT_ASSERT_EQUAL(resRead.GetOrigin(), TTestTxConfig::TxTablet0);
        UNIT_ASSERT_EQUAL(resRead.GetTxInitiator(), 0);
        UNIT_ASSERT_EQUAL(resRead.GetStatus(), NKikimrTxColumnShard::EResultStatus::SUCCESS);
        UNIT_ASSERT_EQUAL(resRead.GetBatch(), 0);
        UNIT_ASSERT_EQUAL(resRead.GetFinished(), true);
        UNIT_ASSERT(resRead.GetData().size() > 0);

        auto data = resRead.GetData();
        auto meta = resRead.GetMeta();
        UNIT_ASSERT(CheckColumns(data, meta, TTestSchema::ExtractNames(ydbSchema), numRows));
        UNIT_ASSERT(DataHas(std::vector<TString>{data}, meta.GetSchema(), portion, true));
        UNIT_ASSERT(DataHasOnly(std::vector<TString>{data}, meta.GetSchema(), portion));
    }
}

void TestWriteRead(bool reboots, const TestTableDescription& table = {}, TString codec = "") {
    auto csControllerGuard = NKikimr::NYDBTest::TControllers::RegisterCSControllerGuard<TDisableCompactionController>();
    TTestBasicRuntime runtime;
    TTester::Setup(runtime);

    runtime.SetLogPriority(NKikimrServices::BLOB_CACHE, NActors::NLog::PRI_DEBUG);
    runtime.SetLogPriority(NKikimrServices::TX_COLUMNSHARD, NActors::NLog::PRI_DEBUG);

    TActorId sender = runtime.AllocateEdgeActor();
    CreateTestBootstrapper(runtime, CreateTestTabletInfo(TTestTxConfig::TxTablet0, TTabletTypes::ColumnShard), &CreateColumnShard);

    TDispatchOptions options;
    options.FinalEvents.push_back(TDispatchOptions::TFinalEventCondition(TEvTablet::EvBoot));
    runtime.DispatchEvents(options);

    auto write = [&](TTestBasicRuntime& runtime, TActorId& sender, ui64 writeId, ui64 tableId,
                     const TString& data, const std::vector<std::pair<TString, TTypeInfo>>& ydbSchema, std::vector<ui64>& intWriteIds) {
        bool ok = WriteData(runtime, sender, writeId, tableId, data, ydbSchema, true, &intWriteIds);
        if (reboots) {
            RebootTablet(runtime, TTestTxConfig::TxTablet0, sender);
        }
        return ok;
    };

    auto proposeCommit = [&](TTestBasicRuntime& runtime, TActorId& sender, ui64 txId,
                             const std::vector<ui64>& writeIds) {
        ProposeCommit(runtime, sender, txId, writeIds);
        if (reboots) {
            RebootTablet(runtime, TTestTxConfig::TxTablet0, sender);
        }
    };

    auto planCommit = [&](TTestBasicRuntime& runtime, TActorId& sender, ui64 planStep, ui64 txId) {
        PlanCommit(runtime, sender, planStep, txId);
        if (reboots) {
            RebootTablet(runtime, TTestTxConfig::TxTablet0, sender);
        }
    };

    //

    ui64 metaShard = TTestTxConfig::TxTablet1;
    ui64 writeId = 0;
    ui64 tableId = 1;

    SetupSchema(runtime, sender, tableId, table, codec);

    const std::vector<std::pair<TString, TTypeInfo>>& ydbSchema = table.Schema;
    const std::vector<std::pair<TString, TTypeInfo>>& testYdbPk = table.Pk;

    // ----xx
    // -----xx..
    // xx----
    // -xxxxx
    std::vector<std::pair<ui64, ui64>> portion = {
        {200, 300},
        {250, 250 + 80 * 1000}, // committed -> index
        {0, 100},
        {50, 300}
    };

    // write 1: ins:1, cmt:0, idx:0

    std::vector<ui64> intWriteIds;
    UNIT_ASSERT(write(runtime, sender, writeId, tableId, MakeTestBlob(portion[0], ydbSchema), ydbSchema, intWriteIds));

    // read
    TAutoPtr<IEventHandle> handle;
    {
        NActors::TLogContextGuard guard = NActors::TLogContextBuilder::Build(NKikimrServices::TX_COLUMNSHARD)("TEST_STEP", 1);
        ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender,
            new TEvColumnShard::TEvRead(sender, metaShard, 0, 0, tableId));
        auto event2 = runtime.GrabEdgeEvent<TEvColumnShard::TEvReadResult>(handle);
        UNIT_ASSERT(event2);

        auto& resRead = Proto(event2);
        UNIT_ASSERT_EQUAL(resRead.GetOrigin(), TTestTxConfig::TxTablet0);
        UNIT_ASSERT_EQUAL(resRead.GetTxInitiator(), metaShard);
        UNIT_ASSERT_EQUAL(resRead.GetStatus(), NKikimrTxColumnShard::EResultStatus::SUCCESS);
        UNIT_ASSERT_EQUAL(resRead.GetBatch(), 0);
        UNIT_ASSERT_EQUAL(resRead.GetFinished(), true);
        UNIT_ASSERT_EQUAL(resRead.GetData(), "");
    }
    // commit 1: ins:0, cmt:1, idx:0

    ui64 planStep = 21;
    ui64 txId = 100;
    proposeCommit(runtime, sender, txId, intWriteIds);
    planCommit(runtime, sender, planStep, txId);

    // read 2 (committed, old snapshot)
    {
        NActors::TLogContextGuard guard = NActors::TLogContextBuilder::Build(NKikimrServices::TX_COLUMNSHARD)("TEST_STEP", 2);
        ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender,
            new TEvColumnShard::TEvRead(sender, metaShard, 0, 0, tableId));
        auto event5 = runtime.GrabEdgeEvent<TEvColumnShard::TEvReadResult>(handle);
        UNIT_ASSERT(event5);

        auto& resRead2 = Proto(event5);
        UNIT_ASSERT_EQUAL(resRead2.GetOrigin(), TTestTxConfig::TxTablet0);
        UNIT_ASSERT_EQUAL(resRead2.GetTxInitiator(), metaShard);
        UNIT_ASSERT_EQUAL(resRead2.GetStatus(), NKikimrTxColumnShard::EResultStatus::SUCCESS);
        UNIT_ASSERT_EQUAL(resRead2.GetBatch(), 0);
        UNIT_ASSERT_EQUAL(resRead2.GetFinished(), true);
        UNIT_ASSERT_EQUAL(resRead2.GetData(), "");
    }

    // read 3 (committed)
    {
        NActors::TLogContextGuard guard = NActors::TLogContextBuilder::Build(NKikimrServices::TX_COLUMNSHARD)("TEST_STEP", 3);
        ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender,
            new TEvColumnShard::TEvRead(sender, metaShard, planStep, txId, tableId));
        auto event6 = runtime.GrabEdgeEvent<TEvColumnShard::TEvReadResult>(handle);
        UNIT_ASSERT(event6);

        auto& resRead3 = Proto(event6);
        UNIT_ASSERT_EQUAL(resRead3.GetOrigin(), TTestTxConfig::TxTablet0);
        UNIT_ASSERT_EQUAL(resRead3.GetTxInitiator(), metaShard);
        UNIT_ASSERT_EQUAL(resRead3.GetStatus(), NKikimrTxColumnShard::EResultStatus::SUCCESS);
        UNIT_ASSERT_EQUAL(resRead3.GetBatch(), 0);
        UNIT_ASSERT_EQUAL(resRead3.GetFinished(), true);
        //UNIT_ASSERT_EQUAL(resRead3.GetData(), data);
        UNIT_ASSERT(resRead3.GetData().size() > 0);
        UNIT_ASSERT(CheckColumns(resRead3.GetData(), resRead3.GetMeta(), TTestSchema::ExtractNames(ydbSchema)));
        {
            std::vector<TString> readData;
            readData.push_back(resRead3.GetData());
            auto& schema = resRead3.GetMeta().GetSchema();
            UNIT_ASSERT(DataHas(readData, schema, portion[0]));
            UNIT_ASSERT(CheckOrdered(resRead3.GetData(), schema));
        }
    }

    // read 4 (column by id)
    {
        NActors::TLogContextGuard guard = NActors::TLogContextBuilder::Build(NKikimrServices::TX_COLUMNSHARD)("TEST_STEP", 4);
        auto read_col1 = std::make_unique<TEvColumnShard::TEvRead>(sender, metaShard, planStep, txId, tableId);
        Proto(read_col1.get()).AddColumnIds(1);
        ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender, read_col1.release());
        auto event7 = runtime.GrabEdgeEvent<TEvColumnShard::TEvReadResult>(handle);
        UNIT_ASSERT(event7);

        auto& resRead4 = Proto(event7);
        UNIT_ASSERT_EQUAL(resRead4.GetOrigin(), TTestTxConfig::TxTablet0);
        UNIT_ASSERT_EQUAL(resRead4.GetTxInitiator(), metaShard);
        UNIT_ASSERT_EQUAL(resRead4.GetStatus(), NKikimrTxColumnShard::EResultStatus::SUCCESS);
        UNIT_ASSERT_EQUAL(resRead4.GetBatch(), 0);
        UNIT_ASSERT_EQUAL(resRead4.GetFinished(), true);
        UNIT_ASSERT(CheckColumns(resRead4.GetData(), resRead4.GetMeta(), {"timestamp"}));
        {
            auto& schema = resRead4.GetMeta().GetSchema();
            UNIT_ASSERT(CheckOrdered(resRead4.GetData(), schema));
        }
    }
    // read 5 (2 columns by name)

    auto read_col2 = std::make_unique<TEvColumnShard::TEvRead>(sender, metaShard, planStep, txId, tableId);
    Proto(read_col2.get()).AddColumnNames("timestamp");
    Proto(read_col2.get()).AddColumnNames("message");
    ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender, read_col2.release());
    auto event8 = runtime.GrabEdgeEvent<TEvColumnShard::TEvReadResult>(handle);
    UNIT_ASSERT(event8);

    auto& resRead5 = Proto(event8);
    UNIT_ASSERT_EQUAL(resRead5.GetOrigin(), TTestTxConfig::TxTablet0);
    UNIT_ASSERT_EQUAL(resRead5.GetTxInitiator(), metaShard);
    UNIT_ASSERT_EQUAL(resRead5.GetStatus(), NKikimrTxColumnShard::EResultStatus::SUCCESS);
    UNIT_ASSERT_EQUAL(resRead5.GetBatch(), 0);
    UNIT_ASSERT_EQUAL(resRead5.GetFinished(), true);
    UNIT_ASSERT(CheckColumns(resRead5.GetData(), resRead5.GetMeta(), {"timestamp", "message"}));
    {
        auto& schema = resRead5.GetMeta().GetSchema();
        UNIT_ASSERT(CheckOrdered(resRead5.GetData(), schema));
    }

    // write 2 (big portion of data): ins:1, cmt:1, idx:0

    ++writeId;
    intWriteIds.clear();
    {
        TString triggerData = MakeTestBlob(portion[1], ydbSchema);
        UNIT_ASSERT(triggerData.size() > NColumnShard::TLimits::MIN_BYTES_TO_INSERT);
        UNIT_ASSERT(write(runtime, sender, writeId, tableId, triggerData, ydbSchema, intWriteIds));
    }

    // commit 2 (init indexation): ins:0, cmt:0, idx:1

    planStep = 22;
    ++txId;
    proposeCommit(runtime, sender, txId, intWriteIds);
    planCommit(runtime, sender, planStep, txId);

    // write 3: ins:1, cmt:0, idx:1

    ++writeId;
    intWriteIds.clear();
    UNIT_ASSERT(write(runtime, sender, writeId, tableId, MakeTestBlob(portion[2], ydbSchema), ydbSchema, intWriteIds));

    // read 6, planstep 0

    ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender,
                new TEvColumnShard::TEvRead(sender, metaShard, 0, 0, tableId));
    auto event9 = runtime.GrabEdgeEvent<TEvColumnShard::TEvReadResult>(handle);
    UNIT_ASSERT(event9);

    auto& resRead6 = Proto(event9);
    UNIT_ASSERT_EQUAL(resRead6.GetOrigin(), TTestTxConfig::TxTablet0);
    UNIT_ASSERT_EQUAL(resRead6.GetTxInitiator(), metaShard);
    UNIT_ASSERT_EQUAL(resRead6.GetStatus(), NKikimrTxColumnShard::EResultStatus::SUCCESS);
    UNIT_ASSERT_EQUAL(resRead6.GetBatch(), 0);
    UNIT_ASSERT_EQUAL(resRead6.GetFinished(), true);
    UNIT_ASSERT_EQUAL(resRead6.GetData(), "");

    // read 7, planstep 21 (part of index)

    ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender,
                new TEvColumnShard::TEvRead(sender, metaShard, 21, txId, tableId));
    auto event10 = runtime.GrabEdgeEvent<TEvColumnShard::TEvReadResult>(handle);
    UNIT_ASSERT(event10);

    auto& resRead7 = Proto(event10);
    UNIT_ASSERT_EQUAL(resRead7.GetOrigin(), TTestTxConfig::TxTablet0);
    UNIT_ASSERT_EQUAL(resRead7.GetTxInitiator(), metaShard);
    UNIT_ASSERT_EQUAL(resRead7.GetStatus(), NKikimrTxColumnShard::EResultStatus::SUCCESS);
    UNIT_ASSERT_EQUAL(resRead7.GetBatch(), 0);
    UNIT_ASSERT_EQUAL(resRead7.GetFinished(), true);
    UNIT_ASSERT(resRead7.GetData().size() > 0);

    {
        std::vector<TString> readData;
        readData.push_back(resRead7.GetData());
        auto& schema = resRead7.GetMeta().GetSchema();
        UNIT_ASSERT(DataHas(readData, schema, portion[0])); // checks no checks REPLACE (indexed vs indexed)
        UNIT_ASSERT(!DataHas(readData, schema, portion[1])); // checks snapshot filter in indexed data
        UNIT_ASSERT(!DataHas(readData, schema, portion[2]));
        UNIT_ASSERT(CheckOrdered(resRead7.GetData(), schema));
    }

    // read 8, planstep 22 (full index)

    ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender,
                new TEvColumnShard::TEvRead(sender, metaShard, 22, txId, tableId));
    auto event11 = runtime.GrabEdgeEvent<TEvColumnShard::TEvReadResult>(handle);
    UNIT_ASSERT(event11);

    auto& resRead8 = Proto(event11);
    UNIT_ASSERT_EQUAL(resRead8.GetOrigin(), TTestTxConfig::TxTablet0);
    UNIT_ASSERT_EQUAL(resRead8.GetTxInitiator(), metaShard);
    UNIT_ASSERT_EQUAL(resRead8.GetStatus(), NKikimrTxColumnShard::EResultStatus::SUCCESS);
    UNIT_ASSERT_EQUAL(resRead8.GetBatch(), 0);
    UNIT_ASSERT_EQUAL(resRead8.GetFinished(), true);
    UNIT_ASSERT(resRead8.GetData().size() > 0);

    {
        std::vector<TString> readData;
        readData.push_back(resRead8.GetData());
        auto& schema = resRead8.GetMeta().GetSchema();
        UNIT_ASSERT(DataHas(readData, schema, portion[0], true)); // checks REPLACE (indexed vs indexed)
        UNIT_ASSERT(DataHas(readData, schema, portion[1]));
        UNIT_ASSERT(!DataHas(readData, schema, portion[2]));
        UNIT_ASSERT(CheckOrdered(resRead8.GetData(), schema));
    }

    // commit 3: ins:0, cmt:1, idx:1

    planStep = 23;
    ++txId;
    proposeCommit(runtime, sender, txId, intWriteIds);
    planCommit(runtime, sender, planStep, txId);

    // write 4: ins:1, cmt:1, idx:1

    ++writeId;
    intWriteIds.clear();
    UNIT_ASSERT(write(runtime, sender, writeId, tableId, MakeTestBlob(portion[3], ydbSchema), ydbSchema, intWriteIds));

    // read 9 (committed, indexed)

    ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender,
                new TEvColumnShard::TEvRead(sender, metaShard, 23, txId, tableId));

    TString schema;
    NKikimrTxColumnShard::TMetadata meta;
    std::vector<TString> readData = ReadManyResults(runtime, schema, meta);

    UNIT_ASSERT(DataHas(readData, schema, portion[0]));
    UNIT_ASSERT(DataHas(readData, schema, portion[1]));
    UNIT_ASSERT(DataHas(readData, schema, portion[2]));
    UNIT_ASSERT(!DataHas(readData, schema, portion[3]));

    // commit 4: ins:0, cmt:2, idx:1 (with duplicates in PK)

    planStep = 24;
    ++txId;
    proposeCommit(runtime, sender, txId, intWriteIds);
    planCommit(runtime, sender, planStep, txId);

    // read 10

    ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender,
                new TEvColumnShard::TEvRead(sender, metaShard, 24, txId, tableId));
    readData.clear();
    schema.clear();
    ui32 expected = 1000;
    for (ui32 i = 0; i < expected; ++i) {
        auto event = runtime.GrabEdgeEvent<TEvColumnShard::TEvReadResult>(handle);
        UNIT_ASSERT(event);

        auto& resRead = Proto(event);
        UNIT_ASSERT_EQUAL(resRead.GetOrigin(), TTestTxConfig::TxTablet0);
        UNIT_ASSERT_EQUAL(resRead.GetTxInitiator(), metaShard);
        UNIT_ASSERT_EQUAL(resRead.GetStatus(), NKikimrTxColumnShard::EResultStatus::SUCCESS);
        UNIT_ASSERT(resRead.GetData().size() > 0);
        //UNIT_ASSERT_EQUAL(resRead.GetBatch(), 0);
        //UNIT_ASSERT_EQUAL(resRead.GetFinished(), true);
        bool lastBach = resRead.GetFinished();
        if (lastBach) {
            expected = resRead.GetBatch() + 1;
        }
        readData.push_back(resRead.GetData());

        auto meta = resRead.GetMeta();
        if (schema.empty()) {
            schema = meta.GetSchema();
        }
        UNIT_ASSERT(CheckOrdered(resRead.GetData(), schema));

        if (lastBach) {
            UNIT_ASSERT(meta.HasReadStats());
            auto& readStats = meta.GetReadStats();

            if (ydbSchema == TTestSchema::YdbSchema()) {
                Cerr << codec << "/" << readStats.GetPortionsBytes() << Endl;
//                if (codec == "" || codec == "lz4") {
//                    UNIT_ASSERT_GE(readStats.GetPortionsBytes() / 100000, 40);
//                    UNIT_ASSERT_LE(readStats.GetPortionsBytes() / 100000, 50);
//                } else if (codec == "none") {
//                    UNIT_ASSERT_GE(readStats.GetPortionsBytes() / 100000, 65);
//                    UNIT_ASSERT_LE(readStats.GetPortionsBytes() / 100000, 78);
//                } else if (codec == "zstd") {
//                    UNIT_ASSERT_GE(readStats.GetPortionsBytes() / 100000, 20);
//                    UNIT_ASSERT_LE(readStats.GetPortionsBytes() / 100000, 30);
//                } else {
//                    UNIT_ASSERT(false);
//                }
            }
        }
    }
    UNIT_ASSERT(DataHas(readData, schema, portion[0]));
    UNIT_ASSERT(DataHas(readData, schema, portion[1]));
    UNIT_ASSERT(DataHas(readData, schema, portion[2]));
    UNIT_ASSERT(DataHas(readData, schema, portion[3]));
    UNIT_ASSERT(DataHas(readData, schema, {0, 500}, true)); // checks REPLACE (committed vs indexed)

    // read 11 (range predicate: closed interval)
    {
        TSerializedTableRange range = MakeTestRange({10, 42}, true, true, testYdbPk);
        NOlap::TPredicate prGreater, prLess;
        std::tie(prGreater, prLess) = RangePredicates(range, testYdbPk);

        auto evRead = std::make_unique<TEvColumnShard::TEvRead>(sender, metaShard, 24, txId, tableId);
        auto* greater = Proto(evRead.get()).MutableGreaterPredicate();
        auto* less = Proto(evRead.get()).MutableLessPredicate();
        for (auto& name : prGreater.ColumnNames()) {
            greater->AddColumnNames(name);
        }
        for (auto& name : prLess.ColumnNames()) {
            less->AddColumnNames(name);
        }
        greater->SetRow(NArrow::SerializeBatchNoCompression(prGreater.Batch));
        less->SetRow(NArrow::SerializeBatchNoCompression(prLess.Batch));
        greater->SetInclusive(prGreater.IsInclusive());
        less->SetInclusive(prLess.IsInclusive());

        ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender, evRead.release());
    }
    readData.clear();
    schema.clear();
    {
        auto event = runtime.GrabEdgeEvent<TEvColumnShard::TEvReadResult>(handle);
        UNIT_ASSERT(event);

        auto& resRead = Proto(event);
        UNIT_ASSERT_EQUAL(resRead.GetOrigin(), TTestTxConfig::TxTablet0);
        UNIT_ASSERT_EQUAL(resRead.GetTxInitiator(), metaShard);
        UNIT_ASSERT_EQUAL(resRead.GetStatus(), NKikimrTxColumnShard::EResultStatus::SUCCESS);
        UNIT_ASSERT(resRead.GetData().size() > 0);
        UNIT_ASSERT_EQUAL(resRead.GetBatch(), 0);
        UNIT_ASSERT_EQUAL(resRead.GetFinished(), true);
        readData.push_back(resRead.GetData());
        if (schema.empty()) {
            schema = resRead.GetMeta().GetSchema();
        }
        UNIT_ASSERT(CheckOrdered(resRead.GetData(), schema));
    }
    UNIT_ASSERT(DataHas(readData, schema, {10, 42 + 1}));
    UNIT_ASSERT(DataHasOnly(readData, schema, {10, 42 + 1}));

    // read 12 (range predicate: open interval)
    {
        TSerializedTableRange range = MakeTestRange({10, 42}, false, false, testYdbPk);
        NOlap::TPredicate prGreater, prLess;
        std::tie(prGreater, prLess) = RangePredicates(range, testYdbPk);

        auto evRead = std::make_unique<TEvColumnShard::TEvRead>(sender, metaShard, 24, txId, tableId);
        auto* greater = Proto(evRead.get()).MutableGreaterPredicate();
        auto* less = Proto(evRead.get()).MutableLessPredicate();
        for (auto& name : prGreater.ColumnNames()) {
            greater->AddColumnNames(name);
        }
        for (auto& name : prLess.ColumnNames()) {
            less->AddColumnNames(name);
        }

        greater->SetRow(NArrow::SerializeBatchNoCompression(prGreater.Batch));
        less->SetRow(NArrow::SerializeBatchNoCompression(prLess.Batch));
        greater->SetInclusive(prGreater.IsInclusive());
        less->SetInclusive(prLess.IsInclusive());

        ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender, evRead.release());
    }
    readData.clear();
    schema.clear();
    {
        auto event = runtime.GrabEdgeEvent<TEvColumnShard::TEvReadResult>(handle);
        UNIT_ASSERT(event);

        auto& resRead = Proto(event);
        UNIT_ASSERT_EQUAL(resRead.GetOrigin(), TTestTxConfig::TxTablet0);
        UNIT_ASSERT_EQUAL(resRead.GetTxInitiator(), metaShard);
        UNIT_ASSERT_EQUAL(resRead.GetStatus(), NKikimrTxColumnShard::EResultStatus::SUCCESS);
        UNIT_ASSERT(resRead.GetData().size() > 0);
        UNIT_ASSERT_EQUAL(resRead.GetBatch(), 0);
        UNIT_ASSERT_EQUAL(resRead.GetFinished(), true);
        readData.push_back(resRead.GetData());
        if (schema.empty()) {
            schema = resRead.GetMeta().GetSchema();
        }
        UNIT_ASSERT(CheckOrdered(resRead.GetData(), schema));
    }
    UNIT_ASSERT(DataHas(readData, schema, {11, 41 + 1}));
    UNIT_ASSERT(DataHasOnly(readData, schema, {11, 41 + 1}));
}

void TestCompactionInGranuleImpl(bool reboots, const TestTableDescription& table) {
    TTestBasicRuntime runtime;
    TTester::Setup(runtime);
    auto csDefaultControllerGuard = NKikimr::NYDBTest::TControllers::RegisterCSControllerGuard<TDefaultTestsController>();

    TActorId sender = runtime.AllocateEdgeActor();
    CreateTestBootstrapper(runtime, CreateTestTabletInfo(TTestTxConfig::TxTablet0, TTabletTypes::ColumnShard), &CreateColumnShard);

    TDispatchOptions options;
    options.FinalEvents.push_back(TDispatchOptions::TFinalEventCondition(TEvTablet::EvBoot));
    runtime.DispatchEvents(options);

    auto write = [&](TTestBasicRuntime& runtime, TActorId& sender, ui64 writeId, ui64 tableId,
                     const TString& data, const std::vector<std::pair<TString, TTypeInfo>>& ydbSchema, std::vector<ui64>& writeIds) {
        bool ok = WriteData(runtime, sender, writeId, tableId, data, ydbSchema, true, &writeIds);
        if (reboots) {
            RebootTablet(runtime, TTestTxConfig::TxTablet0, sender);
        }
        return ok;
    };

    auto proposeCommit = [&](TTestBasicRuntime& runtime, TActorId& sender, ui64 txId,
                             const std::vector<ui64>& writeIds) {
        ProposeCommit(runtime, sender, txId, writeIds);
        if (reboots) {
            RebootTablet(runtime, TTestTxConfig::TxTablet0, sender);
        }
    };

    auto planCommit = [&](TTestBasicRuntime& runtime, TActorId& sender, ui64 planStep, ui64 txId) {
        PlanCommit(runtime, sender, planStep, txId);
        if (reboots) {
            RebootTablet(runtime, TTestTxConfig::TxTablet0, sender);
        }
    };

    //

    ui64 metaShard = TTestTxConfig::TxTablet1;
    ui64 writeId = 0;
    ui64 tableId = 1;
    ui64 planStep = 100;
    ui64 txId = 100;

    SetupSchema(runtime, sender, tableId, table);
    TAutoPtr<IEventHandle> handle;
    const auto& ydbSchema = table.Schema;
    const auto& ydbPk = table.Pk;

    // Write same keys: merge on compaction

    static const ui32 triggerPortionSize = 75 * 1000;
    std::pair<ui64, ui64> triggerPortion = {0, triggerPortionSize};
    TString triggerData = MakeTestBlob(triggerPortion, ydbSchema);
    UNIT_ASSERT(triggerData.size() > NColumnShard::TLimits::MIN_BYTES_TO_INSERT);
    UNIT_ASSERT(triggerData.size() < NColumnShard::TLimits::GetMaxBlobSize());

    static const ui32 portionSize = 1;

    ui32 numWrites = NColumnShard::TLimits::MIN_SMALL_BLOBS_TO_INSERT; // trigger InsertTable -> Index

    // inserts triggered by count
    ui32 pos = triggerPortionSize;
    for (ui32 i = 0; i < 1; ++i, ++planStep, ++txId) {
        std::vector<ui64> ids;
        ids.reserve(numWrites);
        for (ui32 w = 0; w < numWrites; ++w, ++writeId, pos += portionSize) {
            std::pair<ui64, ui64> portion = {pos, pos + portionSize};
            TString data = MakeTestBlob(portion, ydbSchema);

            UNIT_ASSERT(WriteData(runtime, sender, writeId, tableId, data, ydbSchema, true, &ids));
        }

        if (reboots) {
            RebootTablet(runtime, TTestTxConfig::TxTablet0, sender);
        }

        proposeCommit(runtime, sender, txId, ids);
        planCommit(runtime, sender, planStep, txId);
    }
    std::pair<ui64, ui64> smallWrites = {triggerPortionSize, pos};

    // inserts triggered by size
    NOlap::TCompactionLimits engineLimits;
    ui32 numTxs = engineLimits.GranuleSizeForOverloadPrevent / triggerData.size() + 1;

    for (ui32 i = 0; i < numTxs; ++i, ++writeId, ++planStep, ++txId) {
        std::vector<ui64> writeIds;
        UNIT_ASSERT(write(runtime, sender, writeId, tableId, triggerData, ydbSchema, writeIds));

        proposeCommit(runtime, sender, txId, writeIds);
        planCommit(runtime, sender, planStep, txId);
    }

    // TODO: Move tablet's time to the future with mediator timecast instead
    --planStep;
    --txId;

    for (ui32 i = 0; i < 2; ++i) {
        auto read = std::make_unique<TEvColumnShard::TEvRead>(sender, metaShard, planStep, txId, tableId);
        Proto(read.get()).AddColumnNames("timestamp");
        Proto(read.get()).AddColumnNames("message");

        ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender, read.release());

        TString schema;
        NKikimrTxColumnShard::TMetadata meta;
        std::vector<TString> readData = ReadManyResults(runtime, schema, meta);

        if (ydbPk[0].second == TTypeInfo(NTypeIds::String) || ydbPk[0].second == TTypeInfo(NTypeIds::Utf8)) {
            UNIT_ASSERT(DataHas<std::string>(readData, schema, triggerPortion, true));
            UNIT_ASSERT(DataHas<std::string>(readData, schema, smallWrites, true));
        } else {
            UNIT_ASSERT(DataHas(readData, schema, triggerPortion, true));
            UNIT_ASSERT(DataHas(readData, schema, smallWrites, true));
        }

        UNIT_ASSERT(meta.HasReadStats());
        auto& readStats = meta.GetReadStats();
        Cerr << readStats.DebugString() << Endl;
        UNIT_ASSERT(readStats.GetBeginTimestamp() > 0);
        UNIT_ASSERT(readStats.GetDurationUsec() > 0);
        UNIT_ASSERT_VALUES_EQUAL(readStats.GetSelectedIndex(), 0);
//        UNIT_ASSERT_VALUES_EQUAL(readStats.GetIndexGranules(), 1);
        UNIT_ASSERT(readStats.GetIndexBatches() > 0);
        UNIT_ASSERT_VALUES_EQUAL(readStats.GetNotIndexedBatches(), 0);
        UNIT_ASSERT_VALUES_EQUAL(readStats.GetSchemaColumns(), 2); // planStep, txId + 4 PK columns + "message"
        UNIT_ASSERT(readStats.GetIndexPortions() > 0); // got compaction
        UNIT_ASSERT(readStats.GetIndexPortions() <= 5); // got compaction

        RebootTablet(runtime, TTestTxConfig::TxTablet0, sender);
    }
}

using TAssignment = NKikimrSSA::TProgram::TAssignment;
using TAggAssignment = NKikimrSSA::TProgram::TAggregateAssignment;

// SELECT level, timestamp FROM t WHERE timestamp <op> saved_at
static NKikimrSSA::TProgram MakeSelect(TAssignment::EFunction compareId = TAssignment::FUNC_CMP_EQUAL) {
    NKikimrSSA::TProgram ssa;

    std::vector<ui32> columnIds = {1, 9, 5};
    ui32 tmpColumnId = 100;

    auto* line1 = ssa.AddCommand();
    auto* l1_assign = line1->MutableAssign();
    l1_assign->MutableColumn()->SetId(tmpColumnId);
    auto* l1_func = l1_assign->MutableFunction();
    l1_func->SetId(compareId);
    l1_func->AddArguments()->SetId(columnIds[0]);
    l1_func->AddArguments()->SetId(columnIds[1]);

    auto* line2 = ssa.AddCommand();
    line2->MutableFilter()->MutablePredicate()->SetId(tmpColumnId);

    auto* line3 = ssa.AddCommand();
    line3->MutableProjection()->AddColumns()->SetId(columnIds[2]);
    line3->MutableProjection()->AddColumns()->SetId(columnIds[0]);
    return ssa;
}

// SELECT level, timestamp FROM t WHERE likeFunc(timestamp, pattern)
static NKikimrSSA::TProgram MakeSelectLike(TAssignment::EFunction likeId, const TString& pattern) {
    NKikimrSSA::TProgram ssa;

    std::vector<ui32> columnIds = {6}; // message

    auto* line1 = ssa.AddCommand();
    auto* l1_assign = line1->MutableAssign();
    l1_assign->MutableColumn()->SetId(100);
    l1_assign->MutableConstant()->SetText(pattern);

    auto* line2 = ssa.AddCommand();
    auto* l2_assign = line2->MutableAssign();
    l2_assign->MutableColumn()->SetId(101);
    auto* l2_func = l2_assign->MutableFunction();
    l2_func->SetId(likeId);
    l2_func->AddArguments()->SetId(columnIds[0]);
    l2_func->AddArguments()->SetId(100);

    auto* line3 = ssa.AddCommand();
    line3->MutableFilter()->MutablePredicate()->SetId(101);

    auto* line4 = ssa.AddCommand();
    line4->MutableProjection()->AddColumns()->SetId(columnIds[0]);
    return ssa;
}

// SELECT min(x), max(x), some(x), count(x) FROM t [GROUP BY key[0], key[1], ...]
NKikimrSSA::TProgram MakeSelectAggregates(ui32 columnId, const std::vector<ui32>& keys = {},
                                          bool addProjection = true)
{
    NKikimrSSA::TProgram ssa;

    auto* line1 = ssa.AddCommand();
    auto* groupBy = line1->MutableGroupBy();
    for (ui32 key : keys) {
        groupBy->AddKeyColumns()->SetId(key + 1);
    }
    //
    auto* l1_agg1 = groupBy->AddAggregates();
    l1_agg1->MutableColumn()->SetId(100);
    auto* l1_agg1_f = l1_agg1->MutableFunction();
    l1_agg1_f->SetId(TAggAssignment::AGG_MIN);
    l1_agg1_f->AddArguments()->SetId(columnId);
    //
    auto* l1_agg2 = groupBy->AddAggregates();
    l1_agg2->MutableColumn()->SetId(101);
    auto* l1_agg2_f = l1_agg2->MutableFunction();
    l1_agg2_f->SetId(TAggAssignment::AGG_MAX);
    l1_agg2_f->AddArguments()->SetId(columnId);
    //
    auto* l1_agg3 = groupBy->AddAggregates();
    l1_agg3->MutableColumn()->SetId(102);
    auto* l1_agg3_f = l1_agg3->MutableFunction();
    l1_agg3_f->SetId(TAggAssignment::AGG_SOME);
    l1_agg3_f->AddArguments()->SetId(columnId);
    //
    auto* l1_agg4 = groupBy->AddAggregates();
    l1_agg4->MutableColumn()->SetId(103);
    auto* l1_agg4_f = l1_agg4->MutableFunction();
    l1_agg4_f->SetId(TAggAssignment::AGG_COUNT);
    l1_agg4_f->AddArguments()->SetId(columnId);

    // Projection by ids
    if (addProjection) {
        auto* line2 = ssa.AddCommand();
        auto* proj = line2->MutableProjection();
        proj->AddColumns()->SetId(100);
        proj->AddColumns()->SetId(101);
        proj->AddColumns()->SetId(102);
        proj->AddColumns()->SetId(103);
    }
    return ssa;
}

// SELECT min(x), max(x), some(x), count(x) FROM t WHERE y = 1 [GROUP BY key[0], key[1], ...]
NKikimrSSA::TProgram MakeSelectAggregatesWithFilter(ui32 columnId, ui32 filterColumnId,
                                                    const std::vector<ui32>& keys = {},
                                                    bool addProjection = true)
{
    NKikimrSSA::TProgram ssa;

    auto* line1 = ssa.AddCommand();
    auto* l1_assign = line1->MutableAssign();
    l1_assign->MutableColumn()->SetId(50);
    l1_assign->MutableConstant()->SetInt32(1);

    auto* line2 = ssa.AddCommand();
    auto* l2_assign = line2->MutableAssign();
    l2_assign->MutableColumn()->SetId(51);
    auto* l2_func = l2_assign->MutableFunction();
    l2_func->SetId(TAssignment::FUNC_CMP_EQUAL);
    l2_func->AddArguments()->SetId(filterColumnId);
    l2_func->AddArguments()->SetId(50);

    auto* line3 = ssa.AddCommand();
    line3->MutableFilter()->MutablePredicate()->SetId(51);

    auto* line4 = ssa.AddCommand();
    auto* groupBy = line4->MutableGroupBy();
    for (ui32 key : keys) {
        groupBy->AddKeyColumns()->SetId(key + 1);
    }
    //
    auto* l4_agg1 = groupBy->AddAggregates();
    //l4_agg1->MutableColumn()->SetId(100);
    l4_agg1->MutableColumn()->SetName("res_min");
    auto* l4_agg1_f = l4_agg1->MutableFunction();
    l4_agg1_f->SetId(TAggAssignment::AGG_MIN);
    l4_agg1_f->AddArguments()->SetId(columnId);
    //
    auto* l4_agg2 = groupBy->AddAggregates();
    //l4_agg2->MutableColumn()->SetId(101);
    l4_agg2->MutableColumn()->SetName("res_max");
    auto* l4_agg2_f = l4_agg2->MutableFunction();
    l4_agg2_f->SetId(TAggAssignment::AGG_MAX);
    l4_agg2_f->AddArguments()->SetId(columnId);
    //
    auto* l4_agg3 = groupBy->AddAggregates();
    //l4_agg3->MutableColumn()->SetId(102);
    l4_agg3->MutableColumn()->SetName("res_some");
    auto* l4_agg3_f = l4_agg3->MutableFunction();
    l4_agg3_f->SetId(TAggAssignment::AGG_SOME);
    l4_agg3_f->AddArguments()->SetId(columnId);
    //
    auto* l4_agg4 = groupBy->AddAggregates();
    //l4_agg4->MutableColumn()->SetId(103);
    l4_agg4->MutableColumn()->SetName("res_count");
    auto* l4_agg4_f = l4_agg4->MutableFunction();
    l4_agg4_f->SetId(TAggAssignment::AGG_COUNT);
    l4_agg4_f->AddArguments()->SetId(columnId);

    // Projection by names
    if (addProjection) {
        auto* line5 = ssa.AddCommand();
        auto* proj = line5->MutableProjection();
        proj->AddColumns()->SetName("res_min");
        proj->AddColumns()->SetName("res_max");
        proj->AddColumns()->SetName("res_some");
        proj->AddColumns()->SetName("res_count");
    }
    return ssa;
}

void TestReadWithProgram(const TestTableDescription& table = {})
{
    TTestBasicRuntime runtime;
    TTester::Setup(runtime);

    TActorId sender = runtime.AllocateEdgeActor();
    CreateTestBootstrapper(runtime, CreateTestTabletInfo(TTestTxConfig::TxTablet0, TTabletTypes::ColumnShard), &CreateColumnShard);

    TDispatchOptions options;
    options.FinalEvents.push_back(TDispatchOptions::TFinalEventCondition(TEvTablet::EvBoot));
    runtime.DispatchEvents(options);

    ui64 metaShard = TTestTxConfig::TxTablet1;
    ui64 writeId = 0;
    ui64 tableId = 1;
    ui64 planStep = 100;
    ui64 txId = 100;

    SetupSchema(runtime, sender, tableId, table);

    { // write some data
        std::vector<ui64> writeIds;
        bool ok = WriteData(runtime, sender, writeId, tableId, MakeTestBlob({0, 100}, table.Schema), table.Schema, true, &writeIds);
        UNIT_ASSERT(ok);

        ProposeCommit(runtime, sender, txId, writeIds);
        PlanCommit(runtime, sender, planStep, txId);
    }

    ui32 numWrong = 1;
    std::vector<TString> programs;
    programs.push_back("XXXYYYZZZ");

    {
        NKikimrSSA::TProgram ssa = MakeSelect(TAssignment::FUNC_CMP_EQUAL);
        TString serialized;
        UNIT_ASSERT(ssa.SerializeToString(&serialized));
        NKikimrSSA::TOlapProgram program;
        program.SetProgram(serialized);

        programs.push_back("");
        UNIT_ASSERT(program.SerializeToString(&programs.back()));
    }

    {
        NKikimrSSA::TProgram ssa = MakeSelect(TAssignment::FUNC_CMP_NOT_EQUAL);
        TString serialized;
        UNIT_ASSERT(ssa.SerializeToString(&serialized));
        NKikimrSSA::TOlapProgram program;
        program.SetProgram(serialized);

        programs.push_back("");
        UNIT_ASSERT(program.SerializeToString(&programs.back()));
    }

    for (auto& programText : programs) {
        auto* readEvent = new TEvColumnShard::TEvRead(sender, metaShard, planStep, txId, tableId);
        auto& readProto = Proto(readEvent);

        readProto.SetOlapProgramType(::NKikimrSchemeOp::EOlapProgramType::OLAP_PROGRAM_SSA_PROGRAM);
        readProto.SetOlapProgram(programText);

        ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender, readEvent);

        TAutoPtr<IEventHandle> handle;
        auto result = runtime.GrabEdgeEvent<TEvColumnShard::TEvReadResult>(handle);
        UNIT_ASSERT(result);

        auto& resRead = Proto(result);

        UNIT_ASSERT_EQUAL(resRead.GetOrigin(), TTestTxConfig::TxTablet0);
        UNIT_ASSERT_EQUAL(resRead.GetTxInitiator(), metaShard);
        UNIT_ASSERT_EQUAL(resRead.GetStatus(), NKikimrTxColumnShard::EResultStatus::ERROR);
        UNIT_ASSERT_EQUAL(resRead.GetBatch(), 0);
        UNIT_ASSERT_EQUAL(resRead.GetFinished(), true);
        UNIT_ASSERT_EQUAL(resRead.GetData(), "");
    }

    ui32 i = 0;
    for (auto& programText : programs) {
        auto* readEvent = new TEvColumnShard::TEvRead(sender, metaShard, planStep, txId, tableId);
        auto& readProto = Proto(readEvent);

        readProto.SetOlapProgramType(::NKikimrSchemeOp::EOlapProgramType::OLAP_PROGRAM_SSA_PROGRAM_WITH_PARAMETERS);
        readProto.SetOlapProgram(programText);

        ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender, readEvent);

        TAutoPtr<IEventHandle> handle;
        auto result = runtime.GrabEdgeEvent<TEvColumnShard::TEvReadResult>(handle);
        UNIT_ASSERT(result);

        auto& resRead = Proto(result);
        UNIT_ASSERT_EQUAL(resRead.GetOrigin(), TTestTxConfig::TxTablet0);
        UNIT_ASSERT_EQUAL(resRead.GetTxInitiator(), metaShard);
        if (i < numWrong) {
            UNIT_ASSERT_EQUAL(resRead.GetStatus(), NKikimrTxColumnShard::EResultStatus::ERROR);
            UNIT_ASSERT_EQUAL(resRead.GetBatch(), 0);
            UNIT_ASSERT_EQUAL(resRead.GetFinished(), true);
            UNIT_ASSERT_EQUAL(resRead.GetData(), "");
        } else {
            UNIT_ASSERT_EQUAL(resRead.GetStatus(), NKikimrTxColumnShard::EResultStatus::SUCCESS);
            UNIT_ASSERT_EQUAL(resRead.GetBatch(), 0);
            UNIT_ASSERT_EQUAL(resRead.GetFinished(), true);

            auto& meta = resRead.GetMeta();
            auto& schema = meta.GetSchema();

            std::vector<TString> readData;
            readData.push_back(resRead.GetData());

            switch (i) {
                case 1:
                    UNIT_ASSERT(resRead.GetData().size() > 0);
                    UNIT_ASSERT(CheckColumns(readData[0], meta, {"level", "timestamp"}));
                    UNIT_ASSERT(DataHas(readData, schema, {0, 100}, true));
                    break;
                case 2:
                    UNIT_ASSERT(resRead.GetData().size() == 0);
                    break;
                default:
                    break;
            }
        }
        ++i;
    }
}

void TestReadWithProgramLike(const TestTableDescription& table = {}) {
    TTestBasicRuntime runtime;
    TTester::Setup(runtime);

    TActorId sender = runtime.AllocateEdgeActor();
    CreateTestBootstrapper(runtime,
        CreateTestTabletInfo(TTestTxConfig::TxTablet0, TTabletTypes::ColumnShard), &CreateColumnShard);

    TDispatchOptions options;
    options.FinalEvents.push_back(TDispatchOptions::TFinalEventCondition(TEvTablet::EvBoot));
    runtime.DispatchEvents(options);

    ui64 metaShard = TTestTxConfig::TxTablet1;
    ui64 writeId = 0;
    ui64 tableId = 1;
    ui64 planStep = 100;
    ui64 txId = 100;

    SetupSchema(runtime, sender, tableId, table);

    { // write some data
        std::vector<ui64> writeIds;
        bool ok = WriteData(runtime, sender, writeId, tableId, MakeTestBlob({0, 100}, table.Schema), table.Schema, true, &writeIds);
        UNIT_ASSERT(ok);

        ProposeCommit(runtime, sender, txId, writeIds);
        PlanCommit(runtime, sender, planStep, txId);
    }

    TString pattern = "1";
    std::vector<NKikimrSSA::TProgram> ssas = {
        MakeSelectLike(TAssignment::FUNC_STR_MATCH, pattern),
        MakeSelectLike(TAssignment::FUNC_STR_MATCH_IGNORE_CASE, pattern),
        MakeSelectLike(TAssignment::FUNC_STR_STARTS_WITH, pattern),
        MakeSelectLike(TAssignment::FUNC_STR_STARTS_WITH_IGNORE_CASE, pattern),
        MakeSelectLike(TAssignment::FUNC_STR_ENDS_WITH, pattern),
        MakeSelectLike(TAssignment::FUNC_STR_ENDS_WITH_IGNORE_CASE, pattern)
    };

    ui32 i = 0;
    for (auto& ssa : ssas) {
        TString programText;
        {
            TString serialized;
            UNIT_ASSERT(ssa.SerializeToString(&serialized));
            NKikimrSSA::TOlapProgram program;
            program.SetProgram(serialized);
            UNIT_ASSERT(program.SerializeToString(&programText));
        }

        auto* readEvent = new TEvColumnShard::TEvRead(sender, metaShard, planStep, txId, tableId);
        auto& readProto = Proto(readEvent);

        readProto.SetOlapProgramType(::NKikimrSchemeOp::EOlapProgramType::OLAP_PROGRAM_SSA_PROGRAM_WITH_PARAMETERS);
        readProto.SetOlapProgram(programText);

        ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender, readEvent);

        TAutoPtr<IEventHandle> handle;
        auto result = runtime.GrabEdgeEvent<TEvColumnShard::TEvReadResult>(handle);
        UNIT_ASSERT(result);

        auto& resRead = Proto(result);
        UNIT_ASSERT_EQUAL(resRead.GetOrigin(), TTestTxConfig::TxTablet0);
        UNIT_ASSERT_EQUAL(resRead.GetTxInitiator(), metaShard);
        {
            UNIT_ASSERT_EQUAL(resRead.GetStatus(), NKikimrTxColumnShard::EResultStatus::SUCCESS);
            UNIT_ASSERT_EQUAL(resRead.GetBatch(), 0);
            UNIT_ASSERT_EQUAL(resRead.GetFinished(), true);
            UNIT_ASSERT(resRead.GetData().size() > 0);

            auto& meta = resRead.GetMeta();
            //auto& schema = meta.GetSchema();
            TString readData = resRead.GetData();

            switch (i) {
                case 0:
                case 1:
                    UNIT_ASSERT(CheckColumns(readData, meta, {"message"}, 19));
                    break;
                case 2:
                case 3:
                    UNIT_ASSERT(CheckColumns(readData, meta, {"message"}, 11));
                    break;
                case 4:
                case 5:
                    UNIT_ASSERT(CheckColumns(readData, meta, {"message"}, 10));
                    break;
                default:
                    break;
            }
        }
        ++i;
    }
}

void TestSomePrograms(const TestTableDescription& table) {
    TTestBasicRuntime runtime;
    TTester::Setup(runtime);

    TActorId sender = runtime.AllocateEdgeActor();
    CreateTestBootstrapper(runtime, CreateTestTabletInfo(TTestTxConfig::TxTablet0, TTabletTypes::ColumnShard), &CreateColumnShard);

    TDispatchOptions options;
    options.FinalEvents.push_back(TDispatchOptions::TFinalEventCondition(TEvTablet::EvBoot));
    runtime.DispatchEvents(options);

    ui64 metaShard = TTestTxConfig::TxTablet1;
    ui64 writeId = 0;
    ui64 tableId = 1;
    ui64 planStep = 100;
    ui64 txId = 100;

    SetupSchema(runtime, sender, tableId, table);

    { // write some data
        std::vector<ui64> writeIds;
        bool ok = WriteData(runtime, sender, writeId, tableId, MakeTestBlob({0, 100}, table.Schema), table.Schema, true, &writeIds);
        UNIT_ASSERT(ok);

        ProposeCommit(runtime, sender, txId, writeIds);
        PlanCommit(runtime, sender, planStep, txId);
    }

    std::vector<TString> programs;
    // SELECT COUNT(*) FROM /Root/olapStore/olapTable WHERE level = 2 -- bug: "level = 2" appears two times
    programs.push_back(R"(
        Command { Assign { Column { Id: 6 } Constant { Int32: 2 } } }
        Command { Assign { Column { Id: 7 } Function { Id: 1 Arguments { Id: 4 } Arguments { Id: 6 } } } }
        Command { Filter { Predicate { Id: 7 } } }
        Command { Assign { Column { Id: 8 } Constant { Int32: 2 } } }
        Command { Assign { Column { Id: 9 } Function { Id: 1 Arguments { Id: 4 } Arguments { Id: 8 } } } }
        Command { Filter { Predicate { Id: 9 } } }
        Command { GroupBy { Aggregates { Column { Id: 10 } Function { Id: 2 } } } }
        Command { Projection { Columns { Id: 10 } } }
        Version: 1
    )");
    // TODO: add programs with bugs here

    for (auto& ssaText : programs) {
        auto* readEvent = new TEvColumnShard::TEvRead(sender, metaShard, planStep, txId, tableId);
        auto& readProto = Proto(readEvent);

        TString programText;
        NKikimrSSA::TOlapProgram program;
        program.SetProgram(ssaText);
        UNIT_ASSERT(program.SerializeToString(&programText));

        readProto.SetOlapProgramType(::NKikimrSchemeOp::EOlapProgramType::OLAP_PROGRAM_SSA_PROGRAM);
        readProto.SetOlapProgram(programText);

        ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender, readEvent);

        TAutoPtr<IEventHandle> handle;
        auto result = runtime.GrabEdgeEvent<TEvColumnShard::TEvReadResult>(handle);
        UNIT_ASSERT(result);

        auto& resRead = Proto(result);

        UNIT_ASSERT_EQUAL(resRead.GetOrigin(), TTestTxConfig::TxTablet0);
        UNIT_ASSERT_EQUAL(resRead.GetTxInitiator(), metaShard);
        UNIT_ASSERT_EQUAL(resRead.GetStatus(), NKikimrTxColumnShard::EResultStatus::ERROR);
        UNIT_ASSERT_EQUAL(resRead.GetBatch(), 0);
        UNIT_ASSERT_EQUAL(resRead.GetFinished(), true);
        //UNIT_ASSERT_EQUAL(resRead.GetData(), "");
    }
}

struct TReadAggregateResult {
    ui32 NumRows = 1;

    std::vector<int64_t> MinValues = {0};
    std::vector<int64_t> MaxValues = {99};
    std::vector<int64_t> Counts = {100};
};

void TestReadAggregate(const std::vector<std::pair<TString, TTypeInfo>>& ydbSchema, const TString& testDataBlob,
                       bool addProjection, const std::vector<ui32>& aggKeys = {},
                       const TReadAggregateResult& expectedResult = {},
                       const TReadAggregateResult& expectedFiltered = {1, {1}, {1}, {1}}) {
    TTestBasicRuntime runtime;
    TTester::Setup(runtime);

    TActorId sender = runtime.AllocateEdgeActor();
    CreateTestBootstrapper(runtime, CreateTestTabletInfo(TTestTxConfig::TxTablet0, TTabletTypes::ColumnShard), &CreateColumnShard);

    TDispatchOptions options;
    options.FinalEvents.push_back(TDispatchOptions::TFinalEventCondition(TEvTablet::EvBoot));
    runtime.DispatchEvents(options);

    ui64 metaShard = TTestTxConfig::TxTablet1;
    ui64 writeId = 0;
    ui64 tableId = 1;
    ui64 planStep = 100;
    ui64 txId = 100;

    auto pk = ydbSchema;
    pk.resize(4);
    TestTableDescription table{.Schema = ydbSchema, .Pk = pk};
    SetupSchema(runtime, sender, tableId, table);

    { // write some data
        std::vector<ui64> writeIds;
        bool ok = WriteData(runtime, sender, writeId, tableId, testDataBlob, table.Schema, true, &writeIds);
        UNIT_ASSERT(ok);

        ProposeCommit(runtime, sender, txId, writeIds);
        PlanCommit(runtime, sender, planStep, txId);
    }

    // TODO: write some into index

    std::vector<TString> programs;
    THashSet<ui32> isFiltered;
    THashSet<ui32> checkResult;
    THashSet<NScheme::TTypeId> intTypes = {
        NTypeIds::Int8, NTypeIds::Int16, NTypeIds::Int32, NTypeIds::Int64,
        NTypeIds::Uint8, NTypeIds::Uint16, NTypeIds::Uint32, NTypeIds::Uint64,
        NTypeIds::Timestamp
    };
    THashSet<NScheme::TTypeId> strTypes = {
        NTypeIds::Utf8, NTypeIds::String
        //NTypeIds::Yson, NTypeIds::Json, NTypeIds::JsonDocument
    };

    ui32 prog = 0;
    for (ui32 i = 0; i < ydbSchema.size(); ++i, ++prog) {
        if (intTypes.contains(ydbSchema[i].second.GetTypeId()) ||
            strTypes.contains(ydbSchema[i].second.GetTypeId())) {
            checkResult.insert(prog);
        }

        NKikimrSSA::TProgram ssa = MakeSelectAggregates(i + 1, aggKeys, addProjection);
        TString serialized;
        UNIT_ASSERT(ssa.SerializeToString(&serialized));
        NKikimrSSA::TOlapProgram program;
        program.SetProgram(serialized);

        programs.push_back("");
        UNIT_ASSERT(program.SerializeToString(&programs.back()));
    }

    for (ui32 i = 0; i < ydbSchema.size(); ++i, ++prog) {
        isFiltered.insert(prog);
        if (intTypes.contains(ydbSchema[i].second.GetTypeId()) ||
            strTypes.contains(ydbSchema[i].second.GetTypeId())) {
            checkResult.insert(prog);
        }

        NKikimrSSA::TProgram ssa = MakeSelectAggregatesWithFilter(i + 1, 4, aggKeys, addProjection);
        TString serialized;
        UNIT_ASSERT(ssa.SerializeToString(&serialized));
        NKikimrSSA::TOlapProgram program;
        program.SetProgram(serialized);

        programs.push_back("");
        UNIT_ASSERT(program.SerializeToString(&programs.back()));
    }

    std::vector<TString> namedColumns = {"res_min", "res_max", "res_some", "res_count"};
    std::vector<TString> unnamedColumns = {"100", "101", "102", "103"};
    if (!addProjection) {
        for (auto& key : aggKeys) {
            namedColumns.push_back(ydbSchema[key].first);
            unnamedColumns.push_back(ydbSchema[key].first);
        }
    }

    prog = 0;
    for (auto& programText : programs) {
        Cerr << "-- select program: " << prog << " is filtered: " << (int)isFiltered.count(prog) << "\n";

        auto* readEvent = new TEvColumnShard::TEvRead(sender, metaShard, planStep, txId, tableId);
        auto& readProto = Proto(readEvent);

        readProto.SetOlapProgramType(::NKikimrSchemeOp::EOlapProgramType::OLAP_PROGRAM_SSA_PROGRAM_WITH_PARAMETERS);
        readProto.SetOlapProgram(programText);

        ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender, readEvent);

        TAutoPtr<IEventHandle> handle;
        auto result = runtime.GrabEdgeEvent<TEvColumnShard::TEvReadResult>(handle);
        UNIT_ASSERT(result);

        auto& resRead = Proto(result);
        UNIT_ASSERT_EQUAL(resRead.GetOrigin(), TTestTxConfig::TxTablet0);
        UNIT_ASSERT_EQUAL(resRead.GetTxInitiator(), metaShard);

        std::shared_ptr<arrow::RecordBatch> batch;
        {
            UNIT_ASSERT_EQUAL(resRead.GetStatus(), NKikimrTxColumnShard::EResultStatus::SUCCESS);
            UNIT_ASSERT_EQUAL(resRead.GetBatch(), 0);
            UNIT_ASSERT_EQUAL(resRead.GetFinished(), true);
            UNIT_ASSERT(resRead.GetData().size() > 0);

            auto& meta = resRead.GetMeta();
            auto& schema = meta.GetSchema();
            auto& data = resRead.GetData();

            batch = NArrow::DeserializeBatch(data, NArrow::DeserializeSchema(schema));
            UNIT_ASSERT(batch);
            UNIT_ASSERT(batch->ValidateFull().ok());
        }

        if (checkResult.contains(prog)) {
            if (isFiltered.contains(prog)) {
                UNIT_ASSERT(CheckColumns(batch, namedColumns, expectedFiltered.NumRows));
                if (aggKeys.empty()) { // TODO: ORDER BY for compare
                    UNIT_ASSERT(CheckIntValues(batch->GetColumnByName("res_min"), expectedFiltered.MinValues));
                    UNIT_ASSERT(CheckIntValues(batch->GetColumnByName("res_max"), expectedFiltered.MaxValues));
                    UNIT_ASSERT(CheckIntValues(batch->GetColumnByName("res_some"), expectedFiltered.MinValues));
                }
                UNIT_ASSERT(CheckIntValues(batch->GetColumnByName("res_count"), expectedFiltered.Counts));
            } else {
                UNIT_ASSERT(CheckColumns(batch, unnamedColumns, expectedResult.NumRows));
                if (aggKeys.empty()) { // TODO: ORDER BY for compare
                    UNIT_ASSERT(CheckIntValues(batch->GetColumnByName("100"), expectedResult.MinValues));
                    UNIT_ASSERT(CheckIntValues(batch->GetColumnByName("101"), expectedResult.MaxValues));
                    UNIT_ASSERT(CheckIntValues(batch->GetColumnByName("102"), expectedResult.MinValues));
                }
                UNIT_ASSERT(CheckIntValues(batch->GetColumnByName("103"), expectedResult.Counts));
            }
        }

        ++prog;
    }
}

}

Y_UNIT_TEST_SUITE(EvWrite) {

    Y_UNIT_TEST(WriteInTransaction) {
        using namespace NArrow;

        TTestBasicRuntime runtime;
        TTester::Setup(runtime);

        const ui64 tableId = 1;
        const std::vector<std::pair<TString, TTypeInfo>> schema = {
                                                                    {"key", TTypeInfo(NTypeIds::Uint64) },
                                                                    {"field", TTypeInfo(NTypeIds::Utf8) }
                                                                };
        PrepareTablet(runtime, tableId, schema);
        const ui64 txId = 111;

        NConstruction::IArrayBuilder::TPtr keyColumn = std::make_shared<NConstruction::TSimpleArrayConstructor<NConstruction::TIntSeqFiller<arrow::UInt64Type>>>("key");
        NConstruction::IArrayBuilder::TPtr column = std::make_shared<NConstruction::TSimpleArrayConstructor<NConstruction::TStringPoolFiller>>(
            "field", NConstruction::TStringPoolFiller(8, 100));

        auto batch = NConstruction::TRecordBatchConstructor({ keyColumn, column }).BuildBatch(2048);
        TString blobData = NArrow::SerializeBatchNoCompression(batch);
        UNIT_ASSERT(blobData.size() < TLimits::GetMaxBlobSize());

        auto evWrite = std::make_unique<NKikimr::NEvents::TDataEvents::TEvWrite>(txId);
        ui64 payloadIndex = NEvWrite::TPayloadHelper<NKikimr::NEvents::TDataEvents::TEvWrite>(*evWrite).AddDataToPayload(std::move(blobData));
        evWrite->AddOperation(NKikimrDataEvents::TEvWrite::TOperation::OPERATION_REPLACE, tableId, 1, schema, payloadIndex, NKikimrDataEvents::FORMAT_ARROW);

        TActorId sender = runtime.AllocateEdgeActor();
        ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender, evWrite.release());

        {
            TAutoPtr<NActors::IEventHandle> handle;
            auto event = runtime.GrabEdgeEvent<NKikimr::NEvents::TDataEvents::TEvWriteResult>(handle);
            UNIT_ASSERT(event);
            UNIT_ASSERT_VALUES_EQUAL((ui64)event->Record.GetStatus(), (ui64)NKikimrDataEvents::TEvWriteResult::STATUS_PREPARED);

            auto readResult = ReadAllAsBatch(runtime, tableId, NOlap::TSnapshot(10, txId), schema);
            UNIT_ASSERT_VALUES_EQUAL(readResult->num_rows(), 0);

            PlanWriteTx(runtime, sender, NOlap::TSnapshot(11, txId));
        }

        auto readResult = ReadAllAsBatch(runtime, tableId, NOlap::TSnapshot(11, txId), schema);
        UNIT_ASSERT_VALUES_EQUAL(readResult->num_rows(), 2048);
    }

    Y_UNIT_TEST(AbortInTransaction) {
        using namespace NArrow;

        TTestBasicRuntime runtime;
        TTester::Setup(runtime);

        const ui64 tableId = 1;
        const std::vector<std::pair<TString, TTypeInfo>> schema = {
                                                                    {"key", TTypeInfo(NTypeIds::Uint64) },
                                                                    {"field", TTypeInfo(NTypeIds::Utf8) }
                                                                };
        PrepareTablet(runtime, tableId, schema);
        const ui64 txId = 111;

        NConstruction::IArrayBuilder::TPtr keyColumn = std::make_shared<NConstruction::TSimpleArrayConstructor<NConstruction::TIntSeqFiller<arrow::UInt64Type>>>("key");
        NConstruction::IArrayBuilder::TPtr column = std::make_shared<NConstruction::TSimpleArrayConstructor<NConstruction::TStringPoolFiller>>(
            "field", NConstruction::TStringPoolFiller(8, 100));

        auto batch = NConstruction::TRecordBatchConstructor({ keyColumn, column }).BuildBatch(2048);
        TString blobData = NArrow::SerializeBatchNoCompression(batch);
        UNIT_ASSERT(blobData.size() < TLimits::GetMaxBlobSize());

        auto evWrite = std::make_unique<NKikimr::NEvents::TDataEvents::TEvWrite>(txId);
        ui64 payloadIndex = NEvWrite::TPayloadHelper<NKikimr::NEvents::TDataEvents::TEvWrite>(*evWrite).AddDataToPayload(std::move(blobData));
        evWrite->AddOperation(NKikimrDataEvents::TEvWrite::TOperation::OPERATION_REPLACE, tableId, 1, schema, payloadIndex, NKikimrDataEvents::FORMAT_ARROW);

        TActorId sender = runtime.AllocateEdgeActor();
        ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender, evWrite.release());

        ui64 outdatedStep = 11;
        {
            TAutoPtr<NActors::IEventHandle> handle;
            auto event = runtime.GrabEdgeEvent<NKikimr::NEvents::TDataEvents::TEvWriteResult>(handle);
            UNIT_ASSERT(event);
            UNIT_ASSERT_VALUES_EQUAL((ui64)event->Record.GetStatus(), (ui64)NKikimrDataEvents::TEvWriteResult::STATUS_PREPARED);

            outdatedStep = event->Record.GetMaxStep() + 1;
            PlanWriteTx(runtime, sender, NOlap::TSnapshot(outdatedStep, txId + 1), false);
        }

        auto readResult = ReadAllAsBatch(runtime, tableId, NOlap::TSnapshot(outdatedStep, txId), schema);
        UNIT_ASSERT_VALUES_EQUAL(readResult->num_rows(), 0);
    }

    Y_UNIT_TEST(WriteWithSplit) {
        using namespace NArrow;

        TTestBasicRuntime runtime;
        TTester::Setup(runtime);

        const ui64 tableId = 1;
        const std::vector<std::pair<TString, TTypeInfo>> schema = {
                                                                    {"key", TTypeInfo(NTypeIds::Uint64) },
                                                                    {"field", TTypeInfo(NTypeIds::Utf8) }
                                                                };
        PrepareTablet(runtime, tableId, schema);
        const ui64 txId = 111;

        NConstruction::IArrayBuilder::TPtr keyColumn = std::make_shared<NConstruction::TSimpleArrayConstructor<NConstruction::TIntSeqFiller<arrow::UInt64Type>>>("key");
        NConstruction::IArrayBuilder::TPtr column = std::make_shared<NConstruction::TSimpleArrayConstructor<NConstruction::TStringPoolFiller>>(
            "field", NConstruction::TStringPoolFiller(8, TLimits::GetMaxBlobSize() / 1024));

        auto batch = NConstruction::TRecordBatchConstructor({ keyColumn, column }).BuildBatch(2048);
        TString blobData = NArrow::SerializeBatchNoCompression(batch);
        UNIT_ASSERT(blobData.size() > TLimits::GetMaxBlobSize());

        auto evWrite = std::make_unique<NKikimr::NEvents::TDataEvents::TEvWrite>(txId);
        ui64 payloadIndex = NEvWrite::TPayloadHelper<NKikimr::NEvents::TDataEvents::TEvWrite>(*evWrite).AddDataToPayload(std::move(blobData));
        evWrite->AddOperation(NKikimrDataEvents::TEvWrite::TOperation::OPERATION_REPLACE, tableId, 1, schema, payloadIndex, NKikimrDataEvents::FORMAT_ARROW);

        TActorId sender = runtime.AllocateEdgeActor();
        ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender, evWrite.release());

        {
            TAutoPtr<NActors::IEventHandle> handle;
            auto event = runtime.GrabEdgeEvent<NKikimr::NEvents::TDataEvents::TEvWriteResult>(handle);
            UNIT_ASSERT(event);
            UNIT_ASSERT_VALUES_EQUAL((ui64)event->Record.GetStatus(), (ui64)NKikimrDataEvents::TEvWriteResult::STATUS_PREPARED);
            PlanWriteTx(runtime, sender, NOlap::TSnapshot(11, txId));
        }

        auto readResult = ReadAllAsBatch(runtime, tableId, NOlap::TSnapshot(11, txId), schema);
        UNIT_ASSERT_VALUES_EQUAL(readResult->num_rows(), 2048);
    }
}

Y_UNIT_TEST_SUITE(TColumnShardTestReadWrite) {
    Y_UNIT_TEST(Write) {
        TestTableDescription table;
        TestWrite(table);
    }

    Y_UNIT_TEST(WriteStandalone) {
        TestTableDescription table;
        table.InStore = false;
        TestWrite(table);
    }

    Y_UNIT_TEST(WriteExoticTypes) {
        TestTableDescription table;
        table.Schema = TTestSchema::YdbExoticSchema();
        TestWrite(table);
    }

    Y_UNIT_TEST(WriteStandaloneExoticTypes) {
        TestTableDescription table;
        table.Schema = TTestSchema::YdbExoticSchema();
        table.InStore = false;
        TestWrite(table);
    }

    Y_UNIT_TEST(WriteOverload) {
        TestTableDescription table;
        TestWriteOverload(table);
    }

    Y_UNIT_TEST(WriteStandaloneOverload) {
        TestTableDescription table;
        table.InStore = false;
        TestWriteOverload(table);
    }

    Y_UNIT_TEST(WriteReadDuplicate) {
        TestWriteReadDup();
        TestWriteReadLongTxDup();
    }

    Y_UNIT_TEST(WriteRead) {
        TestTableDescription table;
        TestWriteRead(false, table);
    }

    Y_UNIT_TEST(WriteReadStandalone) {
        TestTableDescription table;
        table.InStore = false;
        TestWriteRead(false, table);
    }

    Y_UNIT_TEST(WriteReadExoticTypes) {
        TestTableDescription table;
        table.Schema = TTestSchema::YdbExoticSchema();
        TestWriteRead(false, table);
    }

    Y_UNIT_TEST(WriteReadStandaloneExoticTypes) {
        TestTableDescription table;
        table.Schema = TTestSchema::YdbExoticSchema();
        table.InStore = false;
        TestWriteRead(false, table);
    }

    Y_UNIT_TEST(RebootWriteRead) {
        TestWriteRead(true);
    }

    Y_UNIT_TEST(RebootWriteReadStandalone) {
        TestTableDescription table;
        table.InStore = false;
        TestWriteRead(true, table);
    }

    Y_UNIT_TEST(WriteReadNoCompression) {
        TestWriteRead(true, {}, "none");
    }

    Y_UNIT_TEST(WriteReadZSTD) {
        TestWriteRead(true, {}, "zstd");
    }

    void TestCompactionInGranule(const bool reboot, const NScheme::TTypeId typeId) {
        auto schema = TTestSchema::YdbSchema();
        auto pk = TTestSchema::YdbPkSchema();

        schema[0].second = TTypeInfo(typeId);
        pk[0].second = TTypeInfo(typeId);
        TestTableDescription table{.Schema = schema, .Pk = pk};
        TestCompactionInGranuleImpl(reboot, table);
    }

    Y_UNIT_TEST(CompactionInGranule_PKString) {
        TestCompactionInGranule(false, NTypeIds::String);
    }

    Y_UNIT_TEST(CompactionInGranule_PKUtf8) {
        TestCompactionInGranule(false, NTypeIds::Utf8);
    }

    Y_UNIT_TEST(CompactionInGranule_PKTimestamp) {
        TestCompactionInGranule(false, NTypeIds::Timestamp);
    }

    Y_UNIT_TEST(CompactionInGranule_PKInt32) {
        TestCompactionInGranule(false, NTypeIds::Int32);
    }

    Y_UNIT_TEST(CompactionInGranule_PKUInt32) {
        TestCompactionInGranule(false, NTypeIds::Uint32);
    }

    Y_UNIT_TEST(CompactionInGranule_PKInt64) {
        TestCompactionInGranule(false, NTypeIds::Int64);
    }

    Y_UNIT_TEST(CompactionInGranule_PKUInt64) {
        TestCompactionInGranule(false, NTypeIds::Uint64);
    }

    Y_UNIT_TEST(CompactionInGranule_PKDatetime) {
        TestCompactionInGranule(false, NTypeIds::Datetime);
    }


    Y_UNIT_TEST(CompactionInGranule_PKString_Reboot) {
        TestCompactionInGranule(true, NTypeIds::String);
    }

    Y_UNIT_TEST(CompactionInGranule_PKUtf8_Reboot) {
        TestCompactionInGranule(true, NTypeIds::Utf8);
    }

    Y_UNIT_TEST(CompactionInGranule_PKTimestamp_Reboot) {
        TestCompactionInGranule(true, NTypeIds::Timestamp);
    }

    Y_UNIT_TEST(CompactionInGranule_PKInt32_Reboot) {
        TestCompactionInGranule(true, NTypeIds::Int32);
    }

    Y_UNIT_TEST(CompactionInGranule_PKUInt32_Reboot) {
        TestCompactionInGranule(true, NTypeIds::Uint32);
    }

    Y_UNIT_TEST(CompactionInGranule_PKInt64_Reboot) {
        TestCompactionInGranule(true, NTypeIds::Int64);
    }

    Y_UNIT_TEST(CompactionInGranule_PKUInt64_Reboot) {
        TestCompactionInGranule(true, NTypeIds::Uint64);
    }

    Y_UNIT_TEST(CompactionInGranule_PKDatetime_Reboot) {
        TestCompactionInGranule(true, NTypeIds::Datetime);
    }

    Y_UNIT_TEST(ReadWithProgram) {
        TestReadWithProgram();
    }

    Y_UNIT_TEST(ReadWithProgramLike) {
        TestReadWithProgramLike();
    }

    Y_UNIT_TEST(ReadSomePrograms) {
        TestTableDescription table;
        table.Schema = {
            {"timestamp", TTypeInfo(NTypeIds::Timestamp) },
            {"resource_id", TTypeInfo(NTypeIds::Utf8) },
            {"uid", TTypeInfo(NTypeIds::Utf8) },
            {"level", TTypeInfo(NTypeIds::Int32) },
            {"message", TTypeInfo(NTypeIds::Utf8) }
        };
        table.Pk = {
            {"timestamp", TTypeInfo(NTypeIds::Timestamp) }
        };

        TestSomePrograms(table);
    }

    Y_UNIT_TEST(ReadAggregate) {
        auto schema = TTestSchema::YdbAllTypesSchema();
        auto testBlob = MakeTestBlob({ 0, 100 }, schema);

        TestReadAggregate(schema, testBlob, false);
        TestReadAggregate(schema, testBlob, true);
    }

    Y_UNIT_TEST(ReadGroupBy) {
        auto schema = TTestSchema::YdbAllTypesSchema();
        auto testBlob = MakeTestBlob({ 0, 100 }, schema);

        std::vector<int64_t> counts;
        counts.reserve(100);
        for (int i = 0; i < 100; ++i) {
            counts.push_back(1);
        }

        THashSet<NScheme::TTypeId> sameValTypes = {
            NTypeIds::Yson, NTypeIds::Json, NTypeIds::JsonDocument
        };

        // TODO: query needs normalization to compare with expected
        TReadAggregateResult resDefault = { 100, {}, {}, counts };
        TReadAggregateResult resFiltered = { 1, {}, {}, {1} };
        TReadAggregateResult resGrouped = { 1, {}, {}, {100} };

        for (ui32 key = 0; key < schema.size(); ++key) {
            Cerr << "-- group by key: " << key << "\n";

            // the type has the same values in test batch so result would be grouped in one row
            if (sameValTypes.contains(schema[key].second.GetTypeId())) {
                TestReadAggregate(schema, testBlob, (key % 2), { key }, resGrouped, resFiltered);
            } else {
                TestReadAggregate(schema, testBlob, (key % 2), { key }, resDefault, resFiltered);
            }
        }
        for (ui32 key = 0; key < schema.size() - 1; ++key) {
            Cerr << "-- group by key: " << key << ", " << key + 1 << "\n";
            if (sameValTypes.contains(schema[key].second.GetTypeId()) &&
                sameValTypes.contains(schema[key + 1].second.GetTypeId())) {
                TestReadAggregate(schema, testBlob, (key % 2), { key, key + 1 }, resGrouped, resFiltered);
            } else {
                TestReadAggregate(schema, testBlob, (key % 2), { key, key + 1 }, resDefault, resFiltered);
            }
        }
        for (ui32 key = 0; key < schema.size() - 2; ++key) {
            Cerr << "-- group by key: " << key << ", " << key + 1 << ", " << key + 2 << "\n";
            if (sameValTypes.contains(schema[key].second.GetTypeId()) &&
                sameValTypes.contains(schema[key + 1].second.GetTypeId()) &&
                sameValTypes.contains(schema[key + 1].second.GetTypeId())) {
                TestReadAggregate(schema, testBlob, (key % 2), { key, key + 1, key + 2 }, resGrouped, resFiltered);
            } else {
                TestReadAggregate(schema, testBlob, (key % 2), { key, key + 1, key + 2 }, resDefault, resFiltered);
            }
        }
    }

    class TTabletReadPredicateTest {
    private:
        TTestBasicRuntime& Runtime;
        const ui64 PlanStep;
        const ui64 TxId;
        const std::vector<std::pair<TString, TTypeInfo>> YdbPk;

    public:
        TTabletReadPredicateTest(TTestBasicRuntime& runtime, const ui64 planStep, const ui64 txId, const std::vector<std::pair<TString, TTypeInfo>>& ydbPk)
            : Runtime(runtime)
            , PlanStep(planStep)
            , TxId(txId)
            , YdbPk(ydbPk)
        {}

        class TBorder {
        private:
            std::vector<ui32> Border;
            bool Include;

        public:
            TBorder(const std::vector<ui32>& values, const bool include = false)
                : Border(values)
                , Include(include)
            {}

            bool GetInclude() const noexcept { return Include; }

            std::vector<TCell> GetCellVec(const std::vector<std::pair<TString, TTypeInfo>>& pk,
                                        std::vector<TString>& mem, bool trailingNulls = false) const
            {
                UNIT_ASSERT(Border.size() <= pk.size());
                std::vector<TCell> cells;
                size_t i = 0;
                for (; i < Border.size(); ++i) {
                    cells.push_back(MakeTestCell(pk[i].second, Border[i], mem));
                }
                for (; trailingNulls && i < pk.size(); ++i) {
                    cells.push_back(TCell());
                }
                return cells;
            }
        };

        struct TTestCaseOptions {
            std::optional<TBorder> From;
            std::optional<TBorder> To;
            std::optional<ui32> ExpectedCount;

            TTestCaseOptions() = default;

            TTestCaseOptions& SetFrom(const TBorder& border) { From = border; return *this; }
            TTestCaseOptions& SetTo(const TBorder& border) { To = border; return *this; }
            TTestCaseOptions& SetExpectedCount(ui32 count) { ExpectedCount = count; return *this; }

            TSerializedTableRange MakeRange(const std::vector<std::pair<TString, TTypeInfo>>& pk) const {
                std::vector<TString> mem;
                auto cellsFrom = From ? From->GetCellVec(pk, mem, true) : std::vector<TCell>();
                auto cellsTo = To ? To->GetCellVec(pk, mem) : std::vector<TCell>();
                return TSerializedTableRange(TConstArrayRef<TCell>(cellsFrom), (From ? From->GetInclude() : false),
                                             TConstArrayRef<TCell>(cellsTo), (To ? To->GetInclude(): false));
            }
        };

        class TTestCase: public TTestCaseOptions, TNonCopyable {
        private:
            const TTabletReadPredicateTest& Owner;
            const TString TestCaseName;

            void Execute() {
                const ui64 metaShard = TTestTxConfig::TxTablet1;
                const ui64 tableId = 1;
                const TActorId sender = Owner.Runtime.AllocateEdgeActor();
                std::set<TString> useFields = {"timestamp", "message"};
                { // read with predicate (FROM)
                    auto read = std::make_unique<TEvColumnShard::TEvRead>(sender, metaShard, Owner.PlanStep, Owner.TxId, tableId);
                    Proto(read.get()).AddColumnNames("timestamp");
                    Proto(read.get()).AddColumnNames("message");

                    const TSerializedTableRange range = MakeRange(Owner.YdbPk);

                    NOlap::TPredicate prGreater, prLess;
                    std::tie(prGreater, prLess) = RangePredicates(range, Owner.YdbPk);
                    if (From) {
                        auto* greater = Proto(read.get()).MutableGreaterPredicate();
                        for (auto& name : prGreater.ColumnNames()) {
                            greater->AddColumnNames(name);
                            useFields.emplace(name);
                        }
                        greater->SetRow(NArrow::SerializeBatchNoCompression(prGreater.Batch));
                        greater->SetInclusive(From->GetInclude());
                    };
                    if (To) {
                        auto* less = Proto(read.get()).MutableLessPredicate();
                        for (auto& name : prLess.ColumnNames()) {
                            less->AddColumnNames(name);
                            useFields.emplace(name);
                        }
                        less->SetRow(NArrow::SerializeBatchNoCompression(prLess.Batch));
                        less->SetInclusive(To->GetInclude());
                    }

                    ForwardToTablet(Owner.Runtime, TTestTxConfig::TxTablet0, sender, read.release());
                }

                ui32 numRows = 0;
                std::vector<std::shared_ptr<arrow::RecordBatch>> batches;
                ui32 expected = 100;
                for (ui32 i = 0; i < expected; ++i) {
                    TAutoPtr<IEventHandle> handle;
                    auto event = Owner.Runtime.GrabEdgeEvent<TEvColumnShard::TEvReadResult>(handle);
                    UNIT_ASSERT(event);

                    auto& resRead = Proto(event);
                    AFL_INFO(NKikimrServices::TX_COLUMNSHARD)("0_type_id", Owner.YdbPk[0].second.GetTypeId())("batch", resRead.GetBatch())("data_size", resRead.GetData().size());

                    UNIT_ASSERT_EQUAL(resRead.GetOrigin(), TTestTxConfig::TxTablet0);
                    UNIT_ASSERT_EQUAL(resRead.GetTxInitiator(), metaShard);
                    UNIT_ASSERT_EQUAL(resRead.GetStatus(), NKikimrTxColumnShard::EResultStatus::SUCCESS);
                    if (ExpectedCount && !*ExpectedCount) {
                        UNIT_ASSERT(!resRead.GetBatch());
                        UNIT_ASSERT(resRead.GetFinished());
                        UNIT_ASSERT(!resRead.GetData().size());
                    } else {
                        UNIT_ASSERT(resRead.HasMeta());

                        auto& meta = resRead.GetMeta();
                        auto schema = NArrow::DeserializeSchema(meta.GetSchema());
                        UNIT_ASSERT(schema);
                        auto batch = NArrow::DeserializeBatch(resRead.GetData(), schema);
                        UNIT_ASSERT(batch);

                        numRows += batch->num_rows();
                        batches.emplace_back(batch);

                        if (resRead.GetFinished()) {
                            UNIT_ASSERT(meta.HasReadStats());
                            auto& readStats = meta.GetReadStats();

                            UNIT_ASSERT(readStats.GetBeginTimestamp());
                            UNIT_ASSERT(readStats.GetDurationUsec());
                            UNIT_ASSERT_VALUES_EQUAL(readStats.GetSelectedIndex(), 0);
                            UNIT_ASSERT(readStats.GetIndexBatches());
                            //UNIT_ASSERT_VALUES_EQUAL(readStats.GetNotIndexedBatches(), 0); // TODO
                            UNIT_ASSERT_VALUES_EQUAL(readStats.GetSchemaColumns(), useFields.size()); // planStep, txId + 4 PK columns + "message"
                            //UNIT_ASSERT_VALUES_EQUAL(readStats.GetIndexGranules(), 1);
                            //UNIT_ASSERT_VALUES_EQUAL(readStats.GetIndexPortions(), 0); // TODO: min-max index optimization?
                        }
                    }
                    if (resRead.GetFinished()) {
                        expected = resRead.GetBatch();
                    }
                }
                UNIT_ASSERT(expected < 100);

                if (ExpectedCount) {
                    if (numRows != *ExpectedCount) {
                        for (auto& batch : batches) {
                            Cerr << batch->ToString() << "\n";
                        }
                    }
                    UNIT_ASSERT_VALUES_EQUAL(numRows, *ExpectedCount);
                }
            }

        public:
            TTestCase(TTabletReadPredicateTest& owner, const TString& testCaseName, const TTestCaseOptions& opts = {})
                : TTestCaseOptions(opts)
                , Owner(owner)
                , TestCaseName(testCaseName)
            {
                Cerr << "TEST CASE " << TestCaseName << " START..." << Endl;
            }

            ~TTestCase() {
                try {
                    Execute();
                    Cerr << "TEST CASE " << TestCaseName << " FINISHED" << Endl;
                } catch (...) {
                    Cerr << "TEST CASE " << TestCaseName << " FAILED" << Endl;
                    throw;
                }
            }
        };

        TTestCase Test(const TString& testCaseName, const TTestCaseOptions& options = {}) {
            return TTestCase(*this, testCaseName, options);
        }
    };

    void TestCompactionSplitGranuleImpl(const TestTableDescription& table, const TTestBlobOptions& testBlobOptions = {}) {
        TTestBasicRuntime runtime;
        TTester::Setup(runtime);
        auto csDefaultControllerGuard = NKikimr::NYDBTest::TControllers::RegisterCSControllerGuard<TDefaultTestsController>();

        TActorId sender = runtime.AllocateEdgeActor();
        CreateTestBootstrapper(runtime, CreateTestTabletInfo(TTestTxConfig::TxTablet0, TTabletTypes::ColumnShard), &CreateColumnShard);

        TDispatchOptions options;
        options.FinalEvents.push_back(TDispatchOptions::TFinalEventCondition(TEvTablet::EvBoot));
        runtime.DispatchEvents(options);

        const ui64 metaShard = TTestTxConfig::TxTablet1;
        const ui64 tableId = 1;
        ui64 planStep = 100;
        ui64 txId = 100;

        SetupSchema(runtime, sender, tableId, table, "lz4");
        TAutoPtr<IEventHandle> handle;

        bool isStrPk0 = table.Pk[0].second == TTypeInfo(NTypeIds::String) || table.Pk[0].second == TTypeInfo(NTypeIds::Utf8);

        // Write different keys: grow on compaction

        static const ui32 triggerPortionSize = 85 * 1000;
        static const ui32 overlapSize = 5 * 1000;

        const ui32 numWrites = 23;
        {
            ui64 writeId = 0;
            for (ui32 i = 0; i < numWrites; ++i, ++writeId, ++planStep, ++txId) {
                ui64 start = i * (triggerPortionSize - overlapSize);
                std::pair<ui64, ui64> triggerPortion = { start, start + triggerPortionSize };
                TString triggerData = MakeTestBlob(triggerPortion, table.Schema, testBlobOptions);
                UNIT_ASSERT(triggerData.size() > NColumnShard::TLimits::MIN_BYTES_TO_INSERT);
                UNIT_ASSERT(triggerData.size() < NColumnShard::TLimits::GetMaxBlobSize());

                std::vector<ui64> writeIds;
                UNIT_ASSERT(WriteData(runtime, sender, writeId, tableId, triggerData, table.Schema, true, &writeIds));

                ProposeCommit(runtime, sender, txId, writeIds);
                PlanCommit(runtime, sender, planStep, txId);
            }
        }

        // TODO: Move tablet's time to the future with mediator timecast instead
        --planStep;
        --txId;

        const ui32 fullNumRows = numWrites * (triggerPortionSize - overlapSize) + overlapSize;

        for (ui32 i = 0; i < 2; ++i) {
            {
                auto read = std::make_unique<TEvColumnShard::TEvRead>(sender, metaShard, planStep, txId, tableId);
                Proto(read.get()).AddColumnNames("timestamp");
                Proto(read.get()).AddColumnNames("message");

                ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender, read.release());
            }

            {
                TString schema;
                NKikimrTxColumnShard::TMetadata meta;
                std::vector<TString> readData = ReadManyResults(runtime, schema, meta, 20000);
                {
                    schema = meta.GetSchema();

                    UNIT_ASSERT(meta.HasReadStats());
                    auto& readStats = meta.GetReadStats();

                    UNIT_ASSERT(readStats.GetBeginTimestamp() > 0);
                    UNIT_ASSERT(readStats.GetDurationUsec() > 0);
                    UNIT_ASSERT_VALUES_EQUAL(readStats.GetSelectedIndex(), 0);
                    UNIT_ASSERT(readStats.GetIndexBatches() > 0);
                    //UNIT_ASSERT_VALUES_EQUAL(readStats.GetNotIndexedBatches(), 0); // TODO
                    UNIT_ASSERT_VALUES_EQUAL(readStats.GetSchemaColumns(), 2); // planStep, txId + 4 PK columns + "message"
//                    UNIT_ASSERT_VALUES_EQUAL(readStats.GetIndexGranules(), 3); // got 2 split compactions
                    //UNIT_ASSERT_VALUES_EQUAL(readStats.GetIndexPortions(), x);
                }

                if (testBlobOptions.SameValueColumns.contains("timestamp")) {
                    UNIT_ASSERT(!testBlobOptions.SameValueColumns.contains("message"));
                    UNIT_ASSERT(DataHas<std::string>(readData, schema, { 0, fullNumRows}, true, "message"));
                } else {
                    UNIT_ASSERT(isStrPk0
                        ? DataHas<std::string>(readData, schema, { 0, fullNumRows}, true, "timestamp")
                        : DataHas(readData, schema, { 0, fullNumRows}, true, "timestamp"));
                }
            }

            std::vector<ui32> val0 = { 0 };
            std::vector<ui32> val1 = { 1 };
            std::vector<ui32> val9990 = { 99990 };
            std::vector<ui32> val9999 = { 99999 };
            std::vector<ui32> val1M = { 1000000000 };
            std::vector<ui32> val1M_1 = { 1000000001 };
            std::vector<ui32> valNumRows = {fullNumRows};
            std::vector<ui32> valNumRows_1 = {fullNumRows - 1 };
            std::vector<ui32> valNumRows_2 = {fullNumRows - 2 };

            {
                UNIT_ASSERT(table.Pk.size() >= 2);

                ui32 sameValue = testBlobOptions.SameValue;
                val0 = { sameValue, 0 };
                val1 = { sameValue, 1 };
                val9990 = { sameValue, 99990 };
                val9999 = { sameValue, 99999 };
                val1M = { sameValue, 1000000000 };
                val1M_1 = { sameValue, 1000000001 };
                valNumRows = { sameValue, fullNumRows};
                valNumRows_1 = { sameValue, fullNumRows - 1 };
                valNumRows_2 = { sameValue, fullNumRows - 2 };
            }

            using TBorder = TTabletReadPredicateTest::TBorder;

            TTabletReadPredicateTest testAgent(runtime, planStep, txId, table.Pk);
            testAgent.Test(":1)").SetTo(TBorder(val1, false)).SetExpectedCount(1);
            testAgent.Test(":1]").SetTo(TBorder(val1, true)).SetExpectedCount(2);
            testAgent.Test(":0)").SetTo(TBorder(val0, false)).SetExpectedCount(0);
            testAgent.Test(":0]").SetTo(TBorder(val0, true)).SetExpectedCount(1);

            testAgent.Test("[0:0]").SetFrom(TBorder(val0, true)).SetTo(TBorder(val0, true)).SetExpectedCount(1);
            testAgent.Test("[0:1)").SetFrom(TBorder(val0, true)).SetTo(TBorder(val1, false)).SetExpectedCount(1);
            testAgent.Test("(0:1)").SetFrom(TBorder(val0, false)).SetTo(TBorder(val1, false)).SetExpectedCount(0);
            testAgent.Test("outscope1").SetFrom(TBorder(val1M, true)).SetTo(TBorder(val1M_1, true)).SetExpectedCount(0);
//            VERIFIED AS INCORRECT INTERVAL (its good)
//            testAgent.Test("[0-0)").SetFrom(TTabletReadPredicateTest::TBorder(0, true)).SetTo(TBorder(0, false)).SetExpectedCount(0);

            if (isStrPk0) {
                testAgent.Test("(99990:").SetFrom(TBorder(val9990, false)).SetExpectedCount(109);
                testAgent.Test("(99990:99999)").SetFrom(TBorder(val9990, false)).SetTo(TBorder(val9999, false)).SetExpectedCount(98);
                testAgent.Test("(99990:99999]").SetFrom(TBorder(val9990, false)).SetTo(TBorder(val9999, true)).SetExpectedCount(99);
                testAgent.Test("[99990:99999]").SetFrom(TBorder(val9990, true)).SetTo(TBorder(val9999, true)).SetExpectedCount(100);
            } else {
                testAgent.Test("(numRows:").SetFrom(TBorder(valNumRows, false)).SetExpectedCount(0);
                testAgent.Test("(numRows-1:").SetFrom(TBorder(valNumRows_1, false)).SetExpectedCount(0);
                testAgent.Test("(numRows-2:").SetFrom(TBorder(valNumRows_2, false)).SetExpectedCount(1);
                testAgent.Test("[numRows-1:").SetFrom(TBorder(valNumRows_1, true)).SetExpectedCount(1);
            }

            RebootTablet(runtime, TTestTxConfig::TxTablet0, sender);
        }
        { // Get index stats
            ScanIndexStats(runtime, sender, {tableId, 42}, NOlap::TSnapshot(planStep, txId), 0);
            auto scanInited = runtime.GrabEdgeEvent<NKqp::TEvKqpCompute::TEvScanInitActor>(handle);
            auto& msg = scanInited->Record;
            auto scanActorId = ActorIdFromProto(msg.GetScanActorId());

            ui32 resultLimit = 1024 * 1024;
            runtime.Send(new IEventHandle(scanActorId, sender, new NKqp::TEvKqpCompute::TEvScanDataAck(resultLimit, 0, 1)));
            auto scan = runtime.GrabEdgeEvent<NKqp::TEvKqpCompute::TEvScanData>(handle);
            auto batchStats = scan->ArrowBatch;
            UNIT_ASSERT(batchStats);
            // Cerr << batchStats->ToString() << Endl;
            UNIT_ASSERT_VALUES_EQUAL(batchStats->num_rows(), 5);

            ui64 sumCompactedBytes = 0;
            ui64 sumCompactedRows = 0;
            ui64 sumInsertedBytes = 0;
            ui64 sumInsertedRows = 0;
            for (ui32 i = 0; i < batchStats->num_rows(); ++i) {
                auto paths = batchStats->GetColumnByName("PathId");
                auto kinds = batchStats->GetColumnByName("Kind");
                auto rows = batchStats->GetColumnByName("Rows");
                auto bytes = batchStats->GetColumnByName("Bytes");
                auto rawBytes = batchStats->GetColumnByName("RawBytes");

                ui64 pathId = static_cast<arrow::UInt64Array&>(*paths).Value(i);
                ui32 kind = static_cast<arrow::UInt32Array&>(*kinds).Value(i);
                ui64 numRows = static_cast<arrow::UInt64Array&>(*rows).Value(i);
                ui64 numBytes = static_cast<arrow::UInt64Array&>(*bytes).Value(i);
                ui64 numRawBytes = static_cast<arrow::UInt64Array&>(*rawBytes).Value(i);

                Cerr << "[" << __LINE__ << "] " << table.Pk[0].second.GetTypeId() << " "
                    << pathId << " " << kind << " " << numRows << " " << numBytes << " " << numRawBytes << "\n";

                if (pathId == tableId) {
                    if (kind == (ui32)NOlap::NPortion::EProduced::COMPACTED || kind == (ui32)NOlap::NPortion::EProduced::SPLIT_COMPACTED) {
                        sumCompactedBytes += numBytes;
                        sumCompactedRows += numRows;
                        //UNIT_ASSERT(numRawBytes > numBytes);
                    }
                    if (kind == (ui32)NOlap::NPortion::EProduced::INSERTED) {
                        sumInsertedBytes += numBytes;
                        sumInsertedRows += numRows;
                        //UNIT_ASSERT(numRawBytes > numBytes);
                    }
                } else {
                    UNIT_ASSERT_VALUES_EQUAL(numRows, 0);
                    UNIT_ASSERT_VALUES_EQUAL(numBytes, 0);
                    UNIT_ASSERT_VALUES_EQUAL(numRawBytes, 0);
                }
            }
            Cerr << "compacted=" << sumCompactedRows << ";inserted=" << sumInsertedRows << ";expected=" << fullNumRows << ";" << Endl;
            RebootTablet(runtime, TTestTxConfig::TxTablet0, sender);
            UNIT_ASSERT(sumCompactedRows == fullNumRows);
            UNIT_ASSERT(sumCompactedRows < sumCompactedBytes);
            UNIT_ASSERT(sumInsertedRows == 0);
            UNIT_ASSERT(sumInsertedBytes == 0);
        }
    }

    void TestCompactionSplitGranule(const TTypeId typeId) {
        auto schema = TTestSchema::YdbSchema();
        auto pk = TTestSchema::YdbPkSchema();
        TTestBlobOptions opts;
        opts.SameValueColumns.emplace(pk[0].first);

        schema[0].second = TTypeInfo(typeId);
        pk[0].second = TTypeInfo(typeId);
        schema[1].second = TTypeInfo(typeId);
        pk[1].second = TTypeInfo(typeId);
        TestTableDescription table{.Schema = schema, .Pk = pk};
        TestCompactionSplitGranuleImpl(table, opts);
    }

    Y_UNIT_TEST(CompactionSplitGranule_PKTimestamp) {
        TestCompactionSplitGranule(NTypeIds::Timestamp);
    }

    Y_UNIT_TEST(CompactionSplitGranule_PKInt32) {
        TestCompactionSplitGranule(NTypeIds::Int32);
    }

    Y_UNIT_TEST(CompactionSplitGranule_PKUInt32) {
        TestCompactionSplitGranule(NTypeIds::Uint32);
    }

    Y_UNIT_TEST(CompactionSplitGranule_PKInt64) {
        TestCompactionSplitGranule(NTypeIds::Int64);
    }

    Y_UNIT_TEST(CompactionSplitGranule_PKUInt64) {
        TestCompactionSplitGranule(NTypeIds::Uint64);
    }

    Y_UNIT_TEST(CompactionSplitGranule_PKDatetime) {
        TestCompactionSplitGranule(NTypeIds::Datetime);
    }

    Y_UNIT_TEST(CompactionSplitGranuleStrKey_PKString) {
        TestCompactionSplitGranule(NTypeIds::String);
    }

    Y_UNIT_TEST(CompactionSplitGranuleStrKey_PKUtf8) {
        TestCompactionSplitGranule(NTypeIds::Utf8);
    }

    Y_UNIT_TEST(ReadStale) {
        TTestBasicRuntime runtime;
        TTester::Setup(runtime);

        TActorId sender = runtime.AllocateEdgeActor();
        CreateTestBootstrapper(runtime, CreateTestTabletInfo(TTestTxConfig::TxTablet0, TTabletTypes::ColumnShard), &CreateColumnShard);

        TDispatchOptions options;
        options.FinalEvents.push_back(TDispatchOptions::TFinalEventCondition(TEvTablet::EvBoot));
        runtime.DispatchEvents(options);

        ui64 metaShard = TTestTxConfig::TxTablet1;
        ui64 writeId = 0;
        ui64 tableId = 1;
        ui64 planStep = 1000000;
        ui64 txId = 100;

        auto ydbSchema = TTestSchema::YdbSchema();
        SetupSchema(runtime, sender, tableId);
        TAutoPtr<IEventHandle> handle;

        // Write some test data to adavnce the time
        {
            std::pair<ui64, ui64> triggerPortion = {1, 1000};
            TString triggerData = MakeTestBlob(triggerPortion, ydbSchema);

            std::vector<ui64> writeIds;
            UNIT_ASSERT(WriteData(runtime, sender, writeId, tableId, triggerData, ydbSchema, true, &writeIds));

            ProposeCommit(runtime, sender, txId, writeIds);
            PlanCommit(runtime, sender, planStep, txId);
        }

        TDuration staleness = TDuration::Minutes(6);

        // Try to read snapshot that is too old
        {
            {
                auto request = std::make_unique<TEvColumnShard::TEvRead>(sender, metaShard, planStep - staleness.MilliSeconds(), Max<ui64>(), tableId);
                request->Record.AddColumnNames("timestamp");
                request->Record.AddColumnNames("message");

                ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender, request.release());
            }

            auto event = runtime.GrabEdgeEvent<TEvColumnShard::TEvReadResult>(handle);
            UNIT_ASSERT(event);

            auto& response = event->Record;
            UNIT_ASSERT_VALUES_EQUAL(response.GetOrigin(), TTestTxConfig::TxTablet0);
            UNIT_ASSERT_VALUES_EQUAL(response.GetTxInitiator(), metaShard);
            UNIT_ASSERT_VALUES_EQUAL(response.GetStatus(), (ui32)NKikimrTxColumnShard::EResultStatus::ERROR);
        }

        // Try to scan snapshot that is too old
        {
            {
                auto request = std::make_unique<TEvColumnShard::TEvScan>();
                request->Record.SetTxId(1000);
                request->Record.SetScanId(1);
                request->Record.SetLocalPathId(tableId);
                request->Record.SetTablePath("test_olap_table");
                request->Record.MutableSnapshot()->SetStep(planStep - staleness.MilliSeconds());
                request->Record.MutableSnapshot()->SetTxId(Max<ui64>());

                ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender, request.release());
            }

            auto event = runtime.GrabEdgeEvent<NKqp::TEvKqpCompute::TEvScanError>(handle);
            UNIT_ASSERT(event);

            auto& response = event->Record;
            // Cerr << response << Endl;
            UNIT_ASSERT_VALUES_EQUAL(response.GetStatus(), Ydb::StatusIds::BAD_REQUEST);
            UNIT_ASSERT_VALUES_EQUAL(response.IssuesSize(), 1);
            UNIT_ASSERT_STRING_CONTAINS(response.GetIssues(0).message(), "Snapshot too old: {640000:max}");
        }
    }

    void TestCompactionGC() {
        TTestBasicRuntime runtime;
        TTester::Setup(runtime);
        auto csDefaultControllerGuard = NKikimr::NYDBTest::TControllers::RegisterCSControllerGuard<TDefaultTestsController>();

        runtime.SetLogPriority(NKikimrServices::BLOB_CACHE, NActors::NLog::PRI_INFO);

        TActorId sender = runtime.AllocateEdgeActor();
        CreateTestBootstrapper(runtime, CreateTestTabletInfo(TTestTxConfig::TxTablet0, TTabletTypes::ColumnShard), &CreateColumnShard);

        TDispatchOptions options;
        options.FinalEvents.push_back(TDispatchOptions::TFinalEventCondition(TEvTablet::EvBoot));
        runtime.DispatchEvents(options);

        ui64 metaShard = TTestTxConfig::TxTablet1;
        ui64 writeId = 0;
        ui64 tableId = 1;

        auto ydbSchema = TTestSchema::YdbSchema();
        SetupSchema(runtime, sender, tableId);
        TAutoPtr<IEventHandle> handle;

        bool blockReadFinished = true;
        THashSet<ui64> inFlightReads;
        ui64 addedPortions = 0;
        THashSet<ui64> oldPortions;
        THashSet<ui64> deletedPortions;
        THashSet<TString> deletedBlobs;
        THashSet<TString> delayedBlobs;
        ui64 compactionsHappened = 0;
        ui64 cleanupsHappened = 0;

        auto captureEvents = [&](TTestActorRuntimeBase&, TAutoPtr<IEventHandle>& ev) {
            if (auto* msg = TryGetPrivateEvent<NColumnShard::TEvPrivate::TEvReadFinished>(ev)) {
                Cerr <<  "EvReadFinished " << msg->RequestCookie << Endl;
                inFlightReads.insert(msg->RequestCookie);
                if (blockReadFinished) {
                    return true;
                }
            } else if (auto* msg = TryGetPrivateEvent<NColumnShard::TEvPrivate::TEvWriteIndex>(ev)) {
                // Cerr <<  "EvWriteIndex" << Endl << *msg->IndexChanges << Endl;

                if (auto append = dynamic_pointer_cast<NOlap::TChangesWithAppend>(msg->IndexChanges)) {
                    Y_ABORT_UNLESS(append->AppendedPortions.size());
                    Cerr << "Added portions:";
                    for (const auto& portion : append->AppendedPortions) {
                        ++addedPortions;
                        ui64 portionId = addedPortions;
                        Cerr << " " << portionId << "(" << portion.GetPortionInfo().GetPortion() << ")";
                    }
                    Cerr << Endl;
                }
                if (auto compact = dynamic_pointer_cast<NOlap::TCompactColumnEngineChanges>(msg->IndexChanges)) {
                    Y_ABORT_UNLESS(compact->SwitchedPortions.size());
                    ++compactionsHappened;
                    Cerr << "Compaction old portions:";
                    ui64 srcPathId{0};
                    for (const auto& portionInfo : compact->SwitchedPortions) {
                        const ui64 pathId = portionInfo.GetPathId();
                        UNIT_ASSERT(!srcPathId || srcPathId == pathId);
                        srcPathId = pathId;
                        oldPortions.insert(portionInfo.GetPortion());
                    }
                    Cerr << Endl;
                }
                if (auto cleanup = dynamic_pointer_cast<NOlap::TCleanupColumnEngineChanges>(msg->IndexChanges)) {
                    Y_ABORT_UNLESS(cleanup->PortionsToDrop.size());
                    ++cleanupsHappened;
                    Cerr << "Cleanup old portions:";
                    for (const auto& portion : cleanup->PortionsToDrop) {
                        Cerr << " " << portion.GetPortion();
                        deletedPortions.insert(portion.GetPortion());
                    }
                    Cerr << Endl;
                }
            } else if (auto* msg = TryGetPrivateEvent<NActors::NLog::TEvLog>(ev)) {
                bool matchedEvent = false;
                {
                    TString prefixes[2] = {"Delay Delete Blob ", "Delay Delete Small Blob "};
                    for (TString prefix : prefixes) {
                        size_t pos = msg->Line.find(prefix);
                        if (pos != TString::npos) {
                            TString blobId = msg->Line.substr(pos + prefix.size());
                            Cerr << "Delayed delete: " << blobId << Endl;
                            delayedBlobs.insert(blobId);
                            matchedEvent = true;
                            break;
                        }
                    }
                }
                if (!matchedEvent){
                    TString prefix = "Delete Small Blob ";
                    size_t pos = msg->Line.find(prefix);
                    if (pos != TString::npos) {
                        TString blobId = msg->Line.substr(pos + prefix.size());
                        Cerr << "Delete small blob: " << blobId << Endl;
                        deletedBlobs.insert(blobId);
                        delayedBlobs.erase(blobId);
                        matchedEvent = true;
                    }
                }
            } else if (auto* msg = TryGetPrivateEvent<NKikimr::TEvBlobStorage::TEvCollectGarbage>(ev)) {
                // Extract and save all DoNotKeep blobIds
                Cerr << "GC for channel " << msg->Channel;
                if (msg->DoNotKeep) {
                    Cerr << " deletes blobs: " << JoinStrings(msg->DoNotKeep->begin(), msg->DoNotKeep->end(), " ");
                    for (const auto& blobId : *msg->DoNotKeep) {
                        deletedBlobs.insert(blobId.ToString());
                        delayedBlobs.erase(TUnifiedBlobId(0, blobId).ToStringNew());
                    }
                }
                Cerr << Endl;
            }
            return false;
        };
        runtime.SetEventFilter(captureEvents);

        // Disable GC batching so that deleted blobs get collected without a delay
        {
            TAtomic unusedPrev;
            runtime.GetAppData().Icb->SetValue("ColumnShardControls.BlobCountToTriggerGC", 1, unusedPrev);
        }

        {
            TAtomic unusedPrev;
            runtime.GetAppData().Icb->SetValue("ColumnShardControls.MaxPortionsInGranule", 10, unusedPrev);
        }

        // Write different keys: grow on compaction

        static const ui32 triggerPortionSize = 75 * 1000;
        std::pair<ui64, ui64> triggerPortion = {0, triggerPortionSize};
        TString triggerData = MakeTestBlob(triggerPortion, ydbSchema);
        UNIT_ASSERT(triggerData.size() > NColumnShard::TLimits::MIN_BYTES_TO_INSERT);
        UNIT_ASSERT(triggerData.size() < NColumnShard::TLimits::GetMaxBlobSize());

        ui64 planStep = 5000000;
        ui64 txId = 1000;

        // Overwrite the same data multiple times to produce multiple portions at different timestamps
        ui32 numWrites = 14;
        for (ui32 i = 0; i < numWrites; ++i, ++writeId, ++planStep, ++txId) {
            std::vector<ui64> writeIds;
            UNIT_ASSERT(WriteData(runtime, sender, writeId, tableId, triggerData, ydbSchema, true, &writeIds));

            ProposeCommit(runtime, sender, txId, writeIds);
            PlanCommit(runtime, sender, planStep, txId);
        }

        // Do a small write that is not indexed so that we will get a committed blob in read request
        {
            TString smallData = MakeTestBlob({0, 2}, ydbSchema);
            UNIT_ASSERT(smallData.size() < 100 * 1024);
            std::vector<ui64> writeIds;
            UNIT_ASSERT(WriteData(runtime, sender, writeId, tableId, smallData, ydbSchema, true, &writeIds));

            ProposeCommit(runtime, sender, txId, writeIds);
            PlanCommit(runtime, sender, planStep, txId);
            ++writeId;
            ++planStep;
            ++txId;
        }

        --planStep;
        --txId;
        Cerr << compactionsHappened << Endl;
        UNIT_ASSERT_GE(compactionsHappened, 3); // we catch it three times per action

        ui64 previousCompactionsHappened = compactionsHappened;
        ui64 previousCleanupsHappened = cleanupsHappened;

        // Send a request that reads the latest version
        // This request is expected to read at least 1 committed blob and several index portions
        // These committed blob and portions must not be deleted by the BlobManager until the read request finishes
        auto read = std::make_unique<TEvColumnShard::TEvRead>(sender, metaShard, planStep, txId, tableId);
        Proto(read.get()).AddColumnNames("timestamp");
        Proto(read.get()).AddColumnNames("message");

        ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender, read.release());

        ui32 expected = 0;
        ui32 num = 0;
        while (!expected || num < expected) {
            auto event = runtime.GrabEdgeEvent<TEvColumnShard::TEvReadResult>(handle);
            UNIT_ASSERT(event);

            auto& resRead = Proto(event);
            UNIT_ASSERT_EQUAL(resRead.GetOrigin(), TTestTxConfig::TxTablet0);
            UNIT_ASSERT_EQUAL(resRead.GetTxInitiator(), metaShard);
            UNIT_ASSERT_EQUAL(resRead.GetStatus(), NKikimrTxColumnShard::EResultStatus::SUCCESS);

            if (resRead.GetFinished()) {
                expected = resRead.GetBatch() + 1;
                UNIT_ASSERT(resRead.HasMeta());
            }
            UNIT_ASSERT(resRead.GetData().size() > 0);

            ++num;
            UNIT_ASSERT(num < 10);
        }

        // We captured EvReadFinished event and dropped is so the columnshard still thinks that
        // read request is in progress and keeps the portions

        // Advance the time in order to trigger GC
        TDuration delay = TDuration::Minutes(6);
        planStep += delay.MilliSeconds();
        numWrites = 10;
        for (ui32 i = 0; i < numWrites; ++i, ++writeId, ++planStep, ++txId) {
            std::vector<ui64> writeIds;
            UNIT_ASSERT(WriteData(runtime, sender, writeId, tableId, triggerData, ydbSchema, true, &writeIds));

            ProposeCommit(runtime, sender, txId, writeIds);
            PlanCommit(runtime, sender, planStep, txId);
        }

        Cerr << "Compactions happened: " << compactionsHappened << Endl;
        Cerr << "Cleanups happened: " << cleanupsHappened << Endl;
        Cerr << "Old portions: " << JoinStrings(oldPortions.begin(), oldPortions.end(), " ") << Endl;
        Cerr << "Cleaned up portions: " << JoinStrings(deletedPortions.begin(), deletedPortions.end(), " ") << Endl;

        // Check that GC happened but it didn't collect some old portions
        UNIT_ASSERT_GT(compactionsHappened, previousCompactionsHappened);
        UNIT_ASSERT_GT(cleanupsHappened, previousCleanupsHappened);
        UNIT_ASSERT_GT_C(oldPortions.size(), deletedPortions.size(), "Some old portions must not be deleted because the are in use by read");
        UNIT_ASSERT_GT_C(delayedBlobs.size(), 0, "Read request is expected to have at least one committed blob, which deletion must be delayed");
        previousCompactionsHappened = compactionsHappened;
        previousCleanupsHappened = cleanupsHappened;

        // Send EvReadFinished to release kept portions
        blockReadFinished = false;
        UNIT_ASSERT_VALUES_EQUAL(inFlightReads.size(), 1);
        {
            auto read = std::make_unique<NColumnShard::TEvPrivate::TEvReadFinished>(*inFlightReads.begin());
            ForwardToTablet(runtime, TTestTxConfig::TxTablet0, sender, read.release());
        }

        // Advance the time and trigger some more cleanups withno compactions
        auto csControllerGuard = NKikimr::NYDBTest::TControllers::RegisterCSControllerGuard<TDisableCompactionController>();
        planStep += 2 * delay.MilliSeconds();
        numWrites = 2;
        for (ui32 i = 0; i < numWrites; ++i, ++writeId, ++planStep, ++txId) {
            std::vector<ui64> writeIds;
            UNIT_ASSERT(WriteData(runtime, sender, writeId, tableId, triggerData, ydbSchema, true, &writeIds));

            ProposeCommit(runtime, sender, txId, writeIds);
            PlanCommit(runtime, sender, planStep, txId);
        }

        Cerr << "Compactions happened: " << compactionsHappened << Endl;
        Cerr << "Cleanups happened: " << cleanupsHappened << Endl;
        Cerr << "Old portions: " << JoinStrings(oldPortions.begin(), oldPortions.end(), " ") << Endl;
        Cerr << "Cleaned up portions: " << JoinStrings(deletedPortions.begin(), deletedPortions.end(), " ") << Endl;

        // Check that previously kept portions are collected
        UNIT_ASSERT_GE(compactionsHappened, previousCompactionsHappened);
        UNIT_ASSERT_GT(cleanupsHappened, previousCleanupsHappened);
        UNIT_ASSERT_VALUES_EQUAL_C(oldPortions.size(), deletedPortions.size(), "All old portions must be deleted after read has finished");
        UNIT_ASSERT_VALUES_EQUAL_C(delayedBlobs.size(), 0, "All previously delayed deletions must now happen " + JoinSeq(",", delayedBlobs));
    }

    Y_UNIT_TEST(CompactionGC) {
        TestCompactionGC();
    }
}

}
