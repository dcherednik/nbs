#pragma once
#include <contrib/ydb/core/base/defs.h>
#include <library/cpp/actors/core/defs.h>
#include <library/cpp/actors/core/actor.h>
#include <library/cpp/actors/core/event.h>

namespace NKikimr {
namespace NTabletInfo {

inline TActorId MakeTabletInfoID(ui32 node = 0) {
    char x[12] = {'t','a','b','l','e','t','i','n','f','o','r','m'};
    return TActorId(node, TStringBuf(x, 12));
}

IActor* CreateTabletInfo();

} } // end of the NKikimr::NTabletInfo namespace

