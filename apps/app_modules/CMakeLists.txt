# Modules (STARK-0505): a read-only view of what stark_module found (ADR-0021).
idf_component_register(SRCS "app_modules.c" "modules_view.c"
    REQUIRES stark_app stark_ui stark_event stark_input stark_module stark_log
    WHOLE_ARCHIVE)
