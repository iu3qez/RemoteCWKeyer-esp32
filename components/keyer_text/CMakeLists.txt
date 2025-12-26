# keyer_text - Text-to-Morse keyer
#
# Converts text strings to morse code and produces keying samples.
# Runs on Core 1 (bg_task).

idf_component_register(
    SRCS
        "src/text_keyer.c"
        "src/text_memory.c"
    INCLUDE_DIRS "include"
    REQUIRES keyer_core keyer_decoder keyer_config nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wconversion
    -Wshadow
    -Wstrict-prototypes
)
