SIZE(LARGE)
TIMEOUT(3600)

# use ya:not_autocheck and ya:manual with sanitizers until we have sufficient
# quota in the YC_NBS group: https://sandbox.yandex-team.ru/admin/groups/YC_NBS/general
IF (SANITIZER_TYPE)
    TAG(
        ya:fat
        ya:force_sandbox
        ya:not_autocheck
        ya:manual
        sb:ttl=3
        sb:logs_ttl=3
        sb:ssd
        sb:MULTISLOT
        sb:cores=16
    )
ELSE()
    TAG(
        ya:fat
        ya:force_sandbox
        sb:ttl=3
        sb:logs_ttl=3
        sb:ssd
        sb:MULTISLOT
        sb:cores=16
    )
ENDIF()

IF (NOT OPENSOURCE)
    FORK_SUBTESTS()
    SPLIT_FACTOR(8)
ENDIF()

REQUIREMENTS(
    cpu:8
    ram:30
)
