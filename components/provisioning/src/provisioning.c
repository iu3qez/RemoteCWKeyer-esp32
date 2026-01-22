/**
 * @file provisioning.c
 * @brief Main provisioning logic and NVS operations
 *
 * Provides check functions and main entry point for provisioning mode.
 * Uses hardcoded NVS keys for isolation from generated config code.
 */

#include "provisioning.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "led.h"

/* Internal declarations */
extern void prov_wifi_start_ap(void);
extern void prov_wifi_stop(void);
extern void prov_http_start(void);
extern void prov_http_stop(void);

static const char *TAG = "provisioning";

/* NVS configuration - must match config_nvs.h definitions */
#define PROV_NVS_NAMESPACE  "keyer_cfg"   /* Must match CONFIG_NVS_NAMESPACE */
#define PROV_NVS_WIFI_SSID  "wifi_ssid"   /* Must match NVS_WIFI_SSID */
#define PROV_NVS_WIFI_PASS  "wifi_pass"   /* Must match NVS_WIFI_PASSWORD */
#define PROV_NVS_WIFI_EN    "wifi_en"     /* Must match NVS_WIFI_ENABLED */
#define PROV_NVS_CALLSIGN   "callsign"    /* Must match NVS_SYSTEM_CALLSIGN */
#define PROV_NVS_THEME      "user_theme"  /* New key for provisioning */

/* Factory reset timing */
#define FACTORY_RESET_CHECK_INTERVAL_MS  100

bool provisioning_is_needed(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(PROV_NVS_NAMESPACE, NVS_READONLY, &handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* Namespace doesn't exist - definitely needs provisioning */
        ESP_LOGI(TAG, "NVS namespace not found - provisioning needed");
        return true;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        /* On error, default to needing provisioning */
        return true;
    }

    /* Check if SSID exists and is non-empty */
    char ssid[33] = {0};
    size_t ssid_len = sizeof(ssid);
    err = nvs_get_str(handle, PROV_NVS_WIFI_SSID, ssid, &ssid_len);

    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND || ssid[0] == '\0') {
        ESP_LOGI(TAG, "WiFi SSID not configured - provisioning needed");
        return true;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS read error: %s", esp_err_to_name(err));
        return true;
    }

    ESP_LOGI(TAG, "WiFi configured (SSID: %s) - normal boot", ssid);
    return false;
}

bool provisioning_check_factory_reset(gpio_num_t dit_gpio, gpio_num_t dah_gpio, uint32_t hold_ms)
{
    ESP_LOGI(TAG, "Checking factory reset (DIT=%d, DAH=%d, hold=%lums)",
             dit_gpio, dah_gpio, (unsigned long)hold_ms);

    /* Configure GPIOs as input with pull-up */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << dit_gpio) | (1ULL << dah_gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    /* Check if both paddles are pressed (active low) */
    uint32_t elapsed_ms = 0;
    uint32_t check_interval = FACTORY_RESET_CHECK_INTERVAL_MS;

    while (elapsed_ms < hold_ms) {
        int dit_level = gpio_get_level(dit_gpio);
        int dah_level = gpio_get_level(dah_gpio);

        /* Both must be LOW (pressed) */
        if (dit_level != 0 || dah_level != 0) {
            /* Released early - not a factory reset */
            if (elapsed_ms > 0) {
                ESP_LOGI(TAG, "Paddles released after %lums - no reset", (unsigned long)elapsed_ms);
            }
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(check_interval));
        elapsed_ms += check_interval;

        /* Progress feedback every second */
        if (elapsed_ms % 1000 == 0) {
            ESP_LOGI(TAG, "Factory reset countdown: %lu/%lums",
                     (unsigned long)elapsed_ms, (unsigned long)hold_ms);
        }
    }

    /* Held for full duration - trigger factory reset */
    ESP_LOGW(TAG, "Factory reset triggered!");

    /* Clear WiFi SSID from NVS */
    nvs_handle_t handle;
    esp_err_t err = nvs_open(PROV_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_erase_key(handle, PROV_NVS_WIFI_SSID);
        nvs_erase_key(handle, PROV_NVS_WIFI_PASS);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "WiFi credentials cleared from NVS");
    } else {
        ESP_LOGE(TAG, "Failed to open NVS for reset: %s", esp_err_to_name(err));
    }

    return true;
}

/**
 * @brief Save provisioning data to NVS
 *
 * Called by HTTP handler after form submission.
 */
esp_err_t prov_save_config(const char *ssid, const char *password,
                            const char *callsign, uint8_t theme)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(PROV_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Save WiFi credentials */
    err = nvs_set_str(handle, PROV_NVS_WIFI_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save SSID: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_set_str(handle, PROV_NVS_WIFI_PASS, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save password: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    /* Enable WiFi */
    err = nvs_set_u8(handle, PROV_NVS_WIFI_EN, 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable WiFi flag: %s", esp_err_to_name(err));
        /* Non-fatal */
    }

    /* Save callsign */
    err = nvs_set_str(handle, PROV_NVS_CALLSIGN, callsign);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save callsign: %s", esp_err_to_name(err));
        /* Non-fatal */
    }

    /* Save theme */
    err = nvs_set_u8(handle, PROV_NVS_THEME, theme);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save theme: %s", esp_err_to_name(err));
        /* Non-fatal */
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Configuration saved: SSID=%s, callsign=%s, theme=%d",
             ssid, callsign, theme);
    return ESP_OK;
}

/**
 * @brief Schedule reboot after delay
 */
static void reboot_task(void *arg)
{
    uint32_t delay_ms = (uint32_t)(uintptr_t)arg;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    ESP_LOGI(TAG, "Rebooting now...");
    esp_restart();
}

void prov_schedule_reboot(uint32_t delay_ms)
{
    xTaskCreate(reboot_task, "reboot", 2048, (void *)(uintptr_t)delay_ms, 5, NULL);
}

void provisioning_start(void)
{
    ESP_LOGI(TAG, "=== PROVISIONING MODE ===");
    ESP_LOGI(TAG, "Starting AP and web server...");

    /* Start WiFi AP */
    prov_wifi_start_ap();

    /* Start HTTP server */
    prov_http_start();

    /* Set LED to blue breathing to indicate provisioning mode */
    if (led_is_initialized()) {
        led_set_state(LED_STATE_PROVISIONING);
    }

    ESP_LOGI(TAG, "Provisioning ready. Connect to 'CWKeyer-Setup' and open http://192.168.4.1");

    /* Run LED animation loop - normal bg_task doesn't run during provisioning */
    while (1) {
        if (led_is_initialized()) {
            led_tick(esp_timer_get_time(), false, false);
        }
        vTaskDelay(pdMS_TO_TICKS(20));  /* ~50 Hz update for smooth breathing */
    }
}
