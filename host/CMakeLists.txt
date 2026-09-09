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
    # keyer_cwnet's module brief promises these two for the whole component,
    # and two of its files are compiled only here.
    -Wshadow
    -Wstrict-prototypes
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

# cwnetd: the station daemon (U6). The core it drives — cwnet_server.c and
# cwnet_play.c — is host-only by KTD2 and deliberately absent from the
# ESP-IDF component's SRCS, so it is listed here by hand. cwnet_client.c is
# NOT linked: the daemon is the other end of the wire (KTD10).
set(CWNET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../components/keyer_cwnet)

add_library(cwnet_core STATIC
    ${CWNET_DIR}/src/cwnet_frame.c
    ${CWNET_DIR}/src/cwnet_ping.c
    ${CWNET_DIR}/src/cwnet_timestamp.c
    ${CWNET_DIR}/src/cwnet_play.c
    ${CWNET_DIR}/src/cwnet_server.c
)
target_include_directories(cwnet_core PUBLIC ${CWNET_DIR}/include)

add_executable(cwnetd
    cwnetd/main.c
    cwnetd/key_output.c
)
target_link_libraries(cwnetd PRIVATE host_platform cwnet_core)
