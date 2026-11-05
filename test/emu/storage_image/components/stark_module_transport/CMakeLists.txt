# Test substitute for components/stark_module_transport (ADR-0021 §6): the
# test-only fake 24C02 behind the production API. Test image only.
idf_component_register(
    SRCS
        "module_transport_fake.c"
        "../../../../fake/fake_24c02.c"
    INCLUDE_DIRS
        "../../../../../components/stark_module_transport/include"
    PRIV_INCLUDE_DIRS
        "../../../../fake"
        "../../../../../components/stark_module/include"
    REQUIRES
        stark_err
)
