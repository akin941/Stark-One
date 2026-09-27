# App descriptor, explicit static registry (ADR-0009), lifecycle and the
# launcher (STARK-0019). Apps live in apps/app_<name>/ (EXTRA_COMPONENT_DIRS)
# and REQUIRE stark_app; this component never lists them.
idf_component_register(
    SRCS
        "app_registry.c"
        "app_catalog.c"
        "app_manager.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
        stark_ui
    PRIV_REQUIRES
        stark_log
)
