LIBRARY()

SRCS(
    data_plane_helpers.cpp
    sdk_test_setup.h
    test_utils.h
    test_server.h
    test_server.cpp
    ut_utils.h
    ut_utils.cpp
)

PEERDIR(
    library/cpp/grpc/server
    library/cpp/testing/unittest
    library/cpp/threading/chunk_queue
    contrib/ydb/core/testlib/default
    contrib/ydb/library/persqueue/topic_parser_public
    contrib/ydb/public/sdk/cpp/client/ydb_driver
    contrib/ydb/public/sdk/cpp/client/ydb_persqueue_core
    contrib/ydb/public/sdk/cpp/client/ydb_persqueue_public
    contrib/ydb/public/sdk/cpp/client/ydb_table
)

YQL_LAST_ABI_VERSION()

END()
