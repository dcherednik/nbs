LIBRARY()

PEERDIR(
    library/cpp/actors/core
    library/cpp/grpc/client
    yt/cpp/mapreduce/interface
    contrib/ydb/library/yql/providers/dq/config
    contrib/ydb/library/yql/core/issue
    contrib/ydb/library/yql/providers/common/metrics
    contrib/ydb/library/yql/providers/dq/api/grpc
    contrib/ydb/library/yql/providers/dq/api/protos
    contrib/ydb/library/yql/providers/dq/common
    contrib/ydb/library/yql/providers/yt/lib/log
    contrib/ydb/library/yql/providers/dq/actors/events
)

IF (NOT OS_WINDOWS)
    PEERDIR(
        yt/yt/client
    )
ENDIF()

SET(
    SOURCE
    nodeid_assigner.cpp
    nodeid_assigner.h
    resource_manager.cpp
    resource_manager.h
)

IF (NOT OS_WINDOWS)
    SET(
        SOURCE
        ${SOURCE}
        nodeid_cleaner.cpp
        nodeid_cleaner.h
        worker_registrator.cpp
        worker_registrator.h
        lock.cpp
        lock.h
        resource_uploader.cpp
        resource_downloader.cpp
        resource_cleaner.cpp
        yt_wrapper.cpp
        yt_wrapper.h
        yt_resource_manager.cpp
    )
ENDIF()

SRCS(
    ${SOURCE}
)

YQL_LAST_ABI_VERSION()

END()
