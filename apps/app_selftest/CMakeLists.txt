# Self-Test (STARK-0707, ADR-0023 section 8): runs the registered checks on the app worker, walks the
# operator through the attested ones and exports the report, all through stark_diag. selftest_views.c
# is pure (host-tested).
idf_component_register(SRCS "app_selftest.c" "selftest_views.c"
    REQUIRES stark_app stark_ui stark_gfx stark_event stark_input stark_diag stark_buzzer stark_hal
             stark_log
    WHOLE_ARCHIVE)
