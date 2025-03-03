LIBRARY()

SRCS(
    hive_proxy.cpp
    hive_proxy_actor.cpp
    hive_proxy_actor_bootext.cpp
    hive_proxy_actor_create.cpp
    hive_proxy_actor_drain.cpp
    hive_proxy_actor_getinfo.cpp
    hive_proxy_actor_lock.cpp
    hive_proxy_actor_reassign.cpp
    hive_proxy_actor_unlock.cpp
    hive_proxy_fallback_actor.cpp
    tablet_boot_info_backup.cpp
)

PEERDIR(
    cloud/storage/core/libs/actors
    cloud/storage/core/libs/aio
    cloud/storage/core/libs/api
    cloud/storage/core/libs/hive_proxy/protos

    contrib/ydb/core/base
    contrib/ydb/core/mind
    contrib/ydb/core/tablet
    contrib/ydb/core/tablet_flat
    contrib/ydb/core/testlib
    contrib/ydb/core/testlib/basics

    library/cpp/actors/core
)

END()

RECURSE(
    protos
)

RECURSE_FOR_TESTS(
    ut
)
