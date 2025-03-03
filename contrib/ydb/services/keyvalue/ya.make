LIBRARY()

SRCS(
    grpc_service.cpp
)

PEERDIR(
    contrib/ydb/public/api/grpc
    library/cpp/grpc/server
    contrib/ydb/core/grpc_services
    contrib/ydb/core/grpc_services/base
    contrib/ydb/core/kesus/tablet
    contrib/ydb/core/keyvalue
)

END()

RECURSE_FOR_TESTS(
    ut
)
