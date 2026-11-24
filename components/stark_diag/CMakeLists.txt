# Diagnostics v1 (STARK-0109): diag_core.c is pure and host-tested;
# stark_diag.c is the esp_timer sampler (target-only). L3.
# The self-test (STARK-0707): selftest_core.c (the runner) and selftest_report.c (the report
# formatter) are pure and host-tested; stark_diag.c binds the runner to the clock, the log and
# the event bus.
idf_component_register(
    SRCS
        "diag_core.c"
        "selftest_core.c"
        "selftest_report.c"
        "selftest_svc.c"
        "stark_diag.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
    PRIV_REQUIRES
        stark_event
        stark_hal
        stark_log
        esp_timer
        heap
        freertos
)
