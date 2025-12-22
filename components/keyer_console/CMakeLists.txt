# keyer_console - Serial console
#
# Interactive command interface with history and tab completion.
# Uses USB Serial JTAG for input/output.

idf_component_register(
    SRCS
        "src/console.c"
        "src/parser.c"
        "src/commands.c"
        "src/line_buffer.c"
        "src/history.c"
        "src/completion.c"
    INCLUDE_DIRS "include"
    REQUIRES keyer_config keyer_logging driver
    PRIV_REQUIRES keyer_usb
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wconversion
    -Wshadow
)
