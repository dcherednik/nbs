#pragma once
// unique tag to fix pragma once gcc glueing: ./ydb/core/blobstorage/pdisk/defs.h
#include <contrib/ydb/core/base/defs.h>
#include <contrib/ydb/library/yverify_stream/yverify_stream.h>
#include <library/cpp/actors/core/event_local.h>
#include <library/cpp/actors/core/actor_bootstrapped.h>
#include <library/cpp/actors/core/log.h>
#include <contrib/ydb/library/services/services.pb.h>
#include <util/system/sanitizers.h>
#include <library/cpp/actors/core/monotonic_provider.h>
