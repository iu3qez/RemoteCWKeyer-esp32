# keyer_wifi - WiFi connectivity manager
#
# STA connection with AP fallback.
# Atomic state for LED integration.

idf_component_register(
    SRCS
        "src/wifi.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_event nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wconversion
    -Wshadow
)
