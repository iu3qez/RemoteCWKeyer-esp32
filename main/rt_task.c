/**
 * @file rt_task.c
 * @brief Real-time task (Core 0)
 *
 * Hard real-time keying loop:
 * GPIO Poll → Iambic FSM → Stream Push → Audio/TX Consume
 *
 * ARCHITECTURE.md compliance:
 * - Runs on Core 0 with highest priority
 * - Maximum latency: 100µs
 * - Zero context switches in hot path
 * - No blocking calls, no heap allocation
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include "keyer_core.h"
#include "iambic.h"
#include "sidetone.h"
#include "audio_buffer.h"
#include "ptt.h"
#include "rt_log.h"
#include "hal_gpio.h"

/* External globals */
extern keying_stream_t g_keying_stream;
extern fault_state_t g_fault_state;

/* Audio buffer for I2S output */
#define AUDIO_BUFFER_SIZE 64
static int16_t s_audio_buffer_storage[AUDIO_BUFFER_SIZE];
static audio_ring_buffer_t s_audio_buffer;

void rt_task(void *arg) {
    (void)arg;

    /* Initialize iambic processor */
    iambic_config_t iambic_cfg = IAMBIC_CONFIG_DEFAULT;
    iambic_cfg.wpm = 25;
    iambic_processor_t iambic;
    iambic_init(&iambic, &iambic_cfg);

    /* Initialize hard RT consumer */
    hard_rt_consumer_t consumer;
    hard_rt_consumer_init(&consumer, &g_keying_stream, &g_fault_state, 2);

    /* Initialize sidetone generator */
    sidetone_gen_t sidetone;
    sidetone_init(&sidetone, 600, 8000, 40);  /* 600Hz, 8kHz sample rate, 5ms fade */

    /* Initialize PTT controller */
    ptt_controller_t ptt;
    ptt_init(&ptt, 100);  /* 100ms tail */

    /* Initialize audio buffer */
    audio_buffer_init(&s_audio_buffer, s_audio_buffer_storage, AUDIO_BUFFER_SIZE);

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(1);  /* 1ms tick */

    /* Log startup */
    int64_t now_us = esp_timer_get_time();
    RT_INFO(&g_rt_log_stream, now_us, "RT task started");

    for (;;) {
        now_us = esp_timer_get_time();

        /* 1. Poll GPIO paddles */
        gpio_state_t gpio = hal_gpio_read_paddles();

        /* 2. Tick iambic FSM */
        stream_sample_t sample = iambic_tick(&iambic, now_us, gpio);

        /* 3. Push to stream */
        if (!stream_push(&g_keying_stream, sample)) {
            fault_set(&g_fault_state, FAULT_PRODUCER_OVERRUN, 0);
        }

        /* 4. Consume for audio/TX (co-located, no context switch) */
        stream_sample_t out;
        hard_rt_result_t result = hard_rt_consumer_tick(&consumer, &out);

        switch (result) {
            case HARD_RT_OK:
                /* Update TX output */
                hal_gpio_set_tx(out.local_key != 0);

                /* Generate sidetone if key down */
                if (out.local_key) {
                    int16_t audio = sidetone_next_sample(&sidetone, true);
                    audio_buffer_push(&s_audio_buffer, audio);
                    ptt_audio_sample(&ptt, (uint64_t)now_us);
                } else {
                    /* Key up - fade out */
                    int16_t audio = sidetone_next_sample(&sidetone, false);
                    if (audio != 0) {
                        audio_buffer_push(&s_audio_buffer, audio);
                    }
                }
                break;

            case HARD_RT_FAULT:
                /* FAULT - stop TX/audio immediately */
                hal_gpio_set_tx(false);
                sidetone_reset(&sidetone);
                ptt_force_off(&ptt);
                RT_ERROR(&g_rt_log_stream, now_us, "FAULT: %s",
                         fault_code_str(fault_get_code(&g_fault_state)));
                break;

            case HARD_RT_NO_DATA:
                /* No new data - continue */
                break;
        }

        /* 5. Update PTT */
        ptt_tick(&ptt, (uint64_t)now_us);

        /* Wait for next tick */
        vTaskDelayUntil(&last_wake, period);
    }
}
