/**
 * @file hal_gpio.c
 * @brief GPIO HAL implementation
 */

#include "hal_gpio.h"

#ifdef CONFIG_IDF_TARGET
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "hal_gpio";
static hal_gpio_config_t s_config = HAL_GPIO_CONFIG_DEFAULT;
static bool s_tx_state = false;

/**
 * @brief Reset a GPIO pin to pure GPIO function
 *
 * Some GPIOs (especially GPIO4 on ESP32-S3) may be claimed by the bootloader
 * for JTAG or other functions. This helper forcibly disconnects the pin from
 * any peripheral and configures it as a simple GPIO input with pull-up.
 */
static void force_gpio_reset(gpio_num_t pin) {
    gpio_reset_pin(pin);
    gpio_iomux_out(pin, 1, false);        /* func=1 is GPIO, no invert */
    gpio_iomux_in(pin, 0x100);            /* Disconnect from any input signal */
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
}

void hal_gpio_init(const hal_gpio_config_t *config) {
    s_config = *config;

    ESP_LOGI(TAG, "Configuring GPIO: DIT=%d, DAH=%d, TX=%d, active_low=%d",
             config->dit_pin, config->dah_pin, config->tx_pin, config->active_low);

    /*
     * CRITICAL: Force reset paddle pins before gpio_config().
     * On ESP32-S3, GPIO4 (and potentially others) may be held by the bootloader
     * for JTAG functions. A simple gpio_config() is not sufficient to reclaim them.
     */
    force_gpio_reset((gpio_num_t)config->dit_pin);
    force_gpio_reset((gpio_num_t)config->dah_pin);

    /* Configure DIT input */
    gpio_config_t dit_conf = {
        .pin_bit_mask = (1ULL << config->dit_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&dit_conf);
    ESP_LOGI(TAG, "DIT GPIO%d config: %s (pull_up=%d)",
             config->dit_pin, esp_err_to_name(err), dit_conf.pull_up_en);

    /* Configure DAH input */
    gpio_config_t dah_conf = {
        .pin_bit_mask = (1ULL << config->dah_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&dah_conf);
    ESP_LOGI(TAG, "DAH GPIO%d config: %s (pull_up=%d)",
             config->dah_pin, esp_err_to_name(err), dah_conf.pull_up_en);

    /* Configure TX output */
    gpio_config_t tx_conf = {
        .pin_bit_mask = (1ULL << config->tx_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&tx_conf);
    ESP_LOGI(TAG, "TX GPIO%d config: %s", config->tx_pin, esp_err_to_name(err));

    /* Ensure TX is off */
    hal_gpio_set_tx(false);

    /* Read initial levels */
    int dit_level = gpio_get_level(config->dit_pin);
    int dah_level = gpio_get_level(config->dah_pin);
    ESP_LOGI(TAG, "Initial levels: DIT=%d, DAH=%d", dit_level, dah_level);
}

static bool s_debug_once = true;

gpio_state_t hal_gpio_read_paddles(void) {
    int dit_level = gpio_get_level((gpio_num_t)s_config.dit_pin);
    int dah_level = gpio_get_level((gpio_num_t)s_config.dah_pin);

    /* Invert if active low: level=0 means pressed */
    bool dit_pressed = s_config.active_low ? (dit_level == 0) : (dit_level != 0);
    bool dah_pressed = s_config.active_low ? (dah_level == 0) : (dah_level != 0);

    if (s_debug_once) {
        ESP_LOGI(TAG, "read_paddles: pins=%d,%d levels=%d,%d active_low=%d pressed=%d,%d",
                 s_config.dit_pin, s_config.dah_pin,
                 dit_level, dah_level, s_config.active_low,
                 dit_pressed, dah_pressed);
        s_debug_once = false;
    }

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

hal_gpio_config_t hal_gpio_get_config(void) {
    return s_config;
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

hal_gpio_config_t hal_gpio_get_config(void) {
    return s_config;
}

/* Test helper to set paddle state */
void hal_gpio_test_set_paddles(bool dit, bool dah) {
    s_paddle_state = gpio_from_paddles(dit, dah);
}

#endif /* CONFIG_IDF_TARGET */
