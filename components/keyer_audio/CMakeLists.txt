# keyer_audio - Audio subsystem
#
# Sidetone generation, audio ring buffer, PTT control.
# Uses phase accumulator with 256-entry sine LUT.

idf_component_register(
    SRCS
        "src/sidetone.c"
        "src/sine_lut.c"
        "src/audio_buffer.c"
        "src/ptt.c"
        "src/audio_source.c"
    INCLUDE_DIRS "include"
    REQUIRES keyer_core
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wconversion
    -Wshadow
)
