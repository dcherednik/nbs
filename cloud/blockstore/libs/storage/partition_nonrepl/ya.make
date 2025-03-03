LIBRARY()

SRCS(
    config.cpp
    copy_range.cpp
    replica_info.cpp
    resync_range.cpp

    mirror_request_actor.cpp

    part_mirror.cpp
    part_mirror_actor.cpp
    part_mirror_actor_mirror.cpp
    part_mirror_actor_readblocks.cpp
    part_mirror_actor_stats.cpp
    part_mirror_state.cpp

    part_mirror_resync.cpp
    part_mirror_resync_actor.cpp
    part_mirror_resync_actor_forward.cpp
    part_mirror_resync_actor_readblocks.cpp
    part_mirror_resync_actor_resync.cpp
    part_mirror_resync_actor_stats.cpp
    part_mirror_resync_state.cpp
    part_mirror_resync_util.cpp

    part_nonrepl.cpp
    part_nonrepl_actor.cpp
    part_nonrepl_actor_checksumblocks.cpp
    part_nonrepl_actor_readblocks.cpp
    part_nonrepl_actor_readblocks_local.cpp
    part_nonrepl_actor_stats.cpp
    part_nonrepl_actor_writeblocks.cpp
    part_nonrepl_actor_zeroblocks.cpp
    part_nonrepl_common.cpp

    part_nonrepl_rdma.cpp
    part_nonrepl_rdma_actor.cpp
    part_nonrepl_rdma_actor_checksumblocks.cpp
    part_nonrepl_rdma_actor_readblocks.cpp
    part_nonrepl_rdma_actor_readblocks_local.cpp
    part_nonrepl_rdma_actor_stats.cpp
    part_nonrepl_rdma_actor_writeblocks.cpp
    part_nonrepl_rdma_actor_zeroblocks.cpp

    part_nonrepl_migration.cpp
    part_nonrepl_migration_actor.cpp
    part_nonrepl_migration_actor_migration.cpp
    part_nonrepl_migration_actor_mirror.cpp
    part_nonrepl_migration_actor_readblocks.cpp
    part_nonrepl_migration_actor_readblocks_local.cpp
    part_nonrepl_migration_actor_stats.cpp
    part_nonrepl_migration_state.cpp
)

PEERDIR(
    cloud/blockstore/config
    cloud/blockstore/libs/common
    cloud/blockstore/libs/kikimr
    cloud/blockstore/libs/rdma/iface
    cloud/blockstore/libs/service_local
    cloud/blockstore/libs/storage/api
    cloud/blockstore/libs/storage/core
    cloud/blockstore/libs/storage/partition_nonrepl/model
    cloud/blockstore/libs/storage/protos

    cloud/storage/core/libs/common

    library/cpp/actors/core

    contrib/ydb/core/base
    contrib/ydb/core/testlib
    contrib/ydb/core/testlib/basics
)

END()

RECURSE(
    model
)

RECURSE_FOR_TESTS(
    ut
)
