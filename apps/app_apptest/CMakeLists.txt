# App Test (STARK-0105): exercises the platform's app-facing services — the
# modal dialog now; STARK-0108 adds the worker helper and fault containment.
idf_component_register(SRCS "app_apptest.c"
    REQUIRES stark_app stark_ui stark_log
    WHOLE_ARCHIVE)
