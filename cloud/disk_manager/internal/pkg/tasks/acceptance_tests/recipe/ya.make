OWNER(g:cloud-nbs)

PY3_PROGRAM()

PY_SRCS(
    __main__.py
    node_launcher.py
)

PEERDIR(
    cloud/disk_manager/test/common
    cloud/storage/core/tools/common/python
    kikimr/ci/libraries
    library/python/testing/recipe
)

END()

RECURSE(
    init-db
    node
    tasks
)
