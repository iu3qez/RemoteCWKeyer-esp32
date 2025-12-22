/**
 * @file usb_uf2.c
 * @brief UF2 bootloader entry via command
 */

#include "usb_uf2.h"
#include "esp_tinyuf2.h"
#include "esp_log.h"

static const char *TAG = "usb_uf2";

static void uf2_update_complete_cb(esp_tinyuf2_ota_state_t state, uint32_t size) {
    if (state == ESP_TINYUF2_OTA_COMPLETE) {
        ESP_LOGI(TAG, "UF2 update complete: %lu bytes", (unsigned long)size);
    }
}

esp_err_t usb_uf2_init(void) {
    ESP_LOGI(TAG, "Initializing UF2 support");

    tinyuf2_ota_config_t ota_config = DEFAULT_TINYUF2_OTA_CONFIG();
    ota_config.complete_cb = uf2_update_complete_cb;
    ota_config.if_restart = true;

    esp_err_t ret = esp_tinyuf2_install(&ota_config, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_tinyuf2_install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

void usb_uf2_enter(void) {
    ESP_LOGI(TAG, "Entering UF2 bootloader mode...");
    esp_restart_from_tinyuf2();
    /* Does not return */
}
