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
static i2s_chan_handle_t s_i2s_tx = NULL;
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

/**
 * @brief Initialize I2S for audio output
 */
static esp_err_t init_i2s(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_i2s_tx, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S channel create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(s_config.sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = s_config.i2s_mclk_pin,
            .bclk = s_config.i2s_bclk_pin,
            .ws = s_config.i2s_lrck_pin,
            .dout = s_config.i2s_dout_pin,
            .din = GPIO_NUM_NC,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    ret = i2s_channel_init_std_mode(s_i2s_tx, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S std mode init failed: %s", esp_err_to_name(ret));
        i2s_del_channel(s_i2s_tx);
        s_i2s_tx = NULL;
        return ret;
    }

    ret = i2s_channel_enable(s_i2s_tx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S enable failed: %s", esp_err_to_name(ret));
        i2s_del_channel(s_i2s_tx);
        s_i2s_tx = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "I2S initialized (MCLK=%d, BCLK=%d, LRCK=%d, DOUT=%d, rate=%lu)",
             s_config.i2s_mclk_pin, s_config.i2s_bclk_pin,
             s_config.i2s_lrck_pin, s_config.i2s_dout_pin,
             (unsigned long)s_config.sample_rate);
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

    /* Step 3: Initialize I2S */
    ret = init_i2s();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S init failed, continuing without audio");
        return ESP_OK;  /* Don't block boot */
    }

    /* TODO: Step 4 in subsequent task */

    s_audio_available = true;
    ESP_LOGI(TAG, "Audio HAL initialized (sample_rate=%lu)",
             (unsigned long)s_config.sample_rate);
    return ESP_OK;
}

size_t hal_audio_write(const int16_t *samples, size_t count) {
    if (!s_audio_available || s_i2s_tx == NULL) {
        return count;  /* Silently discard */
    }

    /* Convert mono to stereo for ES8311 */
    static int16_t stereo_buf[256];  /* 128 mono samples max */
    size_t to_write = count > 128 ? 128 : count;

    for (size_t i = 0; i < to_write; i++) {
        stereo_buf[i * 2] = samples[i];      /* Left */
        stereo_buf[i * 2 + 1] = samples[i];  /* Right (copy) */
    }

    size_t bytes_written = 0;
    esp_err_t ret = i2s_channel_write(s_i2s_tx, stereo_buf,
                                       to_write * 2 * sizeof(int16_t),
                                       &bytes_written, 0);
    if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "I2S write failed: %s", esp_err_to_name(ret));
    }

    return bytes_written / (2 * sizeof(int16_t));
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
    if (s_i2s_tx != NULL) {
        i2s_channel_enable(s_i2s_tx);
    }
}

void hal_audio_stop(void) {
    if (s_i2s_tx != NULL) {
        i2s_channel_disable(s_i2s_tx);
    }
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
