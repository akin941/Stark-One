# Test substitute for components/stark_subghz_transport (ADR-0022 §3): the
# test-only fake CC1101 behind the production API. Test image only.
idf_component_register(
    SRCS
        "subghz_transport_fake.c"
        "../../../../fake/fake_cc1101.c"
    INCLUDE_DIRS
        "../../../../../components/stark_subghz_transport/include"
    PRIV_INCLUDE_DIRS
        "../../../../fake"
    REQUIRES
        stark_err
        stark_module
    PRIV_REQUIRES
        freertos
)
