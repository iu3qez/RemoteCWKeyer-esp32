/**
 * @file usb_console.h
 * @brief CDC0 console with immediate echo
 *
 * Uses TinyUSB callback for character-by-character processing
 * with immediate echo before pushing to console state machine.
 */

#ifndef KEYER_USB_CONSOLE_H
#define KEYER_USB_CONSOLE_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize USB console on CDC0
 *
 * Registers RX callback for immediate echo and character processing.
 *
 * @return ESP_OK on success
 */
esp_err_t usb_console_init(void);

/**
 * @brief Print string to console (CDC0)
 *
 * @param str String to print
 */
void usb_console_print(const char *str);

/**
 * @brief Print formatted string to console (CDC0)
 *
 * @param fmt Format string
 * @param ... Arguments
 */
void usb_console_printf(const char *fmt, ...);

/**
 * @brief Print prompt to console
 */
void usb_console_prompt(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_USB_CONSOLE_H */
