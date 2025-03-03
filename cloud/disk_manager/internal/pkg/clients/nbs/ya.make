OWNER(g:cloud-nbs)

GO_LIBRARY()

SRCS(
    client.go
    factory.go
    interface.go
    metrics.go
    multi_zone_client.go
    session.go
    testing_client.go
)

END()

RECURSE(
    config
)

RECURSE_FOR_TESTS(
    mocks
    tests
)
