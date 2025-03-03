LIBRARY()

SRCS(
    actors_factory.cpp
    executer_actor.cpp
    finalizer_actor.cpp
    initializer_actor.cpp
    resources_cleaner_actor.cpp
    result_writer_actor.cpp
    status_tracker_actor.cpp
    stopper_actor.cpp
    ydb_connector_actor.cpp
    ydb_run_actor.cpp
)

PEERDIR(
    library/cpp/actors/protos
    library/cpp/lwtrace/protos
    contrib/ydb/core/fq/libs/compute/common
    contrib/ydb/core/fq/libs/config/protos
    contrib/ydb/core/fq/libs/control_plane_storage/proto
    contrib/ydb/core/fq/libs/graph_params/proto
    contrib/ydb/core/fq/libs/grpc
    contrib/ydb/core/fq/libs/quota_manager/proto
    contrib/ydb/core/protos
    contrib/ydb/core/util
    contrib/ydb/library/db_pool/protos
    contrib/ydb/library/yql/core/expr_nodes
    contrib/ydb/library/yql/dq/expr_nodes
    contrib/ydb/library/yql/minikql/arrow
    contrib/ydb/library/yql/providers/dq/api/protos
    contrib/ydb/public/api/grpc
    contrib/ydb/public/api/grpc/draft
    contrib/ydb/public/lib/operation_id/protos
)

YQL_LAST_ABI_VERSION()

END()

RECURSE(
    control_plane
    events
    synchronization_service
)
