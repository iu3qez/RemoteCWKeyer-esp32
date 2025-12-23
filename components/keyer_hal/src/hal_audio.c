/**
 * @file hal_audio.c
 * @brief Audio HAL - ES8311 codec + TCA9555 PA control
 */

#include "hal_audio.h"

#ifdef CONFIG_IDF_TARGET

#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

static const char *TAG = "hal_audio";

/* State */
static hal_audio_config_t s_config;
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static bool s_audio_available = false;

/**
 * @brief Initialize I2C bus
 */
static esp_err_t init_i2c(void) {
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = s_config.i2c_sda_pin,
        .scl_io_num = s_config.i2c_scl_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C bus initialized (SDA=%d, SCL=%d)",
             s_config.i2c_sda_pin, s_config.i2c_scl_pin);
    return ESP_OK;
}

esp_err_t hal_audio_init(const hal_audio_config_t *config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_config = *config;
    s_audio_available = false;

    /* Step 1: Initialize I2C bus */
    esp_err_t ret = init_i2c();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Audio init failed at I2C, continuing without audio");
        return ESP_OK;  /* Don't block boot */
    }

    /* TODO: Steps 2-4 in subsequent tasks */

    s_audio_available = true;
    ESP_LOGI(TAG, "Audio HAL initialized (sample_rate=%lu)",
             (unsigned long)s_config.sample_rate);
    return ESP_OK;
}

size_t hal_audio_write(const int16_t *samples, size_t count) {
    if (!s_audio_available) {
        return count;  /* Silently discard */
    }
    (void)samples;
    /* TODO: I2S write in Task 5 */
    return count;
}

esp_err_t hal_audio_set_volume(uint8_t volume_percent) {
    if (!s_audio_available) {
        return ESP_OK;
    }
    (void)volume_percent;
    /* TODO: Codec volume in Task 6 */
    return ESP_OK;
}

esp_err_t hal_audio_set_pa(bool enable) {
    if (!s_audio_available) {
        return ESP_OK;
    }
    (void)enable;
    /* TODO: TCA9555 PA in Task 4 */
    return ESP_OK;
}

void hal_audio_start(void) {
    /* TODO: I2S enable in Task 5 */
}

void hal_audio_stop(void) {
    /* TODO: I2S disable in Task 5 */
}

bool hal_audio_is_available(void) {
    return s_audio_available;
}

#else
/* Host stub */

static bool s_available = false;

esp_err_t hal_audio_init(const hal_audio_config_t *config) {
    (void)config;
    s_available = true;
    return ESP_OK;
}

size_t hal_audio_write(const int16_t *samples, size_t count) {
    (void)samples;
    return count;
}

esp_err_t hal_audio_set_volume(uint8_t volume_percent) {
    (void)volume_percent;
    return ESP_OK;
}

esp_err_t hal_audio_set_pa(bool enable) {
    (void)enable;
    return ESP_OK;
}

void hal_audio_start(void) {}
void hal_audio_stop(void) {}
bool hal_audio_is_available(void) { return s_available; }

#endif /* CONFIG_IDF_TARGET */
