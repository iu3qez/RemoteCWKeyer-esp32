# keyer_console - Serial console
#
# Interactive command interface with history and tab completion.
# Uses USB Serial JTAG for input/output.

# Generate log_tags.h from ESP_LOG tags in codebase (runs at configure time)
execute_process(
    COMMAND python3 "${CMAKE_SOURCE_DIR}/scripts/gen_log_tags.py" "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE GEN_LOG_TAGS_RESULT
)
if(NOT GEN_LOG_TAGS_RESULT EQUAL 0)
    message(WARNING "Failed to generate log_tags.h")
endif()

idf_component_register(
    SRCS
        "src/console.c"
        "src/parser.c"
        "src/commands.c"
        "src/line_buffer.c"
        "src/history.c"
        "src/completion.c"
    INCLUDE_DIRS "include"
    REQUIRES keyer_config keyer_logging driver keyer_hal
    PRIV_REQUIRES keyer_usb
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wconversion
    -Wshadow
)
