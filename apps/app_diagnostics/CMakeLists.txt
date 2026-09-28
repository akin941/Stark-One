# Diagnostics (STARK-0109): stark_diag's snapshot and task table, live.
idf_component_register(SRCS "app_diagnostics.c"
    REQUIRES stark_app stark_ui stark_gfx stark_event stark_diag stark_log
    WHOLE_ARCHIVE)
