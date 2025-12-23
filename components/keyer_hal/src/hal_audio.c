/**
 * @file hal_audio.c
 * @brief Audio HAL - ES8311 codec + TCA9555 PA control
 */

#include "hal_audio.h"

#ifdef CONFIG_IDF_TARGET

#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_io_expander.h"
#include "esp_io_expander_tca95xx_16bit.h"

static const char *TAG = "hal_audio";

/* State */
static hal_audio_config_t s_config;
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static esp_io_expander_handle_t s_io_expander = NULL;
static bool s_pa_enabled = false;
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

/**
 * @brief Initialize TCA9555 IO expander for PA control
 */
static esp_err_t init_io_expander(void) {
    if (!s_config.pa_via_io_expander) {
        ESP_LOGI(TAG, "PA via direct GPIO (not IO expander)");
        return ESP_OK;
    }

    esp_err_t ret = esp_io_expander_new_i2c_tca95xx_16bit(
        s_i2c_bus,
        ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000,  /* 0x20 */
        &s_io_expander
    );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCA9555 init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configure PA pin as output, initially OFF */
    uint32_t pa_mask = (1u << (uint32_t)s_config.pa_pin);
    ret = esp_io_expander_set_dir(s_io_expander, pa_mask, IO_EXPANDER_OUTPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set PA pin direction: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_io_expander_set_level(s_io_expander, pa_mask, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear PA pin: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "TCA9555 initialized, PA pin=%d", s_config.pa_pin);
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

    /* Step 2: Initialize TCA9555 IO expander */
    ret = init_io_expander();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "IO expander init failed, PA control unavailable");
        /* Continue - PA control is not critical */
    }

    /* TODO: Steps 3-4 in subsequent tasks */

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
    if (s_io_expander == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_pa_enabled == enable) {
        return ESP_OK;  /* No change needed */
    }

    uint32_t pa_mask = (1u << (uint32_t)s_config.pa_pin);
    uint8_t level = s_config.pa_active_high ? (enable ? 1 : 0) : (enable ? 0 : 1);

    esp_err_t ret = esp_io_expander_set_level(s_io_expander, pa_mask, level);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to %s PA: %s",
                 enable ? "enable" : "disable", esp_err_to_name(ret));
        return ret;
    }

    s_pa_enabled = enable;
    ESP_LOGI(TAG, "PA %s", enable ? "enabled" : "disabled");
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
