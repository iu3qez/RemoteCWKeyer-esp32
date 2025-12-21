# keyer_iambic - Iambic FSM (Mode A/B)
#
# Pure logic, no I/O or allocation. Converts GPIO state to keying output.
# Fully testable on host without hardware.

idf_component_register(
    SRCS
        "src/iambic.c"
    INCLUDE_DIRS "include"
    REQUIRES keyer_core
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wconversion
    -Wshadow
    -Wstrict-prototypes
)
