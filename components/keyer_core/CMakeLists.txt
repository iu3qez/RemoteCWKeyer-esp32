# keyer_core - Lock-free stream, sample, consumer, fault
#
# This is the heart of the keyer. All keying events flow through KeyingStream.
# No ESP-IDF dependencies - pure C with stdatomic.h

idf_component_register(
    SRCS
        "src/stream.c"
        "src/sample.c"
        "src/consumer.c"
        "src/fault.c"
    INCLUDE_DIRS "include"
    REQUIRES ""
)

# Strict warnings for core components
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wconversion
    -Wshadow
    -Wstrict-prototypes
)
