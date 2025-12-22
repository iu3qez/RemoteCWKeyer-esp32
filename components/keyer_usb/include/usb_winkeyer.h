/**
 * @file usb_winkeyer.h
 * @brief CDC2 Winkeyer3 emulation (stub)
 *
 * Future implementation for K1EL Winkeyer3 protocol.
 */

#ifndef KEYER_USB_WINKEYER_H
#define KEYER_USB_WINKEYER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize Winkeyer3 on CDC2 (stub)
 *
 * @return ESP_OK (always, stub implementation)
 */
esp_err_t usb_winkeyer_init(void);

/**
 * @brief Check if Winkeyer is enabled
 *
 * @return false (stub, not implemented)
 */
bool usb_winkeyer_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_USB_WINKEYER_H */
