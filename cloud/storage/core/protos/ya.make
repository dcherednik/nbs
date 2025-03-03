PROTO_LIBRARY()

INCLUDE_TAGS(GO_PROTO)

PEERDIR(
    library/cpp/lwtrace/protos
)

SRCS(
    authorization_mode.proto
    certificate.proto
    endpoints.proto
    error.proto
    media.proto
    request_source.proto
    tablet.proto
    throttler.proto
    trace.proto
)

END()
