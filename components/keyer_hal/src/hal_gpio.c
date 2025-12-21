/**
 * @file hal_gpio.c
 * @brief GPIO HAL implementation
 */

#include "hal_gpio.h"

#ifdef CONFIG_IDF_TARGET
#include "driver/gpio.h"

static hal_gpio_config_t s_config = HAL_GPIO_CONFIG_DEFAULT;
static bool s_tx_state = false;

void hal_gpio_init(const hal_gpio_config_t *config) {
    s_config = *config;

    /* Configure DIT input */
    gpio_config_t dit_conf = {
        .pin_bit_mask = (1ULL << config->dit_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = config->active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = config->active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&dit_conf);

    /* Configure DAH input */
    gpio_config_t dah_conf = {
        .pin_bit_mask = (1ULL << config->dah_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = config->active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = config->active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&dah_conf);

    /* Configure TX output */
    gpio_config_t tx_conf = {
        .pin_bit_mask = (1ULL << config->tx_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&tx_conf);

    /* Ensure TX is off */
    hal_gpio_set_tx(false);
}

gpio_state_t hal_gpio_read_paddles(void) {
    int dit_level = gpio_get_level(s_config.dit_pin);
    int dah_level = gpio_get_level(s_config.dah_pin);

    /* Invert if active low */
    bool dit_pressed = s_config.active_low ? (dit_level == 0) : (dit_level == 1);
    bool dah_pressed = s_config.active_low ? (dah_level == 0) : (dah_level == 1);

    return gpio_from_paddles(dit_pressed, dah_pressed);
}

void hal_gpio_set_tx(bool on) {
    s_tx_state = on;
    int level = s_config.tx_active_high ? (on ? 1 : 0) : (on ? 0 : 1);
    gpio_set_level(s_config.tx_pin, level);
}

bool hal_gpio_get_tx(void) {
    return s_tx_state;
}

#else
/* Host stub */

static hal_gpio_config_t s_config = HAL_GPIO_CONFIG_DEFAULT;
static gpio_state_t s_paddle_state = {0};
static bool s_tx_state = false;

void hal_gpio_init(const hal_gpio_config_t *config) {
    s_config = *config;
}

gpio_state_t hal_gpio_read_paddles(void) {
    return s_paddle_state;
}

void hal_gpio_set_tx(bool on) {
    s_tx_state = on;
}

bool hal_gpio_get_tx(void) {
    return s_tx_state;
}

/* Test helper to set paddle state */
void hal_gpio_test_set_paddles(bool dit, bool dah) {
    s_paddle_state = gpio_from_paddles(dit, dah);
}

#endif /* CONFIG_IDF_TARGET */
