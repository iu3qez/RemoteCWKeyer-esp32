/**
 * @file usb_cdc.h
 * @brief TinyUSB multi-CDC initialization and management
 *
 * CDC0: Console (interactive commands with immediate echo)
 * CDC1: Log (RT-safe ring buffer drain)
 * CDC2: Winkeyer3 (stub for future)
 */

#ifndef KEYER_USB_CDC_H
#define KEYER_USB_CDC_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** CDC interface numbers */
#define CDC_ITF_CONSOLE  0
#define CDC_ITF_LOG      1
#define CDC_ITF_WINKEYER 2  /* Future */

/** USB identification */
#define USB_VID          0x303A  /* Espressif */
#define USB_PID          0x8002  /* Custom device */
#define USB_SERIAL       "KEYER-IU3QEZ-001"

/**
 * @brief Initialize TinyUSB with multi-CDC
 *
 * Initializes TinyUSB, registers CDC callbacks, starts USB task.
 * Must be called early in app_main(), before console_task.
 *
 * @return ESP_OK on success
 */
esp_err_t usb_cdc_init(void);

/**
 * @brief Write to specific CDC interface
 *
 * @param itf Interface number (CDC_ITF_*)
 * @param data Data buffer
 * @param len Data length
 * @return Number of bytes written
 */
size_t usb_cdc_write(uint8_t itf, const uint8_t *data, size_t len);

/**
 * @brief Flush CDC interface TX buffer
 *
 * @param itf Interface number
 */
void usb_cdc_flush(uint8_t itf);

/**
 * @brief Check if CDC interface is connected
 *
 * @param itf Interface number
 * @return true if connected
 */
bool usb_cdc_connected(uint8_t itf);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_USB_CDC_H */
