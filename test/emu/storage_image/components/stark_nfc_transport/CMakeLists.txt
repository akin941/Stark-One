# Test substitute for components/stark_nfc_transport (ADR-0020 §3): the
# test-only fake PN532 behind the production API. Test image only.
idf_component_register(
    SRCS
        "nfc_transport_fake.c"
        "../../../../fake/fake_pn532.c"
    INCLUDE_DIRS
        "../../../../../components/stark_nfc_transport/include"
    PRIV_INCLUDE_DIRS
        "../../../../fake"
    REQUIRES
        stark_err
)
