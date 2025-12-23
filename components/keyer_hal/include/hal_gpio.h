/**
 * @file hal_gpio.h
 * @brief GPIO hardware abstraction for paddles and TX
 */

#ifndef KEYER_HAL_GPIO_H
#define KEYER_HAL_GPIO_H

#include <stdint.h>
#include <stdbool.h>
#include "sample.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief GPIO configuration
 */
typedef struct {
    uint8_t dit_pin;       /**< DIT paddle GPIO pin */
    uint8_t dah_pin;       /**< DAH paddle GPIO pin */
    uint8_t tx_pin;        /**< TX output GPIO pin */
    bool active_low;       /**< Paddle inputs are active low */
    bool tx_active_high;   /**< TX output is active high */
} hal_gpio_config_t;

/**
 * @brief Default GPIO configuration
 */
#define HAL_GPIO_CONFIG_DEFAULT { \
    .dit_pin = 3, \
    .dah_pin = 4, \
    .tx_pin = 15, \
    .active_low = true, \
    .tx_active_high = true \
}

/**
 * @brief Initialize GPIO
 * @param config GPIO configuration
 */
void hal_gpio_init(const hal_gpio_config_t *config);

/**
 * @brief Read paddle state
 * @return Current paddle GPIO state
 */
gpio_state_t hal_gpio_read_paddles(void);

/**
 * @brief Set TX output
 * @param on true to key TX, false to unkey
 */
void hal_gpio_set_tx(bool on);

/**
 * @brief Get TX state
 * @return true if TX is keyed
 */
bool hal_gpio_get_tx(void);

/**
 * @brief Get current GPIO configuration
 * @return Current configuration
 */
hal_gpio_config_t hal_gpio_get_config(void);

/**
 * @brief Enable/disable verbose debug logging
 * @param enable true to enable debug prints
 */
void hal_gpio_set_debug(bool enable);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_HAL_GPIO_H */
