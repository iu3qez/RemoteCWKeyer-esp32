cmake_minimum_required(VERSION 3.16)
project(cwnetd_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Same strict flags as test_host/CMakeLists.txt (R17: one bar, Linux and
# macOS both build the daemon with it).
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

# host/platform: non-blocking TCP sockets and a monotonic clock for anything
# that runs on the station PC. No esp_timer, no test_host stub — independent
# of the firmware build.
add_library(host_platform STATIC
    platform/sock.c
    platform/clock.c
)
target_include_directories(host_platform PUBLIC platform)

enable_testing()

add_executable(loopback_test tests/loopback_test.c)
target_link_libraries(loopback_test PRIVATE host_platform)

add_test(NAME loopback_test COMMAND loopback_test)
