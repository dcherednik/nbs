#include "tablet_counters_app.h"

#include "tablet_counters_protobuf.h"

#include <contrib/ydb/core/protos/counters_schemeshard.pb.h>
#include <contrib/ydb/core/protos/counters_datashard.pb.h>
#include <contrib/ydb/core/protos/counters_hive.pb.h>
#include <contrib/ydb/core/protos/counters_kesus.pb.h>

namespace NKikimr {

THolder<TTabletCountersBase> CreateAppCountersByTabletType(TTabletTypes::EType type) {
    switch (type) {
    case TTabletTypes::SchemeShard:
        return MakeHolder<TAppProtobufTabletCounters<
            NSchemeShard::ESimpleCounters_descriptor,
            NSchemeShard::ECumulativeCounters_descriptor,
            NSchemeShard::EPercentileCounters_descriptor
        >>();
    case TTabletTypes::DataShard:
        return MakeHolder<TAppProtobufTabletCounters<
            NDataShard::ESimpleCounters_descriptor,
            NDataShard::ECumulativeCounters_descriptor,
            NDataShard::EPercentileCounters_descriptor
        >>();
    case TTabletTypes::Hive:
        return MakeHolder<TAppProtobufTabletCounters<
            NHive::ESimpleCounters_descriptor,
            NHive::ECumulativeCounters_descriptor,
            NHive::EPercentileCounters_descriptor
        >>();
    case TTabletTypes::Kesus:
        return MakeHolder<TAppProtobufTabletCounters<
            NKesus::ESimpleCounters_descriptor,
            NKesus::ECumulativeCounters_descriptor,
            NKesus::EPercentileCounters_descriptor
        >>();
    default:
        return {};
    }
}

}
