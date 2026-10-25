# NFC (STARK-0401…0405, ADR-0020). The NFC core is pure and host-tested;
# the PN532 port and the NFC task arrive in STARK-0402/0403. L3.
idf_component_register(
    SRCS
        "ndef.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        stark_err
)
