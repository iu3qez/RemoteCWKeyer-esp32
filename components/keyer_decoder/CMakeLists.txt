# keyer_decoder - CW Morse decoder
#
# Consumes keying_stream_t samples as best-effort consumer on Core 1.
# Decodes transmitted CW to text using adaptive timing classification.
# Pure logic, testable on host without hardware.

idf_component_register(
    SRCS
        "src/timing_classifier.c"
        "src/decoder.c"
    INCLUDE_DIRS "include"
    REQUIRES keyer_core keyer_morse
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wconversion
    -Wshadow
    -Wstrict-prototypes
)
