/**
 * @file usb_uf2.c
 * @brief UF2 bootloader entry via command
 *
 * Note: esp_tinyuf2 conflicts with esp_tinyusb (both include TinyUSB).
 * For now, we use a simple reboot to ROM bootloader approach.
 * Full UF2 support can be added later by resolving the TinyUSB conflict.
 */

#include "usb_uf2.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "usb_uf2";

esp_err_t usb_uf2_init(void) {
    ESP_LOGI(TAG, "UF2 stub initialized (reboot to ROM bootloader)");
    return ESP_OK;
}

void usb_uf2_enter(void) {
    ESP_LOGI(TAG, "Rebooting to ROM bootloader...");
    /* Reboot - user can then flash via esptool or USB download mode */
    esp_restart();
    /* Does not return */
}
