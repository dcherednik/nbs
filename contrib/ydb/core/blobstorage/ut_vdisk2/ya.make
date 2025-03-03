UNITTEST()

FORK_SUBTESTS()

SIZE(MEDIUM)
TIMEOUT(600)

IF (SANITIZER_TYPE)
    ENV(TIMEOUT=400)
ENDIF()

SRCS(
    defs.h
    env.h
    huge.cpp
)

PEERDIR(
    library/cpp/testing/unittest
    contrib/ydb/core/blobstorage/backpressure
    contrib/ydb/core/blobstorage/groupinfo
    contrib/ydb/core/blobstorage/pdisk/mock
    contrib/ydb/core/blobstorage/vdisk
    contrib/ydb/core/blobstorage/vdisk/common
    contrib/ydb/core/tx/scheme_board
    contrib/ydb/library/yql/public/udf/service/stub
)

END()
