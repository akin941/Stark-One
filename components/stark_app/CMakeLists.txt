# App descriptor, lifecycle and the launcher (STARK-0019); worker helper, join
# contract and fault containment (STARK-0108; app_worker.c is target-only). The explicit static
# registry itself lives at the composition root, main/app_registry.c (ADR-0016). Apps live in apps/app_<name>/ (EXTRA_COMPONENT_DIRS)
# and REQUIRE stark_app; this component never lists them.
idf_component_register(
    SRCS
        "app_catalog.c"
        "app_fault.c"
        "app_manager.c"
        "app_worker.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
        stark_ui
    PRIV_REQUIRES
        stark_event
        stark_log
        freertos
)
