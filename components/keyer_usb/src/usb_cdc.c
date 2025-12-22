/**
 * @file usb_cdc.c
 * @brief TinyUSB multi-CDC initialization
 */

#include "usb_cdc.h"
#include "usb_console.h"
#include "usb_log.h"
#include "usb_winkeyer.h"
#include "usb_uf2.h"

#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "esp_log.h"

static const char *TAG = "usb_cdc";

esp_err_t usb_cdc_init(void) {
    ESP_LOGI(TAG, "Initializing TinyUSB multi-CDC");

    /* TinyUSB configuration */
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,  /* Use default */
        .string_descriptor = NULL,  /* Use default */
        .external_phy = false,
        .configuration_descriptor = NULL,  /* Use default */
    };

    esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TinyUSB driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Initialize CDC-ACM for each interface */
    tinyusb_config_cdcacm_t acm_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .rx_unread_buf_sz = 256,
        .callback_rx = NULL,  /* Set by usb_console_init */
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL,
    };

    /* CDC0: Console */
    acm_cfg.cdc_port = TINYUSB_CDC_ACM_0;
    ret = tusb_cdc_acm_init(&acm_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CDC0 init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* CDC1: Log */
    acm_cfg.cdc_port = TINYUSB_CDC_ACM_1;
    ret = tusb_cdc_acm_init(&acm_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CDC1 init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Initialize sub-components */
    ret = usb_console_init();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = usb_log_init();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = usb_winkeyer_init();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = usb_uf2_init();
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "TinyUSB multi-CDC initialized");
    return ESP_OK;
}

size_t usb_cdc_write(uint8_t itf, const uint8_t *data, size_t len) {
    return tinyusb_cdcacm_write_queue((tinyusb_cdcacm_itf_t)itf, data, len);
}

void usb_cdc_flush(uint8_t itf) {
    tinyusb_cdcacm_write_flush((tinyusb_cdcacm_itf_t)itf, 0);
}

bool usb_cdc_connected(uint8_t itf) {
    return tusb_cdc_acm_initialized((tinyusb_cdcacm_itf_t)itf);
}
