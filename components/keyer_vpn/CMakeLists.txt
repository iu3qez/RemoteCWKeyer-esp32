idf_component_register(
    SRCS "src/vpn.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_netif esp_event nvs_flash esp_timer keyer_logging keyer_config keyer_wifi
    PRIV_REQUIRES esp_wireguard
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wno-unused-parameter
    -Wconversion
    -Wsign-conversion
    -Wdouble-promotion
    -Wformat=2
    -Wformat-security
    -Wnull-dereference
    -Wstack-protector
    -fstack-protector-strong
    -Wstrict-overflow=2
)
