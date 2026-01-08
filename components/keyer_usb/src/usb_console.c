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
 * @brief Echo single character to CDC0
 */
static void echo_char(char c) {
    uint8_t buf[4];
    size_t len = 0;

    if (c >= 0x20 && c <= 0x7E) {
        /* Printable character */
        buf[0] = (uint8_t)c;
        len = 1;
    } else if (c == '\r' || c == '\n') {
        /* Enter - echo CRLF */
        buf[0] = '\r';
        buf[1] = '\n';
        len = 2;
    } else if (c == 0x08 || c == 0x7F) {
        /* Backspace or DEL - destructive backspace */
        buf[0] = '\b';
        buf[1] = ' ';
        buf[2] = '\b';
        len = 3;
    }
    /* Ctrl+C, Ctrl+U: no echo */

    if (len > 0) {
        tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, buf, len);
    }
}

/**
 * @brief RX callback for CDC0 - immediate echo
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
        char c = (char)buf[i];

        /* Echo immediately */
        echo_char(c);

        /* Push to console state machine */
        bool cmd_complete = console_push_char(c);
        if (cmd_complete) {
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
