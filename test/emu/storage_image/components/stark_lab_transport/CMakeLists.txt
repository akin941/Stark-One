# Test substitute for components/stark_lab_transport (ADR-0023 §2): the test-only pad
# model behind the production API. Test image only.
idf_component_register(
    SRCS
        "lab_transport_fake.c"
        "../../../../fake/fake_lab.c"
    INCLUDE_DIRS
        "../../../../../components/stark_lab_transport/include"
    PRIV_INCLUDE_DIRS
        "../../../../fake"
    REQUIRES
        stark_err
        stark_board
        stark_module
)
