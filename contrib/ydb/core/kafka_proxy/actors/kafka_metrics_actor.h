#pragma once

#include <contrib/ydb/core/kafka_proxy/kafka_events.h>

#include <library/cpp/monlib/dynamic_counters/counters.h>

#include <library/cpp/actors/core/actor.h>

namespace NKafka {

    struct TKafkaMetricsSettings {
        TIntrusivePtr<::NMonitoring::TDynamicCounters> Counters;
    };

    NActors::IActor* CreateKafkaMetricsActor(const TKafkaMetricsSettings& settings);

} // namespace NKafka
