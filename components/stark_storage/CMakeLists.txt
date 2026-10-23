# Storage service (STARK-0201, ADR-0018): storage_core.c is pure and
# host-tested; stark_storage.c is the storage task (target-only). L3.
idf_component_register(
    SRCS
        "storage_core.c"
        "stark_storage.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
        stark_storage_medium
    PRIV_REQUIRES
        stark_board
        stark_event
        stark_log
        esp_timer
        freertos
)
