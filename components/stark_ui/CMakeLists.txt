# Screen stack, status bar, damage-driven rendering, UI loop (STARK-0017;
# multi-rect damage set + frame statistics, STARK-0101);
# list menu widget + pure, host-tested menu model (STARK-0018).
idf_component_register(
    SRCS
        "ui_stack.c"
        "ui_damage.c"
        "ui_dialog.c"
        "ui_dialog_layout.c"
        "ui_statusbar.c"
        "ui_menu.c"
        "ui_menu_model.c"
        "ui_format.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
        stark_event
        stark_gfx
    PRIV_REQUIRES
        stark_buzzer
        stark_display
        stark_hal
        stark_input
        stark_log
)
