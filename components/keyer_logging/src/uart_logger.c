/**
 * @file uart_logger.c
 * @brief UART log drain task
 *
 * Runs on Core 1, drains log streams to UART on GPIO6.
 */

#include "rt_log.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#ifdef ESP_PLATFORM
/* ESP-IDF includes */
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#define UART_LOG_PORT    UART_NUM_1
#define UART_LOG_TX_PIN  GPIO_NUM_6
#define UART_LOG_BAUD    115200
#define UART_BUF_SIZE    256

static char s_format_buf[256];

/**
 * @brief Initialize UART logger
 */
void uart_logger_init(void) {
    uart_config_t uart_config = {
        .baud_rate = UART_LOG_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    /* Configure UART */
    uart_driver_install(UART_LOG_PORT, UART_BUF_SIZE, 0, 0, NULL, 0);
    uart_param_config(UART_LOG_PORT, &uart_config);
    uart_set_pin(UART_LOG_PORT, UART_LOG_TX_PIN, UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

/**
 * @brief Format and send log entry to UART
 */
static void send_entry(const log_entry_t *entry) {
    /* Format: [timestamp_us] LEVEL: message\r\n */
    int len = snprintf(s_format_buf, sizeof(s_format_buf),
                       "[%lld] %s: %.*s\r\n",
                       (long long)entry->timestamp_us,
                       log_level_str(entry->level),
                       (int)entry->len,
                       entry->msg);

    if (len > 0) {
        uart_write_bytes(UART_LOG_PORT, s_format_buf, (size_t)len);
    }
}

/**
 * @brief UART logger task
 *
 * Drains RT and BG log streams to UART.
 * Runs on Core 1, priority normal.
 */
void uart_logger_task(void *arg) {
    (void)arg;

    log_entry_t entry;
    uint32_t last_dropped_report_ms = 0;

    for (;;) {
        bool had_entry = false;

        /* Drain RT log stream (higher priority) */
        while (log_stream_drain(&g_rt_log_stream, &entry)) {
            send_entry(&entry);
            had_entry = true;
        }

        /* Drain BG log stream */
        while (log_stream_drain(&g_bg_log_stream, &entry)) {
            send_entry(&entry);
            had_entry = true;
        }

        /* Report dropped messages periodically */
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (now_ms - last_dropped_report_ms >= 10000) {
            uint32_t rt_dropped = log_stream_dropped(&g_rt_log_stream);
            uint32_t bg_dropped = log_stream_dropped(&g_bg_log_stream);

            if (rt_dropped > 0 || bg_dropped > 0) {
                int len = snprintf(s_format_buf, sizeof(s_format_buf),
                                   "[%" PRIu32 "] WARN: Dropped logs: RT=%" PRIu32 " BG=%" PRIu32 "\r\n",
                                   now_ms, rt_dropped, bg_dropped);
                if (len > 0) {
                    uart_write_bytes(UART_LOG_PORT, s_format_buf, (size_t)len);
                }

                log_stream_reset_dropped(&g_rt_log_stream);
                log_stream_reset_dropped(&g_bg_log_stream);
            }

            last_dropped_report_ms = now_ms;
        }

        /* Sleep if no entries */
        if (!had_entry) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

#else
/* Host stub */

void uart_logger_init(void) {
    /* No-op on host */
}

void uart_logger_task(void *arg) {
    (void)arg;
    /* No-op on host */
}

#endif /* ESP_PLATFORM */
