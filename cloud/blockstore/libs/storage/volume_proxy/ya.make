LIBRARY()

SRCS(
    volume_proxy.cpp
)

PEERDIR(
    cloud/blockstore/libs/kikimr
    cloud/blockstore/libs/storage/api
    cloud/blockstore/libs/storage/core
    library/cpp/actors/core
    contrib/ydb/core/tablet
    contrib/ydb/core/testlib
)

END()

RECURSE_FOR_TESTS(
    ut
)
