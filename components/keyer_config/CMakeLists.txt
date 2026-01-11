# keyer_config - Configuration system
#
# Generated from parameters.yaml by gen_config_c.py
# Provides atomic config struct and NVS persistence.

# Ensure include directory exists before idf_component_register validates it
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include")

idf_component_register(
    SRCS
        "src/config.c"
        "src/config_nvs.c"
        "src/config_console.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash
)

# Code generation from parameters.yaml
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(GEN_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/gen_config_c.py")
set(PARAMS_YAML "${CMAKE_SOURCE_DIR}/parameters.yaml")
set(GEN_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Generate config headers
add_custom_command(
    OUTPUT
        "${GEN_OUTPUT_DIR}/config.h"
        "${GEN_OUTPUT_DIR}/config_meta.h"
        "${GEN_OUTPUT_DIR}/config_nvs.h"
        "${GEN_OUTPUT_DIR}/config_console.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/config.c"
    COMMAND ${Python3_EXECUTABLE} ${GEN_SCRIPT} ${PARAMS_YAML} ${GEN_OUTPUT_DIR}
    DEPENDS ${PARAMS_YAML} ${GEN_SCRIPT}
    COMMENT "Generating C configuration code from parameters.yaml"
    VERBATIM
)

add_custom_target(generate_config DEPENDS "${GEN_OUTPUT_DIR}/config.h")
add_dependencies(${COMPONENT_LIB} generate_config)
