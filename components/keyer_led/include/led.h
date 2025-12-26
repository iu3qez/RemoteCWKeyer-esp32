/**
 * @file led.h
 * @brief WS2812B RGB LED status driver
 *
 * Displays keyer state through RGB LEDs:
 * - Boot/connecting: orange breathing
 * - Connected: green flash then dim
 * - AP mode: alternating orange/blue
 * - Degraded: dim yellow
 * - Keying: DIT/DAH/squeeze indication
 */

#ifndef KEYER_LED_H
#define KEYER_LED_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LED state machine states
 */
typedef enum {
    LED_STATE_OFF = 0,          /**< LEDs off */
    LED_STATE_BOOT,             /**< Boot: orange breathing */
    LED_STATE_WIFI_CONNECTING,  /**< Connecting: orange breathing */
    LED_STATE_WIFI_FAILED,      /**< Brief red flash */
    LED_STATE_DEGRADED,         /**< Dim yellow steady */
    LED_STATE_AP_MODE,          /**< Alternating orange/blue */
    LED_STATE_CONNECTED,        /**< Green flash sequence */
    LED_STATE_IDLE,             /**< Dim green steady */
} led_state_t;

/**
 * @brief LED configuration
 */
typedef struct {
    uint8_t gpio_data;       /**< WS2812B data GPIO */
    uint8_t led_count;       /**< Number of LEDs */
    uint8_t brightness;      /**< Master brightness 0-100 */
    uint8_t brightness_dim;  /**< Dim brightness 0-100 */
} led_config_t;

/**
 * @brief Default LED configuration
 */
#define LED_CONFIG_DEFAULT { \
    .gpio_data = 38, \
    .led_count = 7, \
    .brightness = 50, \
    .brightness_dim = 10 \
}

/**
 * @brief Initialize LED driver
 *
 * @param config LED configuration
 * @return ESP_OK on success, error code on failure
 *
 * @note Non-fatal: keyer continues if init fails
 */
esp_err_t led_init(const led_config_t *config);

/**
 * @brief Deinitialize LED driver
 */
void led_deinit(void);

/**
 * @brief Set LED state
 *
 * @param state New state
 */
void led_set_state(led_state_t state);

/**
 * @brief Get current LED state
 *
 * @return Current state
 */
led_state_t led_get_state(void);

/**
 * @brief Update LED display (call periodically from bg_task)
 *
 * Handles:
 * - State machine animations (breathing, flashing)
 * - Keying overlay (DIT/DAH/squeeze)
 *
 * @param now_us Current timestamp in microseconds
 * @param dit DIT paddle pressed
 * @param dah DAH paddle pressed
 */
void led_tick(int64_t now_us, bool dit, bool dah);

/**
 * @brief Update brightness from config
 *
 * @param brightness Master brightness 0-100
 * @param brightness_dim Dim brightness 0-100
 */
void led_set_brightness(uint8_t brightness, uint8_t brightness_dim);

/**
 * @brief Check if LED driver is initialized
 *
 * @return true if initialized and ready
 */
bool led_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_LED_H */
