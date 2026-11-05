# Module bus (STARK-0501…0505, ADR-0021). The descriptor parser and discovery
# core are pure and host-tested. L3.
idf_component_register(
    SRCS
        "module_desc.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
)
