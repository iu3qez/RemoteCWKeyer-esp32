/**
 * @file provisioning.h
 * @brief Initial device provisioning via AP + web UI
 *
 * Provides first-time setup for unconfigured devices:
 * - Creates open AP "CWKeyer-Setup"
 * - Serves web form for WiFi/callsign/theme configuration
 * - Saves to NVS and reboots
 *
 * DESIGN: Completely isolated from normal keyer operation.
 * This code runs INSTEAD of normal boot, never alongside it.
 */

#ifndef PROVISIONING_H
#define PROVISIONING_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if device needs provisioning
 *
 * Checks NVS for WiFi SSID. If empty or missing, provisioning is needed.
 *
 * @return true if provisioning needed (no WiFi configured)
 */
bool provisioning_is_needed(void);

/**
 * @brief Check if factory reset requested at boot
 *
 * Checks if both paddles are held for specified duration.
 * If triggered, clears WiFi SSID from NVS.
 *
 * @param dit_gpio DIT paddle GPIO number
 * @param dah_gpio DAH paddle GPIO number
 * @param hold_ms Duration to hold (typically 5000ms)
 * @return true if factory reset triggered (SSID cleared)
 */
bool provisioning_check_factory_reset(gpio_num_t dit_gpio, gpio_num_t dah_gpio, uint32_t hold_ms);

/**
 * @brief Start provisioning mode
 *
 * Creates AP, starts web server, waits for configuration.
 * After successful config, saves to NVS and reboots.
 *
 * @note This function does NOT return. Device reboots after config.
 */
void provisioning_start(void);

#ifdef __cplusplus
}
#endif

#endif /* PROVISIONING_H */
