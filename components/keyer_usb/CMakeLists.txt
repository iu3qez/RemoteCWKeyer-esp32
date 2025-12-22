idf_component_register(
    SRCS
        "src/usb_cdc.c"
        "src/usb_console.c"
        "src/usb_log.c"
        "src/usb_winkeyer.c"
        "src/usb_uf2.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        tinyusb
        esp_tinyuf2
        keyer_console
        keyer_logging
    PRIV_REQUIRES
        freertos
        esp_timer
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-parameter
)
