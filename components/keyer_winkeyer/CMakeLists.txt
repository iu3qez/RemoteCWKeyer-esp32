# keyer_winkeyer - Winkeyer v3 protocol server
#
# Lock-free morse queue and ASCII-to-Morse encoding for
# USB CDC Winkeyer protocol support.
#
# No ESP-IDF dependencies - pure C with stdatomic.h

idf_component_register(
    SRCS
        "src/winkeyer.c"
        "src/winkeyer_morse.c"
        "src/morse_queue.c"
        "src/winkeyer_parser.c"
    INCLUDE_DIRS "include"
    REQUIRES ""
)

# Strict warnings for safety-critical components
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wconversion
    -Wshadow
    -Wstrict-prototypes
)
