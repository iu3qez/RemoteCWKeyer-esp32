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
#include "ptt.h"
#include "rt_log.h"
#include "hal_gpio.h"
#include "hal_audio.h"

/* Drift threshold: 5% */
#define DIAG_DRIFT_THRESHOLD_PCT 5

/* Audio samples per RT tick: 8000 Hz sample rate / 1000 Hz tick rate = 8 */
#define SAMPLES_PER_TICK 8

/* External globals */
extern keying_stream_t g_keying_stream;
extern fault_state_t g_fault_state;

/* ============================================================================
 * Diagnostic State Tracking
 * ============================================================================ */

/**
 * @brief Diagnostic state for detecting transitions
 */
typedef struct {
    bool prev_key_down;              /**< Previous key state */
    fade_state_t prev_fade_state;    /**< Previous sidetone fade state */
    iambic_state_t prev_iambic_state; /**< Previous iambic FSM state */
    int64_t element_start_us;        /**< When current element started */
    int64_t expected_duration_us;    /**< Expected element duration */
} rt_diag_state_t;

static rt_diag_state_t s_diag = {0};

/**
 * @brief Get expected duration for current element
 */
static int64_t get_expected_duration(const iambic_processor_t *iambic) {
    switch (iambic->state) {
        case IAMBIC_STATE_SEND_DIT:
            return iambic_dit_duration_us(&iambic->config);
        case IAMBIC_STATE_SEND_DAH:
            return iambic_dah_duration_us(&iambic->config);
        default:
            return 0;
    }
}

/**
 * @brief Log diagnostic events based on state transitions
 */
static void rt_diag_log(rt_diag_state_t *diag,
                        const iambic_processor_t *iambic,
                        const sidetone_gen_t *sidetone,
                        bool key_down,
                        int64_t now_us) {
    /* Key down transition */
    if (key_down && !diag->prev_key_down) {
        diag->element_start_us = now_us;
        diag->expected_duration_us = get_expected_duration(iambic);

        if (iambic->state == IAMBIC_STATE_SEND_DIT) {
            RT_DIAG_INFO(&g_rt_log_stream, now_us, "KEY DIT down");
        } else if (iambic->state == IAMBIC_STATE_SEND_DAH) {
            RT_DIAG_INFO(&g_rt_log_stream, now_us, "KEY DAH down");
        }
    }

    /* Key up transition */
    if (!key_down && diag->prev_key_down) {
        int64_t actual_duration = now_us - diag->element_start_us;
        RT_DIAG_INFO(&g_rt_log_stream, now_us, "KEY up %lldus",
                     (long long)actual_duration);

        /* Check for timing drift */
        if (diag->expected_duration_us > 0) {
            int64_t diff = actual_duration - diag->expected_duration_us;
            if (diff < 0) diff = -diff;
            int64_t drift_pct = (diff * 100) / diag->expected_duration_us;

            if (drift_pct > DIAG_DRIFT_THRESHOLD_PCT) {
                RT_DIAG_WARN(&g_rt_log_stream, now_us,
                             "DRIFT %lld%% (exp=%lld act=%lld)",
                             (long long)drift_pct,
                             (long long)diag->expected_duration_us,
                             (long long)actual_duration);
            }
        }
    }

    /* Memory armed (detect via iambic state) */
    if (iambic->dit_memory && diag->prev_iambic_state != iambic->state) {
        RT_DIAG_DEBUG(&g_rt_log_stream, now_us, "MEM DIT");
    }
    if (iambic->dah_memory && diag->prev_iambic_state != iambic->state) {
        RT_DIAG_DEBUG(&g_rt_log_stream, now_us, "MEM DAH");
    }

    /* Sidetone fade state transitions */
    if (sidetone->fade_state != diag->prev_fade_state) {
        const char *state_str;
        switch (sidetone->fade_state) {
            case FADE_SILENT:  state_str = "SILENT"; break;
            case FADE_IN:      state_str = "FADE_IN"; break;
            case FADE_SUSTAIN: state_str = "SUSTAIN"; break;
            case FADE_OUT:     state_str = "FADE_OUT"; break;
            default:           state_str = "?"; break;
        }
        RT_DIAG_DEBUG(&g_rt_log_stream, now_us, "TONE %s", state_str);
    }

    /* Update previous state */
    diag->prev_key_down = key_down;
    diag->prev_fade_state = sidetone->fade_state;
    diag->prev_iambic_state = iambic->state;
}

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
            case HARD_RT_OK: {
                /* Update TX output */
                hal_gpio_set_tx(out.local_key != 0);

                /* Generate sidetone samples (8 samples per 1ms tick for 8kHz) */
                bool key_down = (out.local_key != 0);
                int16_t audio_samples[SAMPLES_PER_TICK];
                for (int i = 0; i < SAMPLES_PER_TICK; i++) {
                    audio_samples[i] = sidetone_next_sample(&sidetone, key_down);
                }

                /* Write to I2S (non-blocking) */
                hal_audio_write(audio_samples, SAMPLES_PER_TICK);

                /* Update PTT on key down */
                if (key_down) {
                    ptt_audio_sample(&ptt, (uint64_t)now_us);
                }
                break;
            }

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

        /* 6. Diagnostic logging (zero overhead if disabled) */
        rt_diag_log(&s_diag, &iambic, &sidetone,
                    out.local_key != 0, now_us);

        /* Wait for next tick */
        vTaskDelayUntil(&last_wake, period);
    }
}
