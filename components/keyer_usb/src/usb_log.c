/**
 * @file usb_log.c
 * @brief CDC1 log drain with filtering
 */

#include "usb_log.h"
#include "usb_cdc.h"
#include "rt_log.h"

#include "tusb_cdc_acm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "usb_log";

/* Filter configuration */
#define MAX_TAG_FILTERS 16
#define MAX_TAG_LEN 16

typedef struct {
    char tag[MAX_TAG_LEN];
    log_level_t level;
} tag_filter_t;

static log_level_t s_global_level = LOG_LEVEL_INFO;
static tag_filter_t s_tag_filters[MAX_TAG_FILTERS];
static size_t s_tag_filter_count = 0;

/**
 * @brief Check if entry passes filter
 */
static bool filter_pass(const log_entry_t *entry, const char *tag) {
    /* Check tag-specific filter first */
    for (size_t i = 0; i < s_tag_filter_count; i++) {
        if (strcmp(s_tag_filters[i].tag, tag) == 0) {
            return entry->level <= s_tag_filters[i].level;
        }
    }

    /* Fall back to global level */
    return entry->level <= s_global_level;
}

esp_err_t usb_log_init(void) {
    ESP_LOGI(TAG, "Initializing USB log on CDC1");
    return ESP_OK;
}

void usb_log_task(void *arg) {
    (void)arg;

    log_entry_t entry;
    char line[160];

    ESP_LOGI(TAG, "USB log drain task started");

    for (;;) {
        /* Drain RT log stream */
        while (log_stream_drain(&g_rt_log_stream, &entry)) {
            if (!filter_pass(&entry, "RT")) {
                continue;
            }

            int len = snprintf(line, sizeof(line),
                "[%lld] %s: %.*s\r\n",
                entry.timestamp_us,
                log_level_str(entry.level),
                (int)entry.len,
                entry.msg);

            if (len > 0 && usb_cdc_connected(CDC_ITF_LOG)) {
                tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_1, (uint8_t *)line, (size_t)len);
            }
        }

        /* Drain BG log stream */
        while (log_stream_drain(&g_bg_log_stream, &entry)) {
            if (!filter_pass(&entry, "BG")) {
                continue;
            }

            int len = snprintf(line, sizeof(line),
                "[%lld] %s: %.*s\r\n",
                entry.timestamp_us,
                log_level_str(entry.level),
                (int)entry.len,
                entry.msg);

            if (len > 0 && usb_cdc_connected(CDC_ITF_LOG)) {
                tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_1, (uint8_t *)line, (size_t)len);
            }
        }

        /* Flush and yield */
        if (usb_cdc_connected(CDC_ITF_LOG)) {
            tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_1, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void usb_log_set_level(log_level_t level) {
    s_global_level = level;
}

esp_err_t usb_log_set_tag_level(const char *tag, log_level_t level) {
    /* Check if tag already exists */
    for (size_t i = 0; i < s_tag_filter_count; i++) {
        if (strcmp(s_tag_filters[i].tag, tag) == 0) {
            s_tag_filters[i].level = level;
            return ESP_OK;
        }
    }

    /* Add new tag filter */
    if (s_tag_filter_count >= MAX_TAG_FILTERS) {
        return ESP_ERR_NO_MEM;
    }

    strncpy(s_tag_filters[s_tag_filter_count].tag, tag, MAX_TAG_LEN - 1);
    s_tag_filters[s_tag_filter_count].tag[MAX_TAG_LEN - 1] = '\0';
    s_tag_filters[s_tag_filter_count].level = level;
    s_tag_filter_count++;

    return ESP_OK;
}

log_level_t usb_log_get_level(void) {
    return s_global_level;
}

void usb_log_clear_filters(void) {
    s_tag_filter_count = 0;
}
