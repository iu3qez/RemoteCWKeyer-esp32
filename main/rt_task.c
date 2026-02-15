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
#include "config.h"
#include "text_keyer.h"

/* Drift threshold: 5% */
#define DIAG_DRIFT_THRESHOLD_PCT 5

/* Audio samples per RT tick: 8000 Hz sample rate / 1000 Hz tick rate = 8 */
#define SAMPLES_PER_TICK 8

/* External globals */
extern keying_stream_t g_keying_stream;
extern fault_state_t g_fault_state;

/* Paddle state for text keyer abort (Core 1 reads this) */
atomic_bool g_paddle_active = ATOMIC_VAR_INIT(false);

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

    /* Initialize iambic processor from g_config */
    iambic_config_t iambic_cfg;
    iambic_cfg.wpm = (uint32_t)CONFIG_GET_WPM();
    iambic_cfg.mode = (iambic_mode_t)CONFIG_GET_IAMBIC_MODE();
    iambic_cfg.memory_mode = (memory_mode_t)CONFIG_GET_MEMORY_MODE();
    iambic_cfg.squeeze_mode = (squeeze_mode_t)CONFIG_GET_SQUEEZE_MODE();
    iambic_cfg.mem_window_start_pct = CONFIG_GET_MEM_WINDOW_START_PCT();
    iambic_cfg.mem_window_end_pct = CONFIG_GET_MEM_WINDOW_END_PCT();
    iambic_processor_t iambic;
    iambic_init(&iambic, &iambic_cfg);

    /* Initialize hard RT consumer */
    hard_rt_consumer_t consumer;
    hard_rt_consumer_init(&consumer, &g_keying_stream, &g_fault_state, 2);

    /* Initialize sidetone generator from config */
    sidetone_gen_t sidetone;
    uint32_t sidetone_freq = CONFIG_GET_SIDETONE_FREQ_HZ();
    uint32_t fade_ms = CONFIG_GET_FADE_DURATION_MS();
    if (fade_ms > UINT16_MAX / SAMPLES_PER_TICK) {
        fade_ms = UINT16_MAX / SAMPLES_PER_TICK;
    }
    uint16_t fade_samples = (uint16_t)(fade_ms * SAMPLES_PER_TICK);
    if (fade_samples < SAMPLES_PER_TICK) fade_samples = SAMPLES_PER_TICK;  /* Minimum 1ms fade */
    sidetone_init(&sidetone, sidetone_freq, 8000, fade_samples);

    /* Initialize PTT controller from config */
    ptt_controller_t ptt;
    ptt_init(&ptt, CONFIG_GET_PTT_TAIL_MS());

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(1);  /* 1ms tick */

    /* Log startup */
    int64_t now_us = esp_timer_get_time();
    RT_INFO(&g_rt_log_stream, now_us, "RT task started");

    /* Track config generation for hot-reload */
    uint16_t last_config_gen = atomic_load_explicit(&g_config.generation, memory_order_acquire);

    for (;;) {
        now_us = esp_timer_get_time();

        /* Check for config changes and hot-reload during IDLE */
        uint16_t current_gen = atomic_load_explicit(&g_config.generation, memory_order_acquire);
        if (current_gen != last_config_gen && iambic.state == IAMBIC_STATE_IDLE) {
            /* Reload iambic config from g_config (safe during IDLE only) */
            iambic_cfg.wpm = (uint32_t)CONFIG_GET_WPM();
            iambic_cfg.mode = (iambic_mode_t)CONFIG_GET_IAMBIC_MODE();
            iambic_cfg.memory_mode = (memory_mode_t)CONFIG_GET_MEMORY_MODE();
            iambic_cfg.squeeze_mode = (squeeze_mode_t)CONFIG_GET_SQUEEZE_MODE();
            iambic_cfg.mem_window_start_pct = CONFIG_GET_MEM_WINDOW_START_PCT();
            iambic_cfg.mem_window_end_pct = CONFIG_GET_MEM_WINDOW_END_PCT();

            /* Verify generation didn't change mid-read (optimistic read) */
            uint16_t gen_after = atomic_load_explicit(&g_config.generation, memory_order_acquire);
            if (gen_after != current_gen) {
                continue;  /* Torn read - retry next tick */
            }

            iambic_set_config(&iambic, &iambic_cfg);

            /* Reload sidetone frequency */
            uint32_t new_freq = CONFIG_GET_SIDETONE_FREQ_HZ();
            if (new_freq != sidetone_freq) {
                sidetone_set_frequency(&sidetone, new_freq);
                sidetone_freq = new_freq;
            }

            /* Reload PTT tail */
            ptt_set_tail(&ptt, CONFIG_GET_PTT_TAIL_MS());

            RT_INFO(&g_rt_log_stream, now_us, "Config updated: WPM=%lu freq=%lu",
                    (unsigned long)iambic_cfg.wpm, (unsigned long)sidetone_freq);
            last_config_gen = current_gen;
        }

        /* 1. Poll GPIO paddles */
        gpio_state_t gpio = hal_gpio_read_paddles();

        /* 1b. Override with ISR-detected presses (low latency path) */
        if (hal_gpio_consume_dit_press()) {
            gpio.bits |= GPIO_DIT_BIT;
        }
        if (hal_gpio_consume_dah_press()) {
            gpio.bits |= GPIO_DAH_BIT;
        }

        /* Update paddle active flag for text keyer abort (Core 1) */
        bool paddle_active = !gpio_is_idle(gpio);
        atomic_store_explicit(&g_paddle_active, paddle_active, memory_order_release);

        /* 2. Tick iambic FSM */
        stream_sample_t sample = iambic_tick(&iambic, now_us, gpio);

        /* 2b. Override with text keyer state if active (mutually exclusive with paddle) */
        if (text_keyer_is_key_down()) {
            sample.local_key = 1;
        }

        /* 3. Push to stream */
        if (!stream_push(&g_keying_stream, sample)) {
            fault_set(&g_fault_state, FAULT_PRODUCER_OVERRUN, 0);
        }

        /* 4. Consume for audio/TX (co-located, no context switch) */
        stream_sample_t out;
        hard_rt_result_t result = hard_rt_consumer_tick(&consumer, &out);

        /* Handle consumer result */
        switch (result) {
            case HARD_RT_OK:
                /* Update TX output */
                hal_gpio_set_tx(out.local_key != 0);
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
                /* No new data - use previous state (out is unchanged) */
                break;
        }

        /* Generate and write audio ALWAYS (even when stream empty) to maintain I2S sync */
        bool key_down = (out.local_key != 0);
        int16_t audio_samples[SAMPLES_PER_TICK];
        uint8_t volume = CONFIG_GET_SIDETONE_VOLUME();  /* 1-100 */
        for (int i = 0; i < SAMPLES_PER_TICK; i++) {
            int32_t sample = sidetone_next_sample(&sidetone, key_down);
            audio_samples[i] = (int16_t)((sample * volume) / 100);
        }

        /* DEBUG: Log when key goes down and check ALL audio samples */
        static bool prev_key = false;
        if (key_down && !prev_key) {
            RT_INFO(&g_rt_log_stream, now_us, "KEY - s:%d %d %d %d %d %d %d %d f=%d p=%d",
                    audio_samples[0], audio_samples[1], audio_samples[2], audio_samples[3],
                    audio_samples[4], audio_samples[5], audio_samples[6], audio_samples[7],
                    sidetone.fade_state, sidetone.fade_pos);
        }
        prev_key = key_down;

        /* ALWAYS write to I2S (even silence) to keep codec/I2S synchronized */
        hal_audio_write(audio_samples, SAMPLES_PER_TICK);

        /* Update PTT on key down */
        if (key_down) {
            ptt_audio_sample(&ptt, (uint64_t)now_us);
        }

        /* 5. Update PTT */
        ptt_tick(&ptt, (uint64_t)now_us);

        /* 6. Diagnostic logging (zero overhead if disabled) */
        rt_diag_log(&s_diag, &iambic, &sidetone,
                    out.local_key != 0, now_us);

        /* 7. ISR blanking timer management (must be in task context) */
        hal_gpio_isr_tick(now_us);

        /* Wait for next tick */
        vTaskDelayUntil(&last_wake, period);
    }
}
