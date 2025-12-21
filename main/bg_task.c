/**
 * @file bg_task.c
 * @brief Background task (Core 1)
 *
 * Best-effort processing:
 * - Remote CW forwarder
 * - Morse decoder
 * - Diagnostics
 *
 * Runs on Core 1 with normal priority.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <inttypes.h>

#include "keyer_core.h"
#include "rt_log.h"

/* External globals */
extern keying_stream_t g_keying_stream;
extern fault_state_t g_fault_state;

void bg_task(void *arg) {
    (void)arg;

    /* Initialize best-effort consumer */
    best_effort_consumer_t consumer;
    best_effort_consumer_init(&consumer, &g_keying_stream, 100);  /* Skip if >100 behind */

    /* Log startup */
    int64_t now_us = esp_timer_get_time();
    RT_INFO(&g_bg_log_stream, now_us, "BG task started");

    uint32_t stats_counter = 0;

    for (;;) {
        now_us = esp_timer_get_time();

        /* Process stream samples (best-effort) */
        stream_sample_t sample;
        while (best_effort_consumer_tick(&consumer, &sample)) {
            /* TODO: Process sample for remote/decoder */
            (void)sample;
        }

        /* Periodic stats logging */
        stats_counter++;
        if (stats_counter >= 10000) {  /* Every ~10 seconds at 1ms tick */
            size_t dropped = best_effort_consumer_dropped(&consumer);
            if (dropped > 0) {
                RT_WARN(&g_bg_log_stream, now_us, "BG consumer dropped: %u", (unsigned)dropped);
                best_effort_consumer_reset_dropped(&consumer);
            }

            /* Check fault state */
            if (fault_is_active(&g_fault_state)) {
                RT_ERROR(&g_bg_log_stream, now_us, "FAULT active: %s (count=%" PRIu32 ")",
                         fault_code_str(fault_get_code(&g_fault_state)),
                         fault_get_count(&g_fault_state));
            }

            stats_counter = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
