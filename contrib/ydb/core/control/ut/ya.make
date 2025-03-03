UNITTEST_FOR(contrib/ydb/core/control)

FORK_SUBTESTS()

TIMEOUT(600)

SIZE(MEDIUM)

PEERDIR(
    library/cpp/actors/core
    library/cpp/actors/interconnect
    library/cpp/testing/unittest
    util
    contrib/ydb/core/base
    contrib/ydb/core/mind
    contrib/ydb/core/mon
    contrib/ydb/library/yql/sql/pg_dummy
    contrib/ydb/services/ydb
    contrib/ydb/library/yql/public/udf/service/exception_policy
)

SRCS(
    immediate_control_board_ut.cpp
    immediate_control_board_actor_ut.cpp
)

END()
