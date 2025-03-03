LIBRARY()

SRCS(
    clusters_from_connections.cpp
    database_resolver.cpp
    error.cpp
    nodes_health_check.cpp
    nodes_manager.cpp
    pending_fetcher.cpp
    proxy.cpp
    proxy_private.cpp
    rate_limiter.cpp
    rate_limiter_resources.cpp
    result_writer.cpp
    run_actor.cpp
    table_bindings_from_bindings.cpp
    task_get.cpp
    task_ping.cpp
    task_result_write.cpp
)

PEERDIR(
    library/cpp/actors/core
    library/cpp/actors/interconnect
    library/cpp/json/yson
    library/cpp/monlib/dynamic_counters
    library/cpp/random_provider
    library/cpp/scheme
    library/cpp/string_utils/quote
    library/cpp/time_provider
    library/cpp/yson
    library/cpp/yson/node
    contrib/ydb/core/base
    contrib/ydb/core/fq/libs/actors/logging
    contrib/ydb/core/fq/libs/checkpointing
    contrib/ydb/core/fq/libs/checkpointing_common
    contrib/ydb/core/fq/libs/common
    contrib/ydb/core/fq/libs/compute/common
    contrib/ydb/core/fq/libs/compute/ydb
    contrib/ydb/core/fq/libs/config/protos
    contrib/ydb/core/fq/libs/control_plane_storage
    contrib/ydb/core/fq/libs/control_plane_storage/events
    contrib/ydb/core/fq/libs/db_id_async_resolver_impl
    contrib/ydb/core/fq/libs/db_schema
    contrib/ydb/core/fq/libs/events
    contrib/ydb/core/fq/libs/exceptions
    contrib/ydb/core/fq/libs/grpc
    contrib/ydb/core/fq/libs/private_client
    contrib/ydb/core/fq/libs/rate_limiter/utils
    contrib/ydb/core/fq/libs/result_formatter
    contrib/ydb/core/fq/libs/shared_resources
    contrib/ydb/core/fq/libs/signer
    contrib/ydb/core/protos
    contrib/ydb/core/util
    contrib/ydb/library/mkql_proto
    contrib/ydb/library/security
    contrib/ydb/library/yql/ast
    contrib/ydb/library/yql/core/facade
    contrib/ydb/library/yql/core/services/mounts
    contrib/ydb/library/yql/dq/integration/transform
    contrib/ydb/library/yql/minikql/comp_nodes/llvm
    contrib/ydb/library/yql/providers/common/codec
    contrib/ydb/library/yql/providers/common/comp_nodes
    contrib/ydb/library/yql/providers/common/db_id_async_resolver
    contrib/ydb/library/yql/providers/common/metrics
    contrib/ydb/library/yql/providers/common/provider
    contrib/ydb/library/yql/providers/common/schema/mkql
    contrib/ydb/library/yql/providers/common/token_accessor/client
    contrib/ydb/library/yql/providers/common/udf_resolve
    contrib/ydb/library/yql/providers/dq/actors
    contrib/ydb/library/yql/providers/dq/common
    contrib/ydb/library/yql/providers/dq/counters
    contrib/ydb/library/yql/providers/dq/provider
    contrib/ydb/library/yql/providers/dq/provider/exec
    contrib/ydb/library/yql/providers/dq/worker_manager/interface
    contrib/ydb/library/yql/providers/generic/connector/api/common
    contrib/ydb/library/yql/providers/generic/connector/libcpp
    contrib/ydb/library/yql/providers/generic/provider
    contrib/ydb/library/yql/providers/pq/cm_client
    contrib/ydb/library/yql/providers/pq/provider
    contrib/ydb/library/yql/providers/pq/task_meta
    contrib/ydb/library/yql/providers/s3/provider
    contrib/ydb/library/yql/providers/ydb/provider
    contrib/ydb/library/yql/public/issue
    contrib/ydb/library/yql/public/issue/protos
    contrib/ydb/library/yql/sql/settings
    contrib/ydb/library/yql/utils
    contrib/ydb/library/yql/utils/actor_log
    contrib/ydb/public/api/protos
    contrib/ydb/public/lib/fq
    contrib/ydb/public/sdk/cpp/client/ydb_query
    contrib/ydb/public/sdk/cpp/client/ydb_operation
    contrib/ydb/public/sdk/cpp/client/ydb_table
)

YQL_LAST_ABI_VERSION()

END()

RECURSE(
    logging
)
