# Storage service (STARK-0201/0203, ADR-0018): storage_core.c, storage_path.c
# (pure) and storage_file.c + storage_io_posix.c are host-tested;
# stark_storage.c is the storage task (target-only). L3.
idf_component_register(
    SRCS
        "storage_core.c"
        "storage_path.c"
        "storage_file.c"
        "storage_io_posix.c"
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
        vfs
)
