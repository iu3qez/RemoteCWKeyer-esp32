/**
 * @file usb_winkeyer.c
 * @brief CDC2 Winkeyer3 emulation (stub)
 */

#include "usb_winkeyer.h"
#include "esp_log.h"

static const char *TAG = "usb_winkeyer";

esp_err_t usb_winkeyer_init(void) {
    ESP_LOGI(TAG, "Winkeyer3 stub initialized (not implemented)");
    /* TODO: Implement Winkeyer3 protocol on CDC2
     * - Admin commands (0x00)
     * - Speed (0x02), Sidetone (0x03), Weight (0x04)
     * - PTT timing (0x05)
     * - Clear buffer (0x0A)
     * - Message slots (0x1x)
     * - Status responses
     */
    return ESP_OK;
}

bool usb_winkeyer_is_enabled(void) {
    return false;
}
