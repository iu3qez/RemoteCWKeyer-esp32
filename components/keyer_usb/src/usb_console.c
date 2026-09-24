/**
 * @file usb_console.c
 * @brief CDC0 console transport
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

/**
 * @brief Previous DTR level on CDC0, for rising-edge detection
 *
 * tud_cdc_line_state_cb() fires on every SET_CONTROL_LINE_STATE request the
 * host sends (tinyusb cdc_device.c:436), and a host may re-assert an already
 * asserted DTR. Only the false->true transition is an attach.
 *
 * Written and read exclusively from the TinyUSB task, so no atomics needed.
 */
static bool s_dtr_prev = false;

/**
 * @brief Line-state callback for CDC0 - welcome the operator on attach
 *
 * TinyUSB treats DTR alone as "connected" (cdc_device.c:144-149), so the
 * rising edge of DTR is the attach event. RTS is not required: some terminals
 * assert DTR only.
 *
 * Runs in the TinyUSB task (Core 1 by default, CONFIG_TINYUSB_TASK_AFFINITY),
 * the same context in which console_rx_callback() already runs whole console
 * commands. Nothing here touches the Core 0 RT path.
 */
static void console_line_state_callback(int itf, cdcacm_event_t *event) {
    if (itf != TINYUSB_CDC_ACM_0 || event == NULL) {
        return;
    }

    const bool dtr = event->line_state_changed_data.dtr;
    const bool rising = dtr && !s_dtr_prev;
    s_dtr_prev = dtr;

    if (rising) {
        console_print_welcome();
    }
}

esp_err_t usb_console_init(void) {
    ESP_LOGI(TAG, "Initializing USB console on CDC0");

    /* Register RX callback */
    tinyusb_cdcacm_register_callback(
        TINYUSB_CDC_ACM_0,
        CDC_EVENT_RX,
        console_rx_callback
    );

    /* Register line-state callback: prints the welcome on every attach */
    tinyusb_cdcacm_register_callback(
        TINYUSB_CDC_ACM_0,
        CDC_EVENT_LINE_STATE_CHANGED,
        console_line_state_callback
    );

    return ESP_OK;
}

void usb_console_print(const char *str) {
    size_t len = strlen(str);
    tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (const uint8_t *)str, len);
    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
}

/**
 * @brief Formatting buffer for usb_console_printf()
 *
 * Static rather than on the stack: console commands run in the TinyUSB task,
 * whose stack is CONFIG_TINYUSB_TASK_STACK_SIZE (4096 bytes).
 *
 * usb_console_printf() is reached only through keyer_console's printf, and
 * keyer_console runs only from console_rx_callback() and
 * console_line_state_callback(), both in the TinyUSB task (console_task() is
 * never started). One task, so no lock is needed.
 */
static char s_printf_buf[USB_CONSOLE_PRINTF_BUF_SIZE];

void usb_console_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(s_printf_buf, sizeof(s_printf_buf), fmt, args);
    va_end(args);

    if (len <= 0) {
        return;
    }

    /* vsnprintf() returns the length the whole output would have had, not
     * the number of bytes it wrote. Send only what is in the buffer. */
    size_t n = (size_t)len;
    if (n >= sizeof(s_printf_buf)) {
        n = sizeof(s_printf_buf) - 1;
    }

    tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (const uint8_t *)s_printf_buf, n);
    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
}

void usb_console_prompt(void) {
    console_print_prompt();
}
