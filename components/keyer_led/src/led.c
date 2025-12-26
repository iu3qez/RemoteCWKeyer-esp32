/**
 * @file led.c
 * @brief WS2812B RGB LED driver implementation (stub)
 */

#include "led.h"

esp_err_t led_init(const led_config_t *config)
{
    (void)config;
    return ESP_OK;
}

void led_deinit(void)
{
}

void led_set_state(led_state_t state)
{
    (void)state;
}

led_state_t led_get_state(void)
{
    return LED_STATE_OFF;
}

void led_tick(int64_t now_us, bool dit, bool dah)
{
    (void)now_us;
    (void)dit;
    (void)dah;
}

void led_set_brightness(uint8_t brightness, uint8_t brightness_dim)
{
    (void)brightness;
    (void)brightness_dim;
}

bool led_is_initialized(void)
{
    return false;
}
