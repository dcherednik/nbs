#include "dq_opt_stat.h"

#include <contrib/ydb/library/yql/core/yql_opt_utils.h>
#include <contrib/ydb/library/yql/utils/log/log.h>

using namespace NYql;
using namespace NYql::NNodes;

namespace {

    THashSet<TString> exprCallables = {"SafeCast", "Int32", "Date", "Interval", "String"};

    /**
     * Check if a callable is an attribute of some table
     * Currently just return a boolean and cover only basic cases
     */
    bool IsAttribute(const TExprBase& input) {
        if (input.Maybe<TCoMember>()) {
            return true;
        } else if (auto cast = input.Maybe<TCoSafeCast>()) {
            return IsAttribute(cast.Cast().Value());
        } else if (auto ifPresent = input.Maybe<TCoIfPresent>()) {
            return IsAttribute(ifPresent.Cast().Optional());
        }

        return false;
    }

    /**
     * Check that the expression is a constant expression
     * We use a whitelist of callables
     */
    bool IsConstant(const TExprBase& input) {
        if (input.Maybe<TCoAtom>()) {
            return true;
        } else if (input.Ref().IsCallable(exprCallables)) {
            if (input.Ref().ChildrenSize() >= 1) {
                auto callableInput = TExprBase(input.Ref().Child(0));
                return IsConstant(callableInput);
            } else {
                return false;
            }
        } else if (auto op = input.Maybe<TCoBinaryArithmetic>()) {
            auto left = op.Cast().Left();
            auto right = op.Cast().Right();
            return IsConstant(left) && IsConstant(right);
        }

        return false;
    }
}

/**
 * Compute the selectivity of a predicate given statistics about the input it operates on
 */
double NYql::NDq::ComputePredicateSelectivity(const TExprBase& input, const std::shared_ptr<TOptimizerStatistics>& stats) {
    double result = 1.0;

    // Process OptionalIf, just return the predicate statistics
    if (auto optIf = input.Maybe<TCoOptionalIf>()) {
        result = ComputePredicateSelectivity(optIf.Cast().Predicate(), stats);
    }

    // Same with Coalesce
    else if (auto coalesce = input.Maybe<TCoCoalesce>()) {
        result = ComputePredicateSelectivity(coalesce.Cast().Predicate(), stats);
    }

    // Process AND, OR and NOT logical operators.
    // In case of AND we multiply the selectivities, since we assume them to be independent
    // In case of OR we sum them up, again assuming independence and disjointness, but make sure its at most 1.0
    // In case of NOT we subtract the argument's selectivity from 1.0

    else if (auto andNode = input.Maybe<TCoAnd>()) {
        double res = 1.0;
        for (size_t i = 0; i < andNode.Cast().ArgCount(); i++) {
            res *= ComputePredicateSelectivity(andNode.Cast().Arg(i), stats);
        }
        result = res;
    } else if (auto orNode = input.Maybe<TCoOr>()) {
        double res = 0.0;
        for (size_t i = 0; i < orNode.Cast().ArgCount(); i++) {
            res += ComputePredicateSelectivity(orNode.Cast().Arg(i), stats);
        }
        result = std::max(res, 1.0);
    } else if (auto notNode = input.Maybe<TCoNot>()) {
        result = 1.0 - ComputePredicateSelectivity(notNode.Cast().Value(), stats);
    }

    // Process the equality predicate
    else if (auto equality = input.Maybe<TCoCmpEqual>()) {
        auto left = equality.Cast().Left();
        auto right = equality.Cast().Right();

        if (IsAttribute(right) && IsConstant(left)) {
            std::swap(left, right);
        }

        if (IsAttribute(left)) {
            // In case both arguments refer to an attribute, return 0.2
            if (IsAttribute(right)) {
                result = 0.2;
            }
            // In case the right side is a constant that can be extracted, compute the selectivity using statistics
            // Currently, with the basic statistics we just return 1/nRows

            else if (IsConstant(right)) {
                if (stats->Nrows > 1) {
                    result = 1.0 / stats->Nrows;
                } else {
                    result = 1.0;
                }
            }
        }

    }

    // Process all other comparison predicates
    else if (auto comparison = input.Maybe<TCoCompare>()) {
        auto left = comparison.Cast().Left();
        auto right = comparison.Cast().Right();

        if (IsAttribute(right) && IsConstant(left)) {
            std::swap(left, right);
        }

        if (IsAttribute(left)) {
            // In case both arguments refer to an attribute, return 0.2
            if (IsAttribute(right)) {
                result = 0.2;
            }
            // In case the right side is a constant that can be extracted, compute the selectivity using statistics
            // Currently, with the basic statistics we just return 0.5
            else if (IsConstant(right)) {
                result = 0.5;
            }
        }
    }

    return result;
}
