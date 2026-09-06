/**
 * @file usb_console.c
 * @brief CDC0 console with immediate echo
 */

#include "usb_console.h"
#include "usb_cdc.h"
#include "console.h"

#include "tusb_cdc_acm.h"
#include "esp_log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "usb_console";

/**
 * @brief RX callback for CDC0
 *
 * Bytes go straight into the console state machine, which draws them.
 * This callback deliberately does not echo: an echo here would be a second
 * judgement of the same keystroke, made without sight of the line buffer.
 */
static void console_rx_callback(int itf, cdcacm_event_t *event) {
    (void)event;

    if (itf != TINYUSB_CDC_ACM_0) {
        return;
    }

    uint8_t buf[64];
    size_t len = 0;

    esp_err_t ret = tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, buf, sizeof(buf), &len);
    if (ret != ESP_OK || len == 0) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        /* Push to console state machine (it echoes by repainting the line) */
        if (console_push_char((char)buf[i])) {
            usb_console_prompt();
        }
    }

    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
}

esp_err_t usb_console_init(void) {
    ESP_LOGI(TAG, "Initializing USB console on CDC0");

    /* Register RX callback */
    tinyusb_cdcacm_register_callback(
        TINYUSB_CDC_ACM_0,
        CDC_EVENT_RX,
        console_rx_callback
    );

    return ESP_OK;
}

void usb_console_print(const char *str) {
    size_t len = strlen(str);
    tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (const uint8_t *)str, len);
    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
}

void usb_console_printf(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len > 0) {
        tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (const uint8_t *)buf, (size_t)len);
        tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
    }
}

void usb_console_prompt(void) {
    console_print_prompt();
}
