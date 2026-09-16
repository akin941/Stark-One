idf_component_register(
    SRCS
        "stark_err.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
)

# Ensure exhaustive switch in stark_err_str() is enforced at compile time.
# Adding an enum value without a corresponding case will produce a build error.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wswitch-enum
    -Werror=switch-enum
)