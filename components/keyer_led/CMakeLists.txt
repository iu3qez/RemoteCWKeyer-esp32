# keyer_led - WS2812B RGB LED driver

idf_component_register(
    SRCS
        "src/led.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_rmt esp_timer
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wconversion
    -Wshadow
)
