idf_component_register(
    SRCS
        "src/http_server.c"
        "src/api_config.c"
        "src/api_keyer.c"
        "src/api_system.c"
        "src/api_decoder.c"
        "src/api_timeline.c"
        "src/sse.c"
        "src/assets.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_http_server
        json
        keyer_config
        keyer_core
        keyer_decoder
)

# Strict compiler flags
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror
    -Wno-unused-parameter
    -Wconversion -Wsign-conversion
)
