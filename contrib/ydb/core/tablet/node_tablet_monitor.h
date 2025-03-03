#pragma once

#include "defs.h"
#include <library/cpp/actors/core/defs.h>
#include <library/cpp/actors/core/actor.h>
#include <library/cpp/actors/core/event.h>
#include <library/cpp/actors/core/interconnect.h>
#include <contrib/ydb/core/base/events.h>
#include <contrib/ydb/core/protos/node_whiteboard.pb.h>
#include <util/generic/ptr.h>
#include <util/stream/str.h>

namespace NKikimr {
namespace NNodeTabletMonitor {

inline TActorId MakeNodeTabletMonitorID(ui32 node = 0) {
    char x[12] = {'n','o','d','e','t','a','b','l','e','t','m','o'};
    return TActorId(node, TStringBuf(x, 12));
}


struct ITabletStateClassifier : public TAtomicRefCount<ITabletStateClassifier>{
    virtual ui32 GetMaxTabletStateClass() const = 0;
    virtual std::function<bool(const NKikimrWhiteboard::TTabletStateInfo&)> GetTabletStateClassFilter(ui32 cls) const = 0;
    virtual TString GetTabletStateClassName(ui32 cls) = 0;
    virtual ~ITabletStateClassifier() {}

};


struct TTabletListElement {
    const TEvInterconnect::TNodeInfo* NodeInfo;
    ui64 TabletIndex;
    const NKikimrWhiteboard::TTabletStateInfo* TabletStateInfo;
};


struct TTabletFilterInfo {
    ui32 FilterNodeId;
    TString FilterNodeHost;
};


struct ITabletListRenderer : public TAtomicRefCount<ITabletListRenderer> {
    virtual void RenderPageHeader(TStringStream& str) = 0;
    virtual void RenderPageFooter(TStringStream& str) = 0;
    virtual void RenderTabletList(TStringStream& str,
                                  const TString& listName,
                                  const TVector<TTabletListElement>& tabletsToRender,
                                  const TTabletFilterInfo& filterInfo) = 0;
    virtual ~ITabletListRenderer() {}

};


IActor* CreateNodeTabletMonitor(const TIntrusivePtr<ITabletStateClassifier>& stateClassifier,
                                const TIntrusivePtr<ITabletListRenderer>& renderer);

} // NNodeTabletMonitor
} // NKikimr
