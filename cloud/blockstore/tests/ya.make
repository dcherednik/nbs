# https://st.yandex-team.ru/DEVTOOLSSUPPORT-18977#6285fbd36101de4de4e29f48
IF (SANITIZER_TYPE != "undefined" AND SANITIZER_TYPE != "memory")
    RECURSE(
        plugin
    )
ENDIF()

RECURSE(
    client
    cms
    fio
    functional
    fuzzing
    infra-cms
    loadtest
    loadtest/remote
    monitoring
    mount
    notify
    python
    python_client
    rdma
    recipes
    resize-disk
    spare_node
    stats_aggregator_perf
    storage_discovery
    vhost-server
)
