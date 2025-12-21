# keyer_logging - RT-safe non-blocking logging
#
# Lock-free log stream with ~100-200ns push latency.
# UART drain task runs on Core 1.

idf_component_register(
    SRCS
        "src/log_stream.c"
        "src/uart_logger.c"
    INCLUDE_DIRS "include"
    REQUIRES keyer_core driver esp_driver_uart
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wconversion
    -Wshadow
)
