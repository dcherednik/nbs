LIBRARY()

SRCS(
    ut_helpers_query.cpp
)

PEERDIR(
    contrib/ydb/public/api/grpc
    contrib/ydb/public/api/grpc/draft
    contrib/ydb/public/api/protos
    contrib/ydb/public/api/protos/out
    library/cpp/grpc/client
)

END()

