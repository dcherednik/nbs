SRCDIR(contrib/ydb/library/yql/minikql/computation)

SRCS(
    mkql_block_builder.cpp
    mkql_block_impl.cpp
    mkql_block_reader.cpp
    mkql_block_transport.cpp
    mkql_computation_node.cpp
    mkql_computation_node_codegen.cpp
    mkql_computation_node_graph.cpp
    mkql_computation_node_graph_saveload.cpp
    mkql_computation_node_holders.cpp
    mkql_computation_node_impl.cpp
    mkql_computation_node_pack.cpp
    mkql_computation_node_pack_impl.cpp
    mkql_custom_list.cpp
    mkql_llvm_base.cpp
    mkql_validate.cpp
    mkql_value_builder.cpp
    mkql_computation_pattern_cache.cpp
    presort.cpp
)

PEERDIR(
    contrib/libs/apache/arrow
    library/cpp/actors/util
    library/cpp/enumbitset
    library/cpp/packedtypes
    library/cpp/random_provider
    library/cpp/time_provider
    contrib/ydb/library/yql/minikql
    contrib/ydb/library/yql/minikql/arrow
    contrib/ydb/library/yql/parser/pg_wrapper/interface
    contrib/ydb/library/yql/public/udf
    contrib/ydb/library/yql/utils
    library/cpp/threading/future
)

PROVIDES(mkql_computation)

YQL_LAST_ABI_VERSION()
