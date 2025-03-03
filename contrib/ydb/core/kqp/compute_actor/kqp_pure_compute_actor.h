#pragma once

#include "kqp_compute_actor.h"
#include "kqp_compute_actor_impl.h"

#include <contrib/ydb/core/base/appdata.h>
#include <contrib/ydb/core/protos/tx_datashard.pb.h>
#include <contrib/ydb/core/kqp/rm_service/kqp_rm_service.h>
#include <contrib/ydb/core/kqp/runtime/kqp_compute.h>
#include <contrib/ydb/core/kqp/runtime/kqp_scan_data.h>
#include <contrib/ydb/core/sys_view/scan.h>
#include <contrib/ydb/library/yverify_stream/yverify_stream.h>

#include <contrib/ydb/library/yql/dq/actors/compute/dq_compute_actor_impl.h>


namespace NKikimr {
namespace NKqp {

class TKqpComputeActor : public TDqComputeActorBase<TKqpComputeActor> {
    using TBase = TDqComputeActorBase<TKqpComputeActor>;

public:
    static constexpr NKikimrServices::TActivity::EType ActorActivityType() {
        return NKikimrServices::TActivity::KQP_COMPUTE_ACTOR;
    }

    TKqpComputeActor(const TActorId& executerId, ui64 txId, NDqProto::TDqTask* task,
        IDqAsyncIoFactory::TPtr asyncIoFactory,
        const NKikimr::NMiniKQL::IFunctionRegistry* functionRegistry,
        const TComputeRuntimeSettings& settings, const TComputeMemoryLimits& memoryLimits,
        NWilson::TTraceId traceId, TIntrusivePtr<NActors::TProtoArenaHolder> arena);

    void DoBootstrap();

    STFUNC(StateFunc);

protected:
    ui64 CalcMkqlMemoryLimit() override;

public:
    void FillExtraStats(NDqProto::TDqComputeActorStats* dst, bool last);

private:
    void PassAway() override;

private:
    void HandleExecute(TEvKqpCompute::TEvScanInitActor::TPtr& ev);
    
    void HandleExecute(TEvKqpCompute::TEvScanData::TPtr& ev);

    void HandleExecute(TEvKqpCompute::TEvScanError::TPtr& ev);

    bool IsDebugLogEnabled(const TActorSystem* actorSystem);

private:
    NMiniKQL::TKqpScanComputeContext ComputeCtx;
    TMaybe<NKikimrTxDataShard::TKqpTransaction::TScanTaskMeta> Meta;
    NMiniKQL::TKqpScanComputeContext::TScanData* ScanData = nullptr;
    TActorId SysViewActorId;
    const TDqTaskRunnerParameterProvider ParameterProvider;
};

IActor* CreateKqpComputeActor(const TActorId& executerId, ui64 txId, NDqProto::TDqTask* task,
    IDqAsyncIoFactory::TPtr asyncIoFactory,
    const NKikimr::NMiniKQL::IFunctionRegistry* functionRegistry,
    const TComputeRuntimeSettings& settings, const TComputeMemoryLimits& memoryLimits,
    NWilson::TTraceId traceId, TIntrusivePtr<NActors::TProtoArenaHolder> arena);

} // namespace NKqp
} // namespace NKikimr
