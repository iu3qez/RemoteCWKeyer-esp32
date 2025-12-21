# Main application component
idf_component_register(
    SRCS
        "main.c"
        "rt_task.c"
        "bg_task.c"
    INCLUDE_DIRS "."
    REQUIRES
        keyer_core
        keyer_iambic
        keyer_audio
        keyer_logging
        keyer_console
        keyer_config
        keyer_hal
        freertos
        esp_timer
)
