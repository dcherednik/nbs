LIBRARY()

SRCS(
    query_metrics.h
    query_metrics.cpp
    query_stats.h
    query_stats.cpp
)

PEERDIR(
    library/cpp/actors/core
    contrib/ydb/core/base
    contrib/ydb/core/kqp/runtime
    contrib/ydb/core/sys_view/common
    contrib/ydb/core/sys_view/service
)

YQL_LAST_ABI_VERSION()

END()

RECURSE_FOR_TESTS(
    ut
)
