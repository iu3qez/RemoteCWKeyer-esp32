# Host tests for keyer components
#
# Build: cmake -B build && cmake --build build
# Run: ./build/test_runner
# With sanitizers: cmake -B build -DCMAKE_C_FLAGS="-fsanitize=address,undefined"

cmake_minimum_required(VERSION 3.16)
project(keyer_test_host C)

# Unity test framework (fetched automatically)
include(FetchContent)
FetchContent_Declare(
    unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG v2.5.2
)
FetchContent_MakeAvailable(unity)

# Components under test
set(COMPONENT_DIR ${CMAKE_SOURCE_DIR}/../components)

# Test runner executable
add_executable(test_runner
    test_main.c
    test_console_parser.c
    test_console_commands.c

    # Console component sources (compiled for host)
    ${COMPONENT_DIR}/keyer_console/src/parser.c
    ${COMPONENT_DIR}/keyer_console/src/commands.c
)

target_include_directories(test_runner PRIVATE
    ${COMPONENT_DIR}/keyer_console/include
    ${COMPONENT_DIR}/keyer_config/include
    ${COMPONENT_DIR}/keyer_logging/include
)

target_link_libraries(test_runner PRIVATE unity)

# Strict compiler flags
target_compile_options(test_runner PRIVATE
    -Wall
    -Wextra
    -Wconversion
    -Wshadow
    -Wno-unused-parameter
)

# Enable testing
enable_testing()
add_test(NAME console_tests COMMAND test_runner)
