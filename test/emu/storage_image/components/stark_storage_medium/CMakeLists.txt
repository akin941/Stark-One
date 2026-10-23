# Test substitute for components/stark_storage_medium (ADR-0018 §3): a
# wear-levelled FAT volume on the `emufat` flash partition. Test image only.
idf_component_register(
    SRCS
        "medium_flashfat.c"
    INCLUDE_DIRS
        "../../../../../components/stark_storage_medium/include"
    REQUIRES
        stark_err
    PRIV_REQUIRES
        fatfs
        wear_levelling
)
