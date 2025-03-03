LIBRARY()

SRCS(
    sock64.h
    sock_config.h
    sock_impl.h
    sock_listener.cpp
    sock_listener.h
    sock_ssl.h
)

PEERDIR(
    library/cpp/actors/core
    library/cpp/actors/protos
    contrib/ydb/core/base
    contrib/ydb/core/protos
)

END()

RECURSE_FOR_TESTS(
#    ut
)
