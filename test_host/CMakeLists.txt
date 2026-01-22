cmake_minimum_required(VERSION 3.16)
project(keyer_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Strict compiler flags
add_compile_options(
    -Wall
    -Wextra
    -Werror
    -Wconversion
    -Wsign-conversion
    -Wdouble-promotion
    -Wformat=2
    -Wnull-dereference
    -g
)

# Host test defines
add_compile_definitions(
    HOST_TEST=1
    CONFIG_IDF_TARGET_ESP32S3=1
)

# Unity test framework (fetched automatically)
include(FetchContent)
FetchContent_Declare(
    unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG v2.5.2
)
FetchContent_MakeAvailable(unity)

# Include paths for components
set(COMPONENT_DIR ${CMAKE_SOURCE_DIR}/../components)

include_directories(
    ${COMPONENT_DIR}/keyer_core/include
    ${COMPONENT_DIR}/keyer_iambic/include
    ${COMPONENT_DIR}/keyer_audio/include
    ${COMPONENT_DIR}/keyer_logging/include
    ${COMPONENT_DIR}/keyer_console/include
    ${COMPONENT_DIR}/keyer_config/include
    ${COMPONENT_DIR}/keyer_decoder/include
    ${COMPONENT_DIR}/keyer_winkeyer/include
    ${CMAKE_SOURCE_DIR}/stubs
)

# Source files from components (platform-independent)
set(CORE_SOURCES
    ${COMPONENT_DIR}/keyer_core/src/stream.c
    ${COMPONENT_DIR}/keyer_core/src/sample.c
    ${COMPONENT_DIR}/keyer_core/src/fault.c
    ${COMPONENT_DIR}/keyer_core/src/consumer.c
)

set(IAMBIC_SOURCES
    ${COMPONENT_DIR}/keyer_iambic/src/iambic.c
    ${COMPONENT_DIR}/keyer_iambic/src/iambic_preset.c
)

set(AUDIO_SOURCES
    ${COMPONENT_DIR}/keyer_audio/src/sidetone.c
    ${COMPONENT_DIR}/keyer_audio/src/sine_lut.c
    ${COMPONENT_DIR}/keyer_audio/src/audio_buffer.c
    ${COMPONENT_DIR}/keyer_audio/src/ptt.c
)

set(LOGGING_SOURCES
    ${COMPONENT_DIR}/keyer_logging/src/log_stream.c
)

set(CONSOLE_SOURCES
    ${COMPONENT_DIR}/keyer_console/src/parser.c  # Only parser (no HAL dependency)
    # ${COMPONENT_DIR}/keyer_console/src/console.c  # Excluded: requires commands.c
    # ${COMPONENT_DIR}/keyer_console/src/commands.c  # Excluded: requires HAL (hal_gpio.h)
    # ${COMPONENT_DIR}/keyer_console/src/history.c  # Excluded: linked with console.c
    # ${COMPONENT_DIR}/keyer_console/src/completion.c  # Excluded: requires commands.c
)

set(CONFIG_SOURCES
    ${COMPONENT_DIR}/keyer_config/src/config.c
    ${COMPONENT_DIR}/keyer_config/src/config_console.c
)

set(DECODER_SOURCES
    ${COMPONENT_DIR}/keyer_decoder/src/morse_table.c
    ${COMPONENT_DIR}/keyer_decoder/src/timing_classifier.c
    ${COMPONENT_DIR}/keyer_decoder/src/decoder.c
)

set(WINKEYER_SOURCES
    ${COMPONENT_DIR}/keyer_winkeyer/src/winkeyer_morse.c
    ${COMPONENT_DIR}/keyer_winkeyer/src/morse_queue.c
    ${COMPONENT_DIR}/keyer_winkeyer/src/winkeyer_parser.c
)

# Test sources
set(TEST_SOURCES
    test_main.c
    test_stream.c
    test_iambic.c
    test_iambic_preset.c
    test_sidetone.c
    test_fault.c
    test_console_parser.c
    # test_config_console.c  # Excluded: requires full console system
    # test_history.c  # Excluded: requires console system
    # test_completion.c  # Excluded: requires commands.c
    test_rt_diag.c
    test_morse_table.c
    test_timing_classifier.c
    test_decoder.c
    test_morse.c
    test_winkeyer_parser.c
    stubs/esp_stubs.c
)

# Test runner executable
add_executable(test_runner
    ${TEST_SOURCES}
    ${CORE_SOURCES}
    ${IAMBIC_SOURCES}
    ${AUDIO_SOURCES}
    ${LOGGING_SOURCES}
    ${CONSOLE_SOURCES}
    ${CONFIG_SOURCES}
    ${DECODER_SOURCES}
    ${WINKEYER_SOURCES}
)

target_link_libraries(test_runner PRIVATE unity)

# Enable testing
enable_testing()
add_test(NAME keyer_tests COMMAND test_runner)
