#pragma once

#include <contrib/ydb/core/kqp/runtime/kqp_compute.h>

#include <library/cpp/actors/core/actor.h>
#include <library/cpp/actors/core/actorid.h>

namespace NKikimr::NSysView {

THolder<NActors::IActor> CreateTopPartitionsScan(const NActors::TActorId& ownerId, ui32 scanId,
    const TTableId& tableId, const TTableRange& tableRange,
    const TArrayRef<NMiniKQL::TKqpComputeContextBase::TColumn>& columns);

} // NKikimr::NSysView
