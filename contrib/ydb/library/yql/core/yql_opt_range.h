#pragma once
#include <contrib/ydb/library/yql/ast/yql_expr.h>

namespace NYql {

TExprNode::TPtr ExpandRangeEmpty(const TExprNode::TPtr& node, TExprContext& ctx);
TExprNode::TPtr ExpandAsRange(const TExprNode::TPtr& node, TExprContext& ctx);
TExprNode::TPtr ExpandRangeFor(const TExprNode::TPtr& node, TExprContext& ctx);

}

