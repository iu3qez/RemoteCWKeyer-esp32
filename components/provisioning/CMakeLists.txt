# Provisioning component - Initial device setup via AP
#
# Isolated from normal keyer operation. Only runs if WiFi not configured.
# Intentional code duplication for fault isolation.

idf_component_register(
    SRCS
        "src/provisioning.c"
        "src/provisioning_wifi.c"
        "src/provisioning_http.c"
        "src/provisioning_html.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_wifi esp_http_server esp_netif driver esp_timer keyer_led
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wno-unused-parameter
    -Wconversion
    -Wsign-conversion
)
