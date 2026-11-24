# I2C (STARK-0704, ADR-0023 §5): scan the shared I2C bus, name likely devices from a generic table,
# read, dump and (after a confirmation) write registers through stark_lab; a module ID EEPROM is
# never written. i2c_views.c is pure (host-tested).
idf_component_register(SRCS "app_i2c.c" "i2c_views.c"
    REQUIRES stark_app stark_ui stark_event stark_input stark_lab stark_log
    WHOLE_ARCHIVE)
