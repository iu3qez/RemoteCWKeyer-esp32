#ifndef KEYER_WEBUI_ASSETS_H
#define KEYER_WEBUI_ASSETS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Embedded asset descriptor
 */
typedef struct {
    const char *path;           /**< URL path (e.g., "/index.html") */
    const char *content_type;   /**< MIME type */
    const uint8_t *data;        /**< Gzip-compressed data */
    size_t length;              /**< Data length */
    bool gzipped;               /**< Always true for embedded assets */
} webui_asset_t;

/**
 * @brief Find asset by path
 * @param path URL path
 * @return Asset pointer or NULL
 */
const webui_asset_t *webui_find_asset(const char *path);

/**
 * @brief Get asset count
 * @return Number of embedded assets
 */
size_t webui_get_asset_count(void);

/**
 * @brief Get all assets
 * @return Array of assets (NULL-terminated)
 */
const webui_asset_t *webui_get_assets(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_WEBUI_ASSETS_H */
