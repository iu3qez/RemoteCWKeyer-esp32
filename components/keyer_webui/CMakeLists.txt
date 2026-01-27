idf_component_register(
    SRCS
        "src/http_server.c"
        "src/api_config.c"
        "src/api_keyer.c"
        "src/api_system.c"
        "src/api_decoder.c"
        "src/api_timeline.c"
        "src/ws_server.c"
        "src/assets.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_http_server
        json
        keyer_config
        keyer_core
        keyer_cwnet
        keyer_decoder
        keyer_wifi
        keyer_text
)

# Strict compiler flags
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror
    -Wno-unused-parameter
    -Wconversion -Wsign-conversion
)

# Frontend build configuration
set(FRONTEND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/frontend")
set(FRONTEND_DIST "${FRONTEND_DIR}/dist")
set(FRONTEND_STAMP "${CMAKE_CURRENT_BINARY_DIR}/frontend.stamp")
set(ASSETS_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_assets.py")
set(ASSETS_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/src/assets.c")

# Glob frontend source files for dependency tracking
file(GLOB_RECURSE FRONTEND_SOURCES
    "${FRONTEND_DIR}/src/*.svelte"
    "${FRONTEND_DIR}/src/*.ts"
    "${FRONTEND_DIR}/src/*.js"
    "${FRONTEND_DIR}/src/*.css"
    "${FRONTEND_DIR}/index.html"
    "${FRONTEND_DIR}/package.json"
    "${FRONTEND_DIR}/vite.config.ts"
)

# Custom command: Build frontend with npm (stamp file in build dir, not dist)
add_custom_command(
    OUTPUT "${FRONTEND_STAMP}"
    COMMAND npm run build
    COMMAND ${CMAKE_COMMAND} -E touch "${FRONTEND_STAMP}"
    WORKING_DIRECTORY "${FRONTEND_DIR}"
    DEPENDS ${FRONTEND_SOURCES}
    COMMENT "Building frontend with Vite..."
)

# Custom command: Generate assets.c from dist/
add_custom_command(
    OUTPUT "${ASSETS_OUTPUT}"
    COMMAND python3 "${ASSETS_SCRIPT}" "${FRONTEND_DIST}" "${ASSETS_OUTPUT}"
    DEPENDS "${FRONTEND_STAMP}" "${ASSETS_SCRIPT}"
    COMMENT "Embedding frontend assets into assets.c..."
)

# Custom target to trigger asset generation
add_custom_target(frontend_assets DEPENDS "${ASSETS_OUTPUT}")

# Make component depend on frontend assets
add_dependencies(${COMPONENT_LIB} frontend_assets)
