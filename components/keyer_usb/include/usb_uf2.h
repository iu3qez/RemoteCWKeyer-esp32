/**
 * @file usb_uf2.h
 * @brief UF2 bootloader entry via command
 *
 * Uses esp_tinyuf2 component for USB firmware updates.
 */

#ifndef KEYER_USB_UF2_H
#define KEYER_USB_UF2_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize UF2 support
 *
 * Installs esp_tinyuf2 OTA handler.
 *
 * @return ESP_OK on success
 */
esp_err_t usb_uf2_init(void);

/**
 * @brief Enter UF2 bootloader mode
 *
 * Restarts device into UF2 mode for firmware update.
 * This function does not return.
 */
void usb_uf2_enter(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_USB_UF2_H */
