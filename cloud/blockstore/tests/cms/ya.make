PY3TEST()

INCLUDE(${ARCADIA_ROOT}/cloud/storage/core/tests/recipes/medium.inc)

TEST_SRCS(test.py)

PEERDIR(
    cloud/blockstore/config
    cloud/blockstore/tests/python/lib

    library/python/testing/yatest_common

    contrib/ydb/tests/library

    contrib/python/requests
)

DEPENDS(
    cloud/blockstore/apps/server
    contrib/ydb/apps/ydbd
)

DATA(
    arcadia/cloud/blockstore/tests/certs/server.crt
    arcadia/cloud/blockstore/tests/certs/server.key
)

END()
