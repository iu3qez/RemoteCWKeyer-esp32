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
#include "esp_netif.h"
#include "esp_event.h"
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
#include "wifi.h"
#include "webui.h"
#include "led.h"
#include "decoder.h"
#include "text_keyer.h"
#include "text_memory.h"

static const char *TAG = "main";

/* External task functions */
extern void rt_task(void *arg);
extern void bg_task(void *arg);
extern void start_audio_test(void);  /* Audio test task */

/* Paddle state for text keyer abort (from rt_task.c) */
extern atomic_bool g_paddle_active;

/* UART logger task handle (for stopping after USB CDC ready) */
static TaskHandle_t s_uart_log_task_handle = NULL;

/* Stream buffer in PSRAM */
#define STREAM_BUFFER_SIZE 4096
static EXT_RAM_BSS_ATTR stream_sample_t s_stream_buffer[STREAM_BUFFER_SIZE];

/* Global keying stream */
keying_stream_t g_keying_stream;

/* Global fault state */
fault_state_t g_fault_state = FAULT_STATE_INIT;

void app_main(void) {
    /* Minimal early debug - use printf since ESP_LOG may not be ready */
    printf("\n\n=== app_main() START ===\n");

    ESP_LOGI(TAG, "keyer_c starting...");

    printf(">>> log_stream_init...\n");
    /* Initialize log streams FIRST (before any RT_* logging) */
    log_stream_init(&g_rt_log_stream);
    log_stream_init(&g_bg_log_stream);
    printf(">>> log_stream_init OK\n");

    /* Enable RT diagnostics for boot debugging */
    atomic_store_explicit(&g_rt_diag_enabled, true, memory_order_relaxed);

    /* Initialize UART logger early for boot logs (GPIO6, 115200) */
    uart_logger_init();

    /* DEBUG: 5 second pause to allow UART connection */
    ESP_LOGI(TAG, "DEBUG: Waiting 5 seconds for UART connection...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    /* Initialize NVS */
    printf(">>> NVS init...\n");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    printf(">>> NVS init OK\n");

    /* Initialize config with defaults, then load from NVS */
    printf(">>> config_init_defaults...\n");
    config_init_defaults(&g_config);
    printf(">>> config_init_defaults OK\n");

    printf(">>> config_load_from_nvs...\n");
    int loaded = config_load_from_nvs();
    printf(">>> config_load_from_nvs OK (loaded=%d)\n", loaded);
    if (loaded > 0) {
        ESP_LOGI(TAG, "Loaded %d parameters from NVS", loaded);
    } else {
        ESP_LOGI(TAG, "Using default configuration");
    }

    /* Initialize TCP/IP stack unconditionally (required for HTTP server even without WiFi) */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Initialize HAL GPIO using values from g_config (loaded from NVS or defaults) */
    printf(">>> hal_gpio_init...\n");
    hal_gpio_config_t gpio_cfg = {
        .dit_pin = CONFIG_GET_GPIO_DIT(),
        .dah_pin = CONFIG_GET_GPIO_DAH(),
        .tx_pin = CONFIG_GET_GPIO_TX(),
        .active_low = true,        /* Paddles are active low (internal pull-up) */
        .tx_active_high = true,    /* TX output is active high */
        .isr_blanking_us = CONFIG_GET_ISR_BLANKING_US(),
    };
    ESP_LOGI(TAG, "GPIO config from g_config: DIT=%d, DAH=%d, TX=%d, ISR_blank=%luus",
             gpio_cfg.dit_pin, gpio_cfg.dah_pin, gpio_cfg.tx_pin,
             (unsigned long)gpio_cfg.isr_blanking_us);
    hal_gpio_init(&gpio_cfg);
    printf(">>> hal_gpio_init OK\n");

    /* Initialize USB CDC (before console) */
    printf(">>> usb_cdc_init...\n");
    ESP_ERROR_CHECK(usb_cdc_init());
    printf(">>> usb_cdc_init OK\n");

    /* Initialize LED strip */
    led_config_t led_cfg = {
        .gpio_data = atomic_load_explicit(&g_config.leds.gpio_data, memory_order_relaxed),
        .led_count = atomic_load_explicit(&g_config.leds.count, memory_order_relaxed),
        .brightness = atomic_load_explicit(&g_config.leds.brightness, memory_order_relaxed),
        .brightness_dim = atomic_load_explicit(&g_config.leds.brightness_dim, memory_order_relaxed),
    };
    ret = led_init(&led_cfg);
    if (ret == ESP_OK) {
        led_set_state(LED_STATE_BOOT);
    } else {
        ESP_LOGW(TAG, "LED init failed (non-fatal): %s", esp_err_to_name(ret));
    }

    /* Initialize WiFi if enabled */
    if (atomic_load_explicit(&g_config.wifi.enabled, memory_order_relaxed)) {
        ESP_LOGI(TAG, "WiFi enabled, initializing...");
        wifi_config_app_t wifi_cfg = {
            .enabled = true,
            .timeout_sec = atomic_load_explicit(&g_config.wifi.timeout_sec, memory_order_relaxed),
            .use_static_ip = atomic_load_explicit(&g_config.wifi.use_static_ip, memory_order_relaxed),
        };
        strncpy(wifi_cfg.ssid, g_config.wifi.ssid, sizeof(wifi_cfg.ssid) - 1);
        strncpy(wifi_cfg.password, g_config.wifi.password, sizeof(wifi_cfg.password) - 1);
        strncpy(wifi_cfg.ip_address, g_config.wifi.ip_address, sizeof(wifi_cfg.ip_address) - 1);
        strncpy(wifi_cfg.netmask, g_config.wifi.netmask, sizeof(wifi_cfg.netmask) - 1);
        strncpy(wifi_cfg.gateway, g_config.wifi.gateway, sizeof(wifi_cfg.gateway) - 1);
        strncpy(wifi_cfg.dns, g_config.wifi.dns, sizeof(wifi_cfg.dns) - 1);

        ret = wifi_app_init(&wifi_cfg);
        if (ret == ESP_OK) {
            led_set_state(LED_STATE_WIFI_CONNECTING);
            wifi_app_start();
        } else {
            ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
            led_set_state(LED_STATE_DEGRADED);
        }
    } else {
        ESP_LOGI(TAG, "WiFi disabled");
        led_set_state(LED_STATE_IDLE);
    }

    /* Initialize stream */
    ESP_LOGI(TAG, "Initializing keying stream (%d samples)", STREAM_BUFFER_SIZE);
    stream_init(&g_keying_stream, s_stream_buffer, STREAM_BUFFER_SIZE);

    /* Initialize fault state */
    fault_init(&g_fault_state);

    hal_audio_config_t audio_cfg = HAL_AUDIO_CONFIG_DEFAULT;
    hal_audio_init(&audio_cfg);

    /* Enable PA for sidetone output (TODO: integrate with PTT for proper control) */
    hal_audio_set_pa(true);

    /* Initialize console */
    console_init();

    /* Initialize WebUI (requires WiFi to be connected) */
    ESP_LOGI(TAG, "Initializing WebUI...");
    webui_init();
    webui_start();

    /* Initialize decoder (creates its own stream consumer) */
    decoder_init();

    /* Initialize text keyer */
    text_keyer_config_t text_cfg = {
        .paddle_abort = &g_paddle_active,
    };
    text_keyer_init(&text_cfg);
    text_memory_init();

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

    /* Create UART log drain task on Core 1 (for boot logs, stopped after USB ready) */
    xTaskCreatePinnedToCore(
        uart_logger_task,
        "uart_log",
        2048,
        NULL,
        tskIDLE_PRIORITY + 1,
        &s_uart_log_task_handle,
        1  /* Core 1 */
    );

    /* Wait for USB CDC to be ready, then stop UART logger */
    ESP_LOGI(TAG, "Waiting for USB CDC...");
    while (!usb_cdc_connected(CDC_ITF_CONSOLE)) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG, "USB CDC connected, stopping UART logger");
    if (s_uart_log_task_handle != NULL) {
        vTaskDelete(s_uart_log_task_handle);
        s_uart_log_task_handle = NULL;
    }

    ESP_LOGI(TAG, "keyer_c started successfully");
}
