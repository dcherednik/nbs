LIBRARY()

SRCS(
    events.cpp
)

PEERDIR(
    library/cpp/actors/core
    contrib/ydb/core/fq/libs/config/protos
    contrib/ydb/core/fq/libs/control_plane_storage/proto
    contrib/ydb/core/fq/libs/protos
    contrib/ydb/public/api/grpc/draft
    contrib/ydb/public/lib/operation_id/protos
    contrib/ydb/public/sdk/cpp/client/ydb_query
)

END()
