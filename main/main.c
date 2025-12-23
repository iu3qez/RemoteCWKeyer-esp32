/**
 * @file main.c
 * @brief ESP-IDF entry point for keyer_c
 *
 * Initializes all components and spawns FreeRTOS tasks.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "keyer_core.h"
#include "iambic.h"
#include "audio.h"
#include "rt_log.h"
#include "console.h"
#include "config.h"
#include "config_nvs.h"
#include "hal_gpio.h"
#include "hal_audio.h"
#include "usb_cdc.h"
#include "usb_log.h"

static const char *TAG = "main";

/* External task functions */
extern void rt_task(void *arg);
extern void bg_task(void *arg);

/* Stream buffer in PSRAM */
#define STREAM_BUFFER_SIZE 4096
static EXT_RAM_BSS_ATTR stream_sample_t s_stream_buffer[STREAM_BUFFER_SIZE];

/* Global keying stream */
keying_stream_t g_keying_stream;

/* Global fault state */
fault_state_t g_fault_state = FAULT_STATE_INIT;

void app_main(void) {
    ESP_LOGI(TAG, "keyer_c starting...");

    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialize config with defaults, then load from NVS */
    config_init_defaults(&g_config);
    int loaded = config_load_from_nvs();
    if (loaded > 0) {
        ESP_LOGI(TAG, "Loaded %d parameters from NVS", loaded);
    } else {
        ESP_LOGI(TAG, "Using default configuration");
    }

    /* Initialize HAL GPIO (includes force_gpio_reset for JTAG-claimed pins) */
    hal_gpio_config_t gpio_cfg = HAL_GPIO_CONFIG_DEFAULT;
    hal_gpio_init(&gpio_cfg);

    /* Initialize USB CDC (before console) */
    ESP_ERROR_CHECK(usb_cdc_init());

    /* Initialize stream */
    ESP_LOGI(TAG, "Initializing keying stream (%d samples)", STREAM_BUFFER_SIZE);
    stream_init(&g_keying_stream, s_stream_buffer, STREAM_BUFFER_SIZE);

    /* Initialize fault state */
    fault_init(&g_fault_state);

    /* Initialize log streams */
    log_stream_init(&g_rt_log_stream);
    log_stream_init(&g_bg_log_stream);

    /* Note: UART logger replaced by USB CDC log on CDC1 */

    hal_audio_config_t audio_cfg = HAL_AUDIO_CONFIG_DEFAULT;
    hal_audio_init(&audio_cfg);

    /* Initialize console */
    console_init();

    ESP_LOGI(TAG, "Creating tasks...");

    /* Create RT task on Core 0 (highest priority) */
    xTaskCreatePinnedToCore(
        rt_task,
        "rt_task",
        4096,
        NULL,
        configMAX_PRIORITIES - 1,
        NULL,
        0  /* Core 0 */
    );

    /* Create BG task on Core 1 */
    xTaskCreatePinnedToCore(
        bg_task,
        "bg_task",
        4096,
        NULL,
        tskIDLE_PRIORITY + 2,
        NULL,
        1  /* Core 1 */
    );

    /* Create USB log drain task on Core 1 */
    xTaskCreatePinnedToCore(
        usb_log_task,
        "usb_log",
        4096,
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL,
        1  /* Core 1 */
    );

    /* Note: console_task removed - console handled by USB CDC RX callback */

    ESP_LOGI(TAG, "keyer_c started successfully");
}
