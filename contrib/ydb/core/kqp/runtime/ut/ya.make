UNITTEST_FOR(contrib/ydb/core/kqp/runtime)

FORK_SUBTESTS()

SIZE(MEDIUM)
TIMEOUT(180)

SRCS(
    kqp_scan_data_ut.cpp
)

YQL_LAST_ABI_VERSION()

PEERDIR(
    library/cpp/testing/unittest
    contrib/ydb/core/testlib/basics/default
    contrib/ydb/library/yql/public/udf/service/exception_policy
)

END()
