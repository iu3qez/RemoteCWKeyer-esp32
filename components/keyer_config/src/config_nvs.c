/**
 * @file config_nvs.c
 * @brief NVS persistence for keyer configuration
 */

#include "config.h"
#include "config_nvs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "config_nvs";

/**
 * @brief Migrate old memory_window_us parameter to new percentage-based system
 *
 * This function checks if the old "mem_win" parameter exists in NVS and hasn't
 * been migrated yet. If so, it sets the new percentage-based parameters to
 * 0-100% (full window) and erases the old parameter.
 */
static void migrate_memory_window_params(nvs_handle_t handle) {
    uint32_t old_window_us;

    /* Check if old parameter exists */
    if (nvs_get_u32(handle, "mem_win", &old_window_us) != ESP_OK) {
        return;  /* No migration needed */
    }

    /* Check if already migrated */
    uint8_t test_val;
    if (nvs_get_u8(handle, NVS_KEY_MEM_WINDOW_START_PCT, &test_val) == ESP_OK) {
        /* Already migrated, just clean up old key */
        nvs_erase_key(handle, "mem_win");
        nvs_commit(handle);
        return;
    }

    /* Migrate: old system always used 0-100% window */
    nvs_set_u8(handle, NVS_KEY_MEM_WINDOW_START_PCT, 0);
    nvs_set_u8(handle, NVS_KEY_MEM_WINDOW_END_PCT, 100);
    nvs_erase_key(handle, "mem_win");
    nvs_commit(handle);

    ESP_LOGI(TAG, "Migrated memory_window_us to percentage-based system (0-100%%)");
}

int config_load_from_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err;

    /* First, run migration with READWRITE access */
    err = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved config found, using defaults");
        return 0;
    }
    if (err == ESP_OK) {
        migrate_memory_window_params(handle);
        nvs_close(handle);
        /* Reopen in READONLY mode for loading */
        err = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READONLY, &handle);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return -1;
    }

    int loaded = 0;
    uint8_t u8_val;
    uint16_t u16_val;
    uint32_t u32_val;

    /* Load keyer.wpm */
    if (nvs_get_u16(handle, NVS_KEY_WPM, &u16_val) == ESP_OK) {
        atomic_store_explicit(&g_config.keyer.wpm, u16_val, memory_order_relaxed);
        loaded++;
    }

    /* Load keyer.iambic_mode */
    if (nvs_get_u8(handle, NVS_KEY_IAMBIC_MODE, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.keyer.iambic_mode, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load keyer.memory_mode */
    if (nvs_get_u8(handle, NVS_KEY_MEMORY_MODE, &u8_val) == ESP_OK && u8_val <= 3) {
        atomic_store_explicit(&g_config.keyer.memory_mode, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load keyer.squeeze_mode */
    if (nvs_get_u8(handle, NVS_KEY_SQUEEZE_MODE, &u8_val) == ESP_OK && u8_val <= 1) {
        atomic_store_explicit(&g_config.keyer.squeeze_mode, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load keyer.mem_window_start_pct */
    if (nvs_get_u8(handle, NVS_KEY_MEM_WINDOW_START_PCT, &u8_val) == ESP_OK && u8_val <= 100) {
        atomic_store_explicit(&g_config.keyer.mem_window_start_pct, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load keyer.mem_window_end_pct */
    if (nvs_get_u8(handle, NVS_KEY_MEM_WINDOW_END_PCT, &u8_val) == ESP_OK && u8_val <= 100) {
        atomic_store_explicit(&g_config.keyer.mem_window_end_pct, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load keyer.weight */
    if (nvs_get_u8(handle, NVS_KEY_WEIGHT, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.keyer.weight, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load audio.sidetone_freq_hz */
    if (nvs_get_u16(handle, NVS_KEY_SIDETONE_FREQ_HZ, &u16_val) == ESP_OK) {
        atomic_store_explicit(&g_config.audio.sidetone_freq_hz, u16_val, memory_order_relaxed);
        loaded++;
    }

    /* Load audio.sidetone_volume */
    if (nvs_get_u8(handle, NVS_KEY_SIDETONE_VOLUME, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.audio.sidetone_volume, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load audio.fade_duration_ms */
    if (nvs_get_u8(handle, NVS_KEY_FADE_DURATION_MS, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.audio.fade_duration_ms, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load hardware.gpio_dit */
    if (nvs_get_u8(handle, NVS_KEY_GPIO_DIT, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.hardware.gpio_dit, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load hardware.gpio_dah */
    if (nvs_get_u8(handle, NVS_KEY_GPIO_DAH, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.hardware.gpio_dah, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load hardware.gpio_tx */
    if (nvs_get_u8(handle, NVS_KEY_GPIO_TX, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.hardware.gpio_tx, u8_val, memory_order_relaxed);
        loaded++;
    }

    /* Load timing.ptt_tail_ms */
    if (nvs_get_u32(handle, NVS_KEY_PTT_TAIL_MS, &u32_val) == ESP_OK) {
        atomic_store_explicit(&g_config.timing.ptt_tail_ms, u32_val, memory_order_relaxed);
        loaded++;
    }

    /* Load timing.tick_rate_hz */
    if (nvs_get_u32(handle, NVS_KEY_TICK_RATE_HZ, &u32_val) == ESP_OK) {
        atomic_store_explicit(&g_config.timing.tick_rate_hz, u32_val, memory_order_relaxed);
        loaded++;
    }

    /* Load system.debug_logging */
    if (nvs_get_u8(handle, NVS_KEY_DEBUG_LOGGING, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.system.debug_logging, u8_val != 0, memory_order_relaxed);
        loaded++;
    }

    /* Load system.led_brightness */
    if (nvs_get_u8(handle, NVS_KEY_LED_BRIGHTNESS, &u8_val) == ESP_OK) {
        atomic_store_explicit(&g_config.system.led_brightness, u8_val, memory_order_relaxed);
        loaded++;
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Loaded %d parameters from NVS", loaded);
    return loaded;
}

int config_save_to_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for write: %s", esp_err_to_name(err));
        return -1;
    }

    int saved = 0;

    /* Save keyer.wpm */
    if (nvs_set_u16(handle, NVS_KEY_WPM,
            (uint16_t)atomic_load_explicit(&g_config.keyer.wpm, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save keyer.iambic_mode */
    if (nvs_set_u8(handle, NVS_KEY_IAMBIC_MODE,
            (uint8_t)atomic_load_explicit(&g_config.keyer.iambic_mode, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save keyer.memory_mode */
    if (nvs_set_u8(handle, NVS_KEY_MEMORY_MODE,
            (uint8_t)atomic_load_explicit(&g_config.keyer.memory_mode, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save keyer.squeeze_mode */
    if (nvs_set_u8(handle, NVS_KEY_SQUEEZE_MODE,
            (uint8_t)atomic_load_explicit(&g_config.keyer.squeeze_mode, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save keyer.mem_window_start_pct */
    if (nvs_set_u8(handle, NVS_KEY_MEM_WINDOW_START_PCT,
            (uint8_t)atomic_load_explicit(&g_config.keyer.mem_window_start_pct, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save keyer.mem_window_end_pct */
    if (nvs_set_u8(handle, NVS_KEY_MEM_WINDOW_END_PCT,
            (uint8_t)atomic_load_explicit(&g_config.keyer.mem_window_end_pct, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save keyer.weight */
    if (nvs_set_u8(handle, NVS_KEY_WEIGHT,
            (uint8_t)atomic_load_explicit(&g_config.keyer.weight, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save audio.sidetone_freq_hz */
    if (nvs_set_u16(handle, NVS_KEY_SIDETONE_FREQ_HZ,
            (uint16_t)atomic_load_explicit(&g_config.audio.sidetone_freq_hz, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save audio.sidetone_volume */
    if (nvs_set_u8(handle, NVS_KEY_SIDETONE_VOLUME,
            (uint8_t)atomic_load_explicit(&g_config.audio.sidetone_volume, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save audio.fade_duration_ms */
    if (nvs_set_u8(handle, NVS_KEY_FADE_DURATION_MS,
            (uint8_t)atomic_load_explicit(&g_config.audio.fade_duration_ms, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save hardware.gpio_dit */
    if (nvs_set_u8(handle, NVS_KEY_GPIO_DIT,
            (uint8_t)atomic_load_explicit(&g_config.hardware.gpio_dit, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save hardware.gpio_dah */
    if (nvs_set_u8(handle, NVS_KEY_GPIO_DAH,
            (uint8_t)atomic_load_explicit(&g_config.hardware.gpio_dah, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save hardware.gpio_tx */
    if (nvs_set_u8(handle, NVS_KEY_GPIO_TX,
            (uint8_t)atomic_load_explicit(&g_config.hardware.gpio_tx, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save timing.ptt_tail_ms */
    if (nvs_set_u32(handle, NVS_KEY_PTT_TAIL_MS,
            (uint32_t)atomic_load_explicit(&g_config.timing.ptt_tail_ms, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save timing.tick_rate_hz */
    if (nvs_set_u32(handle, NVS_KEY_TICK_RATE_HZ,
            (uint32_t)atomic_load_explicit(&g_config.timing.tick_rate_hz, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    /* Save system.debug_logging */
    if (nvs_set_u8(handle, NVS_KEY_DEBUG_LOGGING,
            atomic_load_explicit(&g_config.system.debug_logging, memory_order_relaxed) ? 1 : 0) == ESP_OK) {
        saved++;
    }

    /* Save system.led_brightness */
    if (nvs_set_u8(handle, NVS_KEY_LED_BRIGHTNESS,
            (uint8_t)atomic_load_explicit(&g_config.system.led_brightness, memory_order_relaxed)) == ESP_OK) {
        saved++;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        return -1;
    }

    ESP_LOGI(TAG, "Saved %d parameters to NVS", saved);
    return saved;
}

esp_err_t config_load_param(const char *name) {
    (void)name;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t config_save_param(const char *name) {
    (void)name;
    return ESP_ERR_NOT_SUPPORTED;
}
