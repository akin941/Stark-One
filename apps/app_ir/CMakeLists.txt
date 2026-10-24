# IR (STARK-0305/0306): receive, save, browse, replay — own devices only.
idf_component_register(SRCS "app_ir.c" "ir_view.c" "ir_save.c" "ir_browse.c" "ir_send.c"
    REQUIRES stark_app stark_ui stark_event stark_input stark_ir stark_storage stark_log
    WHOLE_ARCHIVE)
