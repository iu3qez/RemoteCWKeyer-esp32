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
    uint32_t isr_blanking_us; /**< ISR blanking period in µs (0 = disable ISR, use polling only) */
} hal_gpio_config_t;

/**
 * @brief Default GPIO configuration
 */
#define HAL_GPIO_CONFIG_DEFAULT { \
    .dit_pin = 4, \
    .dah_pin = 5, \
    .tx_pin = 6, \
    .active_low = true, \
    .tx_active_high = true, \
    .isr_blanking_us = 1500 \
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
 * @brief Check and consume pending DIT press from ISR
 * @return true if DIT was pressed (ISR triggered), false otherwise
 * @note RT-safe, lock-free (atomic exchange)
 */
bool hal_gpio_consume_dit_press(void);

/**
 * @brief Check and consume pending DAH press from ISR
 * @return true if DAH was pressed (ISR triggered), false otherwise
 * @note RT-safe, lock-free (atomic exchange)
 */
bool hal_gpio_consume_dah_press(void);

/**
 * @brief Check if ISR mode is enabled
 * @return true if ISR-based paddle detection is active
 */
bool hal_gpio_isr_enabled(void);

/**
 * @brief Process ISR blanking timers (call from RT task every tick)
 *
 * This function handles the ISR-safe handoff pattern:
 * - Starts blanking timers requested by ISR (safe in task context)
 * - Implements watchdog recovery for stuck interrupts
 *
 * @param now_us Current timestamp from esp_timer_get_time()
 * @note Must be called from task context (not ISR)
 */
void hal_gpio_isr_tick(int64_t now_us);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_HAL_GPIO_H */
