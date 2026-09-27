# App Test (STARK-0105, STARK-0108): exercises the platform's app-facing
# services — the modal dialog, the worker helper and fault containment.
idf_component_register(SRCS "app_apptest.c" "app_apptest_worker.c"
    REQUIRES stark_app stark_ui stark_event stark_hal stark_log
    WHOLE_ARCHIVE)
