/* Placeholder - regenerate with embed_assets.py after frontend build */
#include "assets.h"
#include <stddef.h>

static const webui_asset_t s_assets[] = {
    {NULL, NULL, NULL, 0, false},
};

const webui_asset_t *webui_find_asset(const char *path) {
    (void)path;
    return NULL;
}

size_t webui_get_asset_count(void) {
    return 0;
}

const webui_asset_t *webui_get_assets(void) {
    return s_assets;
}
