# RustRemoteCWKeyer - Pure C Implementation
# ESP-IDF Project CMakeLists.txt
#
# Build: idf.py build
# Flash: idf.py flash monitor

cmake_minimum_required(VERSION 3.16)

# Component directories
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/components"
)

# Include ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(keyer_c C)

# Project-wide compiler flags for quality
# Applied to all components via idf_build_set_property
idf_build_set_property(COMPILE_OPTIONS
    "-Wall"
    "-Wextra"
    "-Wconversion"
    "-Wshadow"
    "-Wdouble-promotion"
    "-Wformat=2"
    "-Wundef"
    "-Wstrict-prototypes"
    "-Wmissing-prototypes"
    "-Wno-unused-parameter"
    "-fstack-protector-strong"
    APPEND
)

# Treat warnings as errors in release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    idf_build_set_property(COMPILE_OPTIONS "-Werror" APPEND)
endif()
