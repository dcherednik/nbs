UNITTEST_FOR(contrib/ydb/library/yql/core/spilling)

FORK_SUBTESTS()

SPLIT_FACTOR(60)

IF (SANITIZER_TYPE == "thread" OR WITH_VALGRIND)
    TIMEOUT(3600)
    SIZE(LARGE)
    TAG(ya:fat)
ELSE()
    TIMEOUT(600)
    SIZE(MEDIUM)
ENDIF()

SRCS(
    spilling_ut.cpp
 )

PEERDIR(
    contrib/ydb/library/yql/public/udf
    contrib/ydb/library/yql/public/udf/service/exception_policy
)

YQL_LAST_ABI_VERSION()

IF (MKQL_RUNTIME_VERSION)
    CFLAGS(
        -DMKQL_RUNTIME_VERSION=$MKQL_RUNTIME_VERSION
    )
ENDIF()

REQUIREMENTS(ram:10)

END()
