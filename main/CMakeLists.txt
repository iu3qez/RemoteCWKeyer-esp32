# Main application component
idf_component_register(
    SRCS
        "main.c"
        "rt_task.c"
        "bg_task.c"
        "audio_test.c"
    INCLUDE_DIRS "."
    REQUIRES
        keyer_core
        keyer_iambic
        keyer_audio
        keyer_logging
        keyer_console
        keyer_usb
        keyer_config
        keyer_hal
        keyer_decoder
        keyer_text
        keyer_led
        keyer_wifi
        keyer_webui
        keyer_cwnet
        provisioning
        freertos
        esp_timer
)
