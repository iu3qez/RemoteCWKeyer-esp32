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
#include "decoder.h"
#include "text_keyer.h"
#include "text_memory.h"

/* External globals */
extern keying_stream_t g_keying_stream;
extern fault_state_t g_fault_state;

/* Paddle state for text keyer abort (from rt_task.c) */
extern atomic_bool g_paddle_active;

void bg_task(void *arg) {
    (void)arg;

    /* Initialize decoder (creates its own stream consumer) */
    decoder_init();

    /* Initialize text keyer */
    text_keyer_config_t text_cfg = {
        .stream = &g_keying_stream,
        .paddle_abort = &g_paddle_active,
    };
    text_keyer_init(&text_cfg);
    text_memory_init();

    /* Log startup */
    int64_t now_us = esp_timer_get_time();
    RT_INFO(&g_bg_log_stream, now_us, "BG task started (text keyer ready)");

    uint32_t stats_counter = 0;

    for (;;) {
        now_us = esp_timer_get_time();

        /* Process decoder (reads from keying_stream) */
        decoder_process();

        /* Tick text keyer (produces samples on keying_stream) */
        text_keyer_tick(now_us);

        /* Periodic stats logging */
        stats_counter++;
        if (stats_counter >= 10000) {  /* Every ~10 seconds at 1ms tick */
            /* Log decoder stats if active */
            if (decoder_is_enabled()) {
                decoder_stats_t stats;
                decoder_get_stats(&stats);
                if (stats.samples_dropped > 0) {
                    RT_WARN(&g_bg_log_stream, now_us, "Decoder dropped: %u samples",
                            (unsigned)stats.samples_dropped);
                }
            }

            /* Check fault state */
            if (fault_is_active(&g_fault_state)) {
                RT_ERROR(&g_bg_log_stream, now_us, "FAULT active: %s (count=%" PRIu32 ")",
                         fault_code_str(fault_get_code(&g_fault_state)),
                         fault_get_count(&g_fault_state));
            }

            stats_counter = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(10));  /* 100Hz - decoder doesn't need 1kHz */
    }
}
