/**
 * @file hal_es8311.c
 * @brief ES8311 codec driver implementation (stub)
 */

#include "hal_es8311.h"

#ifdef CONFIG_IDF_TARGET

/* TODO: Implement ES8311 I2C driver */

esp_err_t hal_es8311_init(const hal_es8311_config_t *config) {
    (void)config;
    return ESP_OK;
}

esp_err_t hal_es8311_set_volume(uint8_t volume) {
    (void)volume;
    return ESP_OK;
}

esp_err_t hal_es8311_set_mute(bool mute) {
    (void)mute;
    return ESP_OK;
}

esp_err_t hal_es8311_power_down(void) {
    return ESP_OK;
}

#endif /* CONFIG_IDF_TARGET */
