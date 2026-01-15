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
    uint32_t isr_blanking_us; /**< ISR blanking period in microseconds (0=disable ISR) */
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
 *
 * Called by RT task to check if DIT was pressed since last call.
 * Atomically clears the pending flag.
 *
 * @return true if DIT was pressed (ISR triggered), false otherwise
 * @note RT-safe, lock-free (atomic exchange)
 */
bool hal_gpio_consume_dit_press(void);

/**
 * @brief Check and consume pending DAH press from ISR
 *
 * Called by RT task to check if DAH was pressed since last call.
 * Atomically clears the pending flag.
 *
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
 * @brief Get ISR diagnostic statistics
 * @param dit_triggers Output: number of DIT ISR triggers
 * @param dah_triggers Output: number of DAH ISR triggers
 * @param blanking_rejects Output: number of edges rejected by blanking
 */
void hal_gpio_isr_get_stats(uint32_t *dit_triggers, uint32_t *dah_triggers,
                            uint32_t *blanking_rejects);

/**
 * @brief Update adaptive blanking period based on WPM
 *
 * Recalculates the optimal blanking period for the given WPM.
 * Formula: blanking = min(base, 40% of worst-case inter-element)
 * QRQ worst-case inter-element = dit_duration / 2
 *
 * @param wpm Speed in words per minute
 * @note Call when WPM configuration changes
 */
void hal_gpio_update_blanking_for_wpm(uint32_t wpm);

/**
 * @brief Check ISR health and recover from stuck interrupts
 *
 * Watchdog mechanism that detects and recovers from situations where
 * the blanking timer fails to re-enable interrupts. Should be called
 * from the RT task every tick.
 *
 * @param now_us Current timestamp from esp_timer_get_time()
 * @note RT-safe, call from RT task every tick
 */
void hal_gpio_isr_watchdog(int64_t now_us);

/**
 * @brief Get watchdog recovery count
 * @return Number of times watchdog had to recover a stuck interrupt
 */
uint32_t hal_gpio_get_watchdog_recoveries(void);

/**
 * @brief Get current effective blanking period
 * @return Current blanking period in microseconds (may differ from config if adaptive)
 */
uint32_t hal_gpio_get_effective_blanking_us(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_HAL_GPIO_H */
