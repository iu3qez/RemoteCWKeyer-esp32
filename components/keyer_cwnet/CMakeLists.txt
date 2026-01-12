# keyer_cwnet - CWNet protocol implementation
#
# Provides timestamp encoding/decoding for the CW streaming protocol.
# Pure logic, testable on host without hardware.

idf_component_register(
    SRCS
        "src/cwnet_timestamp.c"
    INCLUDE_DIRS "include"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wconversion
    -Wshadow
    -Wstrict-prototypes
)
