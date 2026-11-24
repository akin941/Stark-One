# GPIO (STARK-0702, ADR-0023): read, drive, pulse and PWM the pins the lab offers (stark_lab),
# every pin released to hi-Z when the app stops. gpio_views.c is pure (host-tested).
idf_component_register(SRCS "app_gpio.c" "gpio_views.c"
    REQUIRES stark_app stark_ui stark_event stark_input stark_lab stark_module stark_log
    WHOLE_ARCHIVE)
