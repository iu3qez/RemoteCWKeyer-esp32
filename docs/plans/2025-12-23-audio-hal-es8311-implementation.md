# Audio HAL ES8311 + TCA9555 Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement complete audio HAL with ES8311 codec and TCA9555 PA control, replacing the current stub implementation.

**Architecture:** Uses `esp_codec_dev` for codec abstraction, `esp_io_expander` for TCA9555 control, and I2S STD driver for audio data. Unified initialization with graceful degradation on hardware failure.

**Tech Stack:** ESP-IDF v5.x, esp_codec_dev, esp_io_expander, esp_io_expander_tca9555, I2S STD driver.

---

## Prerequisites

Before starting, verify the reference C++ project builds:
```bash
cd tmp/RemoteKeyerIU3QEZ && idf.py build
```

---

### Task 1: Add ESP-IDF Component Dependencies

**Files:**
- Create: `components/keyer_hal/idf_component.yml`

**Step 1: Create component dependency file**

Create `components/keyer_hal/idf_component.yml`:

```yaml
dependencies:
  idf:
    version: ">=5.0.0"
  espressif/esp_codec_dev: ">=1.2.0"
  espressif/esp_io_expander: ">=1.0.0"
  espressif/esp_io_expander_tca9555: ">=1.0.0"
```

**Step 2: Update CMakeLists.txt REQUIRES**

Edit `components/keyer_hal/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS
        "src/hal_gpio.c"
        "src/hal_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES keyer_core driver esp_driver_gpio esp_driver_i2s esp_driver_i2c
    PRIV_REQUIRES esp_codec_dev esp_io_expander esp_io_expander_tca9555
)
```

Remove `src/hal_es8311.c` from SRCS (will be integrated into hal_audio.c).

**Step 3: Verify dependencies resolve**

Run: `idf.py reconfigure`

Expected: Dependencies download to `managed_components/`

**Step 4: Commit**

```bash
git add components/keyer_hal/idf_component.yml components/keyer_hal/CMakeLists.txt
git commit -m "chore(hal): add esp_codec_dev and esp_io_expander dependencies"
```

---

### Task 2: Update hal_audio.h with New Configuration

**Files:**
- Modify: `components/keyer_hal/include/hal_audio.h`

**Step 1: Write the new header with expanded config struct**

Replace contents of `components/keyer_hal/include/hal_audio.h`:

```c
/**
 * @file hal_audio.h
 * @brief Audio HAL - ES8311 codec + TCA9555 PA control
 */

#ifndef KEYER_HAL_AUDIO_H
#define KEYER_HAL_AUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Audio HAL configuration
 */
typedef struct {
    /* I2C bus (shared with other peripherals) */
    int i2c_sda_pin;
    int i2c_scl_pin;
    uint32_t i2c_freq_hz;

    /* I2S pins */
    int i2s_mclk_pin;
    int i2s_bclk_pin;
    int i2s_lrck_pin;
    int i2s_dout_pin;

    /* Audio parameters */
    uint32_t sample_rate;    /**< Sample rate in Hz (typically 8000) */
    uint8_t volume_percent;  /**< Initial volume 0-100 */

    /* PA control */
    bool pa_via_io_expander; /**< true = TCA9555, false = direct GPIO */
    int pa_pin;              /**< TCA9555 pin or GPIO number */
    bool pa_active_high;     /**< PA enable polarity */
} hal_audio_config_t;

/**
 * @brief Default audio configuration (matches reference hardware)
 */
#define HAL_AUDIO_CONFIG_DEFAULT { \
    .i2c_sda_pin = 11, \
    .i2c_scl_pin = 10, \
    .i2c_freq_hz = 400000, \
    .i2s_mclk_pin = 12, \
    .i2s_bclk_pin = 13, \
    .i2s_lrck_pin = 14, \
    .i2s_dout_pin = 16, \
    .sample_rate = 8000, \
    .volume_percent = 70, \
    .pa_via_io_expander = true, \
    .pa_pin = 8, \
    .pa_active_high = true, \
}

/**
 * @brief Initialize audio HAL (ES8311 + I2S + TCA9555)
 * @param config Configuration structure
 * @return ESP_OK on success, error code on failure
 * @note Audio failure does not block boot - system degrades gracefully
 */
esp_err_t hal_audio_init(const hal_audio_config_t *config);

/**
 * @brief Write audio samples to I2S buffer
 * @param samples Pointer to sample buffer (mono, 16-bit signed)
 * @param count Number of samples
 * @return Number of samples actually written
 */
size_t hal_audio_write(const int16_t *samples, size_t count);

/**
 * @brief Set codec volume
 * @param volume_percent 0-100
 * @return ESP_OK on success
 * @note NOT RT-safe (I2C transaction)
 */
esp_err_t hal_audio_set_volume(uint8_t volume_percent);

/**
 * @brief Enable/disable PA via TCA9555
 * @param enable true = PA on, false = PA off
 * @return ESP_OK on success
 * @note NOT RT-safe (I2C transaction)
 */
esp_err_t hal_audio_set_pa(bool enable);

/**
 * @brief Start I2S output
 */
void hal_audio_start(void);

/**
 * @brief Stop I2S output
 */
void hal_audio_stop(void);

/**
 * @brief Check if audio HAL is initialized and available
 * @return true if audio is functional
 */
bool hal_audio_is_available(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_HAL_AUDIO_H */
```

**Step 2: Verify header compiles**

Run: `idf.py build`

Expected: Build may fail (implementation missing), but header syntax is valid.

**Step 3: Commit**

```bash
git add components/keyer_hal/include/hal_audio.h
git commit -m "feat(hal_audio): expand config for ES8311 + TCA9555"
```

---

### Task 3: Implement I2C Bus Initialization

**Files:**
- Modify: `components/keyer_hal/src/hal_audio.c`

**Step 1: Write I2C initialization code**

Replace contents of `components/keyer_hal/src/hal_audio.c`:

```c
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
```

**Step 2: Build and verify**

Run: `idf.py build`

Expected: PASS (component compiles)

**Step 3: Commit**

```bash
git add components/keyer_hal/src/hal_audio.c
git commit -m "feat(hal_audio): implement I2C bus initialization"
```

---

### Task 4: Implement TCA9555 IO Expander for PA Control

**Files:**
- Modify: `components/keyer_hal/src/hal_audio.c`

**Step 1: Add IO expander includes and state**

Add after existing includes in `hal_audio.c`:

```c
#include "esp_io_expander.h"
#include "esp_io_expander_tca9555.h"
```

Add after `s_i2c_bus` declaration:

```c
static esp_io_expander_handle_t s_io_expander = NULL;
static bool s_pa_enabled = false;
```

**Step 2: Add IO expander initialization function**

Add before `hal_audio_init`:

```c
/**
 * @brief Initialize TCA9555 IO expander for PA control
 */
static esp_err_t init_io_expander(void) {
    if (!s_config.pa_via_io_expander) {
        ESP_LOGI(TAG, "PA via direct GPIO (not IO expander)");
        return ESP_OK;
    }

    esp_err_t ret = esp_io_expander_new_i2c_tca9555(
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
    ret = esp_io_expander_set_dir(s_io_expander, (uint16_t)pa_mask, IO_EXPANDER_OUTPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set PA pin direction: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_io_expander_set_level(s_io_expander, (uint16_t)pa_mask, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear PA pin: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "TCA9555 initialized, PA pin=%d", s_config.pa_pin);
    return ESP_OK;
}
```

**Step 3: Update hal_audio_init to call init_io_expander**

In `hal_audio_init`, after I2C init success, add:

```c
    /* Step 2: Initialize TCA9555 IO expander */
    ret = init_io_expander();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "IO expander init failed, PA control unavailable");
        /* Continue - PA control is not critical */
    }
```

**Step 4: Implement hal_audio_set_pa**

Replace the `hal_audio_set_pa` function:

```c
esp_err_t hal_audio_set_pa(bool enable) {
    if (s_io_expander == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_pa_enabled == enable) {
        return ESP_OK;  /* No change needed */
    }

    uint32_t pa_mask = (1u << (uint32_t)s_config.pa_pin);
    uint32_t level = s_config.pa_active_high ? (enable ? 1 : 0) : (enable ? 0 : 1);

    esp_err_t ret = esp_io_expander_set_level(s_io_expander, (uint16_t)pa_mask, level);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to %s PA: %s",
                 enable ? "enable" : "disable", esp_err_to_name(ret));
        return ret;
    }

    s_pa_enabled = enable;
    ESP_LOGI(TAG, "PA %s", enable ? "enabled" : "disabled");
    return ESP_OK;
}
```

**Step 5: Build and verify**

Run: `idf.py build`

Expected: PASS

**Step 6: Commit**

```bash
git add components/keyer_hal/src/hal_audio.c
git commit -m "feat(hal_audio): implement TCA9555 PA control"
```

---

### Task 5: Implement I2S Audio Output

**Files:**
- Modify: `components/keyer_hal/src/hal_audio.c`

**Step 1: Add I2S state variables**

Add after IO expander state:

```c
static i2s_chan_handle_t s_i2s_tx = NULL;
```

**Step 2: Add I2S initialization function**

Add before `hal_audio_init`:

```c
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
```

**Step 3: Update hal_audio_init to call init_i2s**

After IO expander init, add:

```c
    /* Step 3: Initialize I2S */
    ret = init_i2s();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S init failed, continuing without audio");
        return ESP_OK;  /* Don't block boot */
    }
```

**Step 4: Implement hal_audio_write**

Replace the function:

```c
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
```

**Step 5: Implement hal_audio_start and hal_audio_stop**

Replace the functions:

```c
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
```

**Step 6: Build and verify**

Run: `idf.py build`

Expected: PASS

**Step 7: Commit**

```bash
git add components/keyer_hal/src/hal_audio.c
git commit -m "feat(hal_audio): implement I2S audio output"
```

---

### Task 6: Implement ES8311 Codec via esp_codec_dev

**Files:**
- Modify: `components/keyer_hal/src/hal_audio.c`

**Step 1: Add codec includes**

Add after IO expander includes:

```c
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
```

**Step 2: Add codec state variables**

Add after I2S state:

```c
static esp_codec_dev_handle_t s_codec_dev = NULL;
static const audio_codec_ctrl_if_t *s_ctrl_if = NULL;
static const audio_codec_data_if_t *s_data_if = NULL;
static const audio_codec_gpio_if_t *s_gpio_if = NULL;
static const audio_codec_if_t *s_codec_if = NULL;
```

**Step 3: Add codec initialization function**

Add before `hal_audio_init`:

```c
#define ES8311_I2C_ADDR 0x18

/**
 * @brief Initialize ES8311 codec via esp_codec_dev
 */
static esp_err_t init_codec(void) {
    /* Create I2C control interface */
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_NUM_0,
        .addr = ES8311_I2C_ADDR,
        .bus_handle = s_i2c_bus,
    };
    s_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (s_ctrl_if == NULL) {
        ESP_LOGE(TAG, "Failed to create codec I2C ctrl");
        return ESP_FAIL;
    }

    /* Create I2S data interface */
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = NULL,
        .tx_handle = s_i2s_tx,
    };
    s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (s_data_if == NULL) {
        ESP_LOGE(TAG, "Failed to create codec I2S data");
        return ESP_FAIL;
    }

    /* Create GPIO interface */
    s_gpio_if = audio_codec_new_gpio();
    if (s_gpio_if == NULL) {
        ESP_LOGE(TAG, "Failed to create codec GPIO");
        return ESP_FAIL;
    }

    /* Create ES8311 codec interface */
    es8311_codec_cfg_t es_cfg = {
        .ctrl_if = s_ctrl_if,
        .gpio_if = s_gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1,  /* PA managed separately via TCA9555 */
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = (s_config.i2s_mclk_pin >= 0),
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .no_dac_ref = false,
    };
    s_codec_if = es8311_codec_new(&es_cfg);
    if (s_codec_if == NULL) {
        ESP_LOGE(TAG, "Failed to create ES8311 codec");
        return ESP_FAIL;
    }

    /* Create codec device */
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = s_codec_if,
        .data_if = s_data_if,
    };
    s_codec_dev = esp_codec_dev_new(&dev_cfg);
    if (s_codec_dev == NULL) {
        ESP_LOGE(TAG, "Failed to create codec device");
        return ESP_FAIL;
    }

    /* Configure sample info */
    esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = 16,
        .channel = 2,  /* Stereo */
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) | ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1),
        .sample_rate = s_config.sample_rate,
        .mclk_multiple = 0,  /* Auto */
    };
    if (esp_codec_dev_open(s_codec_dev, &sample_info) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open codec");
        return ESP_FAIL;
    }

    /* Set initial volume and unmute */
    esp_codec_dev_set_out_vol(s_codec_dev, (int)s_config.volume_percent);
    esp_codec_dev_set_out_mute(s_codec_dev, false);

    ESP_LOGI(TAG, "ES8311 codec initialized (volume=%d%%)", s_config.volume_percent);
    return ESP_OK;
}
```

**Step 4: Update hal_audio_init to call init_codec**

After I2S init, add:

```c
    /* Step 4: Initialize ES8311 codec */
    ret = init_codec();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Codec init failed, continuing without audio");
        return ESP_OK;  /* Don't block boot */
    }
```

**Step 5: Implement hal_audio_set_volume**

Replace the function:

```c
esp_err_t hal_audio_set_volume(uint8_t volume_percent) {
    if (s_codec_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (volume_percent > 100) {
        volume_percent = 100;
    }

    int ret = esp_codec_dev_set_out_vol(s_codec_dev, (int)volume_percent);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Failed to set volume: %d", ret);
        return ESP_FAIL;
    }

    return ESP_OK;
}
```

**Step 6: Build and verify**

Run: `idf.py build`

Expected: PASS

**Step 7: Commit**

```bash
git add components/keyer_hal/src/hal_audio.c
git commit -m "feat(hal_audio): implement ES8311 codec via esp_codec_dev"
```

---

### Task 7: Add PTT PA Callback Integration

**Files:**
- Modify: `components/keyer_audio/include/ptt.h`
- Modify: `components/keyer_audio/src/ptt.c`

**Step 1: Add PA callback typedef to ptt.h**

Add after includes:

```c
/**
 * @brief PA control callback type
 * @param enable true to enable PA, false to disable
 */
typedef void (*ptt_pa_callback_t)(bool enable);
```

Update `ptt_controller_t`:

```c
typedef struct {
    ptt_state_t state;       /**< Current PTT state */
    uint64_t tail_us;        /**< Tail timeout in microseconds */
    uint64_t last_audio_us;  /**< Timestamp of last audio activity */
    bool audio_active;       /**< Audio currently active */
    ptt_pa_callback_t pa_callback;  /**< Optional PA control callback */
} ptt_controller_t;
```

Add new function declaration:

```c
/**
 * @brief Set PA control callback
 *
 * @param ptt Controller
 * @param callback Function to call on PTT state change (may be NULL)
 */
void ptt_set_pa_callback(ptt_controller_t *ptt, ptt_pa_callback_t callback);
```

**Step 2: Update ptt.c to call PA callback on state transitions**

Update `ptt_init` to clear callback:

```c
void ptt_init(ptt_controller_t *ptt, uint32_t tail_ms) {
    assert(ptt != NULL);

    ptt->state = PTT_OFF;
    ptt->tail_us = (uint64_t)tail_ms * 1000;
    ptt->last_audio_us = 0;
    ptt->audio_active = false;
    ptt->pa_callback = NULL;
}
```

Update `ptt_audio_sample` to call callback on PTT ON:

```c
void ptt_audio_sample(ptt_controller_t *ptt, uint64_t timestamp_us) {
    assert(ptt != NULL);

    ptt->last_audio_us = timestamp_us;
    ptt->audio_active = true;

    /* Turn on PTT immediately on audio */
    if (ptt->state == PTT_OFF) {
        ptt->state = PTT_ON;
        if (ptt->pa_callback != NULL) {
            ptt->pa_callback(true);
        }
    }
}
```

Update `ptt_tick` to call callback on PTT OFF:

```c
void ptt_tick(ptt_controller_t *ptt, uint64_t timestamp_us) {
    assert(ptt != NULL);

    if (ptt->state == PTT_ON) {
        /* Check if tail timeout expired */
        if (!ptt->audio_active && timestamp_us > ptt->last_audio_us + ptt->tail_us) {
            ptt->state = PTT_OFF;
            if (ptt->pa_callback != NULL) {
                ptt->pa_callback(false);
            }
        }
    }

    /* Reset audio_active flag - will be set again by audio_sample if active */
    ptt->audio_active = false;
}
```

Update `ptt_force_off` to call callback:

```c
void ptt_force_off(ptt_controller_t *ptt) {
    assert(ptt != NULL);

    if (ptt->state == PTT_ON && ptt->pa_callback != NULL) {
        ptt->pa_callback(false);
    }
    ptt->state = PTT_OFF;
    ptt->audio_active = false;
}
```

Add `ptt_set_pa_callback`:

```c
void ptt_set_pa_callback(ptt_controller_t *ptt, ptt_pa_callback_t callback) {
    assert(ptt != NULL);
    ptt->pa_callback = callback;
}
```

**Step 3: Build and run host tests**

Run:
```bash
cd test_host && cmake -B build && cmake --build build && ./build/test_runner
```

Expected: All tests PASS (existing tests still work with NULL callback)

**Step 4: Commit**

```bash
git add components/keyer_audio/include/ptt.h components/keyer_audio/src/ptt.c
git commit -m "feat(ptt): add PA control callback for hal_audio integration"
```

---

### Task 8: Delete Obsolete hal_es8311 Files

**Files:**
- Delete: `components/keyer_hal/include/hal_es8311.h`
- Delete: `components/keyer_hal/src/hal_es8311.c`

**Step 1: Remove files**

```bash
rm components/keyer_hal/include/hal_es8311.h
rm components/keyer_hal/src/hal_es8311.c
```

**Step 2: Update main.c to remove hal_es8311 include**

Check `main/main.c` for any `#include "hal_es8311.h"` and remove it if present.

**Step 3: Build and verify**

Run: `idf.py build`

Expected: PASS

**Step 4: Commit**

```bash
git add -u
git commit -m "chore(hal): remove obsolete hal_es8311 (replaced by hal_audio)"
```

---

### Task 9: Integration Test on Hardware

**Files:**
- None (verification only)

**Step 1: Flash and run**

```bash
idf.py flash monitor
```

**Step 2: Verify log output**

Expected log lines:
```
I (xxx) hal_audio: I2C bus initialized (SDA=11, SCL=10)
I (xxx) hal_audio: TCA9555 initialized, PA pin=8
I (xxx) hal_audio: I2S initialized (MCLK=12, BCLK=13, LRCK=14, DOUT=16, rate=8000)
I (xxx) hal_audio: ES8311 codec initialized (volume=70%)
I (xxx) hal_audio: Audio HAL initialized (sample_rate=8000)
```

**Step 3: Test PA control via console (if available)**

If console has `pa on` / `pa off` commands, verify:
- `pa on` → Log shows "PA enabled"
- `pa off` → Log shows "PA disabled"

**Step 4: Test sidetone**

Press paddle keys and verify audio output through speaker.

---

### Task 10: Final Cleanup and Documentation

**Files:**
- Modify: `docs/plans/2025-12-23-audio-hal-es8311-design.md`

**Step 1: Update design doc status**

Change status from "Approvato" to "Implementato".

**Step 2: Commit**

```bash
git add docs/plans/2025-12-23-audio-hal-es8311-design.md
git commit -m "docs: mark audio HAL design as implemented"
```

---

## Summary

| Task | Description | Key Files |
|------|-------------|-----------|
| 1 | Add component dependencies | `idf_component.yml`, `CMakeLists.txt` |
| 2 | Update hal_audio.h config | `hal_audio.h` |
| 3 | I2C bus initialization | `hal_audio.c` |
| 4 | TCA9555 PA control | `hal_audio.c` |
| 5 | I2S audio output | `hal_audio.c` |
| 6 | ES8311 codec | `hal_audio.c` |
| 7 | PTT PA callback | `ptt.h`, `ptt.c` |
| 8 | Delete obsolete files | Remove `hal_es8311.*` |
| 9 | Hardware integration test | Verification |
| 10 | Documentation update | Design doc |
