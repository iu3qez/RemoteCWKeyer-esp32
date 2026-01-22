# keyer_morse - Unified ITU Morse code table
#
# Provides both encoding (char -> bit pattern) and decoding (pattern -> char).
# Used by keyer_decoder and keyer_winkeyer.
# No ESP-IDF dependencies - pure C.

idf_component_register(
    SRCS
        "src/morse_table.c"
    INCLUDE_DIRS "include"
    REQUIRES ""
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wconversion
    -Wshadow
    -Wstrict-prototypes
)
