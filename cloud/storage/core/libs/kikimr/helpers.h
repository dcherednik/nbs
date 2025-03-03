#pragma once

#include "public.h"

#include <cloud/storage/core/libs/actors/helpers.h>
#include <cloud/storage/core/libs/common/error.h>

#include <library/cpp/actors/core/actor.h>
#include <library/cpp/actors/core/event.h>

#include <contrib/ydb/core/base/tablet_pipe.h>
#include <contrib/ydb/core/protos/base.pb.h>
#include <contrib/ydb/core/protos/flat_tx_scheme.pb.h>

#include <util/generic/string.h>

namespace NCloud {

////////////////////////////////////////////////////////////////////////////////

NProto::TError MakeKikimrError(NKikimrProto::EReplyStatus status, TString errorReason = {});
NProto::TError MakeSchemeShardError(NKikimrScheme::EStatus status, TString errorReason = {});

inline void PipeSend(
    const NActors::TActorContext& ctx,
    const NActors::TActorId& pipe,
    std::unique_ptr<NActors::IEventHandle>&& event)
{
    // see pipe client code for details
    event->Rewrite(NKikimr::TEvTabletPipe::EvSend, pipe);
    ctx.Send(event.release());
}

}   // namespace NCloud
