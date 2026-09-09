/**
 * @file led.h
 * @brief WS2812B RGB LED status driver: the RMT shell
 *
 * What the strip shows, and the colour rule it obeys, is led_render.h.
 * This header is the driver around it: the RMT channel, the situation and
 * its clock, and the periodic tick.
 */

#ifndef KEYER_LED_H
#define KEYER_LED_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "led_render.h"

#ifdef __cplusplus
extern "C" {
#endif

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
 * @brief Set the situation the strip shows
 *
 * Idempotent: setting the situation already showing does not restart its
 * animation clock, so this may be called every tick from a mapping.
 *
 * @param situation What the box is doing now
 */
void led_set_situation(led_situation_t situation);

/**
 * @brief The situation the strip is showing
 */
led_situation_t led_get_situation(void);

/**
 * @brief Ask for attention: three flashes, then back to the situation
 *
 * The flashes are in the colour of whatever situation they land on, so an
 * event can never assert a colour the situation contradicts. Use it for a
 * change worth looking up for, such as the link coming up.
 */
void led_notify(void);

/**
 * @brief Update LED display (call periodically from bg_task)
 *
 * @param now_us Current timestamp in microseconds
 * @param dit DIT pin closed, read raw
 * @param dah DAH pin closed, read raw
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
