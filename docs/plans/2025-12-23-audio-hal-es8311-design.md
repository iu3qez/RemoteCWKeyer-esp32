# Audio HAL Design - ES8311 + TCA9555

**Date**: 2025-12-23
**Status**: Implementato
**Language**: C (ESP-IDF)
**Based on**: 2025-12-20-audio-system-design.md, brainstorming session

## Overview

Complete audio HAL implementation using:
- **ES8311** audio codec via `esp_codec_dev` component
- **TCA9555** IO expander via `esp_io_expander` for PA enable
- **I2S STD** driver for audio data

This design supports both sidetone generation (TX) and future RX audio streaming.

## Dependencies

### ESP-IDF Components

Add to `components/keyer_hal/idf_component.yml`:

```yaml
dependencies:
  espressif/esp_codec_dev: "^1.3.0"
  espressif/esp_io_expander: "^1.0.0"
  espressif/esp_io_expander_tca9555: "^1.0.0"
```

### GPIO Configuration

From reference C++ project (matching hardware):

| Function | GPIO | Notes |
|----------|------|-------|
| I2C SDA | 11 | Shared bus (ES8311 + TCA9555) |
| I2C SCL | 10 | Shared bus |
| I2S MCLK | 12 | Master clock to ES8311 |
| I2S BCLK | 13 | Bit clock |
| I2S LRCK | 14 | Word select (L/R clock) |
| I2S DOUT | 16 | Data out (ESP32 → ES8311) |

### I2C Addresses

| Device | Address |
|--------|---------|
| ES8311 | 0x18 |
| TCA9555 | 0x20 |

### TCA9555 Pin Mapping

| TCA9555 Pin | Function |
|-------------|----------|
| P1_0 (pin 8) | PA Enable (active HIGH) |

## File Structure

```
components/keyer_hal/
├── include/
│   ├── hal_audio.h          # Public API (existing, to modify)
│   └── hal_audio_internal.h # Internal structures (new)
├── src/
│   ├── hal_audio.c          # Unified HAL implementation (rewrite)
│   ├── hal_es8311.c         # ES8311 codec via esp_codec_dev (new)
│   └── hal_io_expander.c    # TCA9555 wrapper (new)
└── idf_component.yml        # Dependencies (new)
```

## API Design

### Public Header (hal_audio.h)

```c
#ifndef HAL_AUDIO_H
#define HAL_AUDIO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

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
    uint32_t sample_rate;    /* Hz (typically 8000) */
    uint8_t volume_percent;  /* 0-100 */

    /* PA control */
    bool pa_via_io_expander; /* true = TCA9555, false = direct GPIO */
    int pa_pin;              /* TCA9555 pin or GPIO number */
    bool pa_active_high;     /* PA enable polarity */
} hal_audio_config_t;

/**
 * @brief Default configuration (matches reference hardware)
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
 * @return ESP_OK on success
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
 * @brief Check if audio HAL is initialized
 */
bool hal_audio_is_initialized(void);

#endif /* HAL_AUDIO_H */
```

## Initialization Sequence

```
hal_audio_init()
    │
    ├── 1. Initialize I2C bus (shared)
    │       - i2c_new_master_bus()
    │       - Store handle for codec + IO expander
    │
    ├── 2. Initialize TCA9555 IO expander
    │       - esp_io_expander_new_i2c_tca9555()
    │       - Configure PA pin as output
    │       - PA OFF initially
    │
    ├── 3. Initialize ES8311 codec via esp_codec_dev
    │       - audio_codec_new_es8311()
    │       - Configure: sample_rate, bit_depth, DAC mode
    │       - Set initial volume
    │
    ├── 4. Initialize I2S STD driver
    │       - i2s_new_channel() with tx_chan
    │       - i2s_channel_init_std_mode()
    │       - Configure: MCLK, BCLK, LRCK, DOUT
    │       - i2s_channel_enable()
    │
    └── 5. Enable PA
            - hal_audio_set_pa(true)
```

## RT Task Integration

### Data Flow

```
rt_task (Core 0)
    │
    ├── sidetone_next_sample() → sample
    │
    └── hal_audio_write(&sample, 1)
            │
            └── i2s_channel_write() [non-blocking with timeout]
```

### PTT Integration

PTT controller calls `hal_audio_set_pa()` during state transitions:

```c
/* In ptt.c - called from bg_task, NOT rt_task */
void ptt_update_pa(ptt_controller_t *ptt) {
    static bool last_ptt_state = false;
    bool current = ptt_is_active(ptt);

    if (current != last_ptt_state) {
        hal_audio_set_pa(current);
        last_ptt_state = current;
    }
}
```

**Important**: `hal_audio_set_pa()` involves I2C transaction (~100us), must be called from bg_task, not rt_task.

## Error Handling

### FAULT Conditions

Audio errors should NOT trigger FAULT (audio is not safety-critical):

| Error | Handling |
|-------|----------|
| I2C bus failure | Log error, retry once, degrade gracefully |
| ES8311 not responding | Log error, disable audio output |
| I2S underrun | Fill with silence, log warning |
| TCA9555 failure | Log error, PA state unknown |

### Graceful Degradation

```c
static bool s_audio_available = false;

esp_err_t hal_audio_init(const hal_audio_config_t *config) {
    // ... initialization ...

    if (codec_init_failed || i2s_init_failed) {
        ESP_LOGE(TAG, "Audio init failed, continuing without audio");
        s_audio_available = false;
        return ESP_OK;  // Don't fail boot
    }

    s_audio_available = true;
    return ESP_OK;
}

size_t hal_audio_write(const int16_t *samples, size_t count) {
    if (!s_audio_available) {
        return count;  // Silently discard
    }
    // ... actual write ...
}
```

## esp_codec_dev Usage

### Codec Interface

```c
#include "esp_codec_dev.h"
#include "es8311_codec.h"

static esp_codec_dev_handle_t s_codec_dev = NULL;

static esp_err_t init_codec(i2c_master_bus_handle_t i2c_bus,
                            uint32_t sample_rate) {
    /* Create ES8311 codec interface */
    audio_codec_i2c_cfg_t i2c_cfg = {
        .addr = 0x18,
    };
    const audio_codec_if_t *codec_if = audio_codec_new_es8311(&i2c_cfg);

    /* Create codec device */
    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = codec_if,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    s_codec_dev = esp_codec_dev_new(&dev_cfg);

    /* Configure codec */
    esp_codec_dev_sample_info_t sample_info = {
        .sample_rate = sample_rate,
        .channel = 1,
        .bits_per_sample = 16,
    };
    esp_codec_dev_open(s_codec_dev, &sample_info);

    return ESP_OK;
}
```

## esp_io_expander Usage

### TCA9555 Interface

```c
#include "esp_io_expander.h"
#include "esp_io_expander_tca9555.h"

static esp_io_expander_handle_t s_io_expander = NULL;

#define PA_PIN  8  /* TCA9555 P1_0 */

static esp_err_t init_io_expander(i2c_master_bus_handle_t i2c_bus) {
    esp_io_expander_new_i2c_tca9555(i2c_bus, 0x20, &s_io_expander);

    /* Configure PA pin as output, initially LOW */
    esp_io_expander_set_dir(s_io_expander, 1 << PA_PIN,
                            IO_EXPANDER_OUTPUT);
    esp_io_expander_set_level(s_io_expander, 1 << PA_PIN, 0);

    return ESP_OK;
}

esp_err_t hal_audio_set_pa(bool enable) {
    if (s_io_expander == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t level = enable ? (1 << PA_PIN) : 0;
    return esp_io_expander_set_level(s_io_expander, 1 << PA_PIN, level);
}
```

## Summary

### Dependencies to Add

1. `espressif/esp_codec_dev` - Codec abstraction layer
2. `espressif/esp_io_expander` - IO expander base
3. `espressif/esp_io_expander_tca9555` - TCA9555 driver

### Files to Modify

1. `components/keyer_hal/include/hal_audio.h` - Expand config struct
2. `components/keyer_hal/src/hal_audio.c` - Full rewrite
3. `components/keyer_audio/src/ptt.c` - Add PA callback hook

### Files to Create

1. `components/keyer_hal/idf_component.yml` - Dependencies
2. `components/keyer_hal/src/hal_es8311.c` - Codec wrapper (optional, can inline)
3. `components/keyer_hal/src/hal_io_expander.c` - TCA9555 wrapper (optional)

### Key Design Decisions

1. **Unified HAL** - Single `hal_audio_init()` initializes everything
2. **No mixing** - Sidetone or remote, never both simultaneously
3. **PTT manages PA** - PA enable/disable via PTT state machine, not rt_task
4. **Graceful degradation** - Audio failure does not block system boot
5. **esp_codec_dev** - Future-proof for RX streaming (ADC path)
