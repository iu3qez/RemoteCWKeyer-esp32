/**
 * @file usb_log.h
 * @brief CDC1 log drain with filtering
 *
 * Drains RT-safe log ring buffer to CDC1.
 * Supports tag/level filtering via console commands.
 */

#ifndef KEYER_USB_LOG_H
#define KEYER_USB_LOG_H

#include "esp_err.h"
#include "rt_log.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize USB log on CDC1
 *
 * @return ESP_OK on success
 */
esp_err_t usb_log_init(void);

/**
 * @brief USB log drain task
 *
 * Drains RT and BG log streams to CDC1.
 * Runs on Core 1, low priority.
 *
 * @param arg Unused
 */
void usb_log_task(void *arg);

/**
 * @brief Set global log level filter
 *
 * @param level Minimum level to output
 */
void usb_log_set_level(log_level_t level);

/**
 * @brief Set log level for specific tag
 *
 * @param tag Tag name (max 15 chars)
 * @param level Minimum level for this tag
 * @return ESP_OK on success, ESP_ERR_NO_MEM if too many tags
 */
esp_err_t usb_log_set_tag_level(const char *tag, log_level_t level);

/**
 * @brief Get current global log level
 *
 * @return Current level
 */
log_level_t usb_log_get_level(void);

/**
 * @brief Clear all tag-specific filters
 */
void usb_log_clear_filters(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_USB_LOG_H */
