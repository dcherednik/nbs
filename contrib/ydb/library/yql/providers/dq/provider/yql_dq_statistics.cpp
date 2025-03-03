#include "yql_dq_statistics.h"
#include "yql_dq_state.h"

#include <contrib/ydb/library/yql/dq/opt/dq_opt_stat.h>
#include <contrib/ydb/library/yql/dq/opt/dq_opt_stat_transformer_base.h>
#include <contrib/ydb/library/yql/dq/integration/yql_dq_integration.h>
#include <contrib/ydb/library/yql/core/yql_expr_optimize.h>

#include <contrib/ydb/library/yql/providers/dq/expr_nodes/dqs_expr_nodes.h>

namespace NYql {

using namespace NNodes;

class TDqsStatisticsTransformer : public NDq::TDqStatisticsTransformerBase {
public:
    TDqsStatisticsTransformer(const TDqStatePtr& state)
        : NDq::TDqStatisticsTransformerBase(state->TypeCtx)
        , State(state)
    { }

    bool BeforeLambdasSpecific(const TExprNode::TPtr& input, TExprContext& ctx) override {
        bool matched = true;
        bool hasDqSource = false;

        if (TDqReadWrapBase::Match(input.Get()) || (hasDqSource = TDqSourceWrapBase::Match(input.Get()))) {
            auto node = hasDqSource
                ? input
                : input->Child(TDqReadWrapBase::idx_Input);
            auto dataSourceChildIndex = 1;
            YQL_ENSURE(node->ChildrenSize() > 1);
            YQL_ENSURE(node->Child(dataSourceChildIndex)->IsCallable("DataSource"));
            auto dataSourceName = node->Child(dataSourceChildIndex)->Child(0)->Content();
            auto datasource = State->TypeCtx->DataSourceMap.FindPtr(dataSourceName);
            YQL_ENSURE(datasource);
            if (auto dqIntegration = (*datasource)->GetDqIntegration()) {
                auto stat = dqIntegration->ReadStatistics(node, ctx);
                if (stat) {
                    State->TypeCtx->SetStats(input.Get(), std::move(std::make_shared<TOptimizerStatistics>(*stat)));
                }
            }
        } else {
            matched = false;
        }
        return matched;
    }

    bool AfterLambdasSpecific(const TExprNode::TPtr& input, TExprContext& ctx) override {
        Y_UNUSED(input);
        Y_UNUSED(ctx);
        return false;
    }

private:
    TDqStatePtr State;
};

THolder<IGraphTransformer> CreateDqsStatisticsTransformer(TDqStatePtr state) {
    return MakeHolder<TDqsStatisticsTransformer>(state);
}

} // namespace NYql
