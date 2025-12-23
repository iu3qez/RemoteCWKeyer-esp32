/**
 * @file audio_test.c
 * @brief Audio test - continuous 600Hz tone
 *
 * Minimal test to verify ES8311 codec output.
 * Generates continuous 600Hz sine wave at 8kHz sample rate.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "hal_audio.h"
#include <math.h>

static const char *TAG = "audio_test";

#define SAMPLE_RATE 8000
#define TONE_FREQ 600
#define AMPLITUDE 20000  /* ~60% of max (32767) */

void audio_test_task(void *arg) {
    (void)arg;

    ESP_LOGI(TAG, "Audio test started - generating 600Hz tone");

    /* Phase accumulator for sine generation */
    float phase = 0.0f;
    float phase_step = (2.0f * M_PI * TONE_FREQ) / SAMPLE_RATE;

    /* Buffer for one tick (1ms = 8 samples at 8kHz) */
    int16_t samples[8];

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(1);  /* 1ms */

    for (;;) {
        /* Generate 8 samples */
        for (int i = 0; i < 8; i++) {
            samples[i] = (int16_t)(sinf(phase) * AMPLITUDE);
            phase += phase_step;
            if (phase >= 2.0f * M_PI) {
                phase -= 2.0f * M_PI;
            }
        }

        /* Write to codec */
        size_t written = hal_audio_write(samples, 8);
        if (written != 8) {
            ESP_LOGW(TAG, "Audio write failed: %zu/8", written);
        }

        /* Wait for next tick */
        vTaskDelayUntil(&last_wake, period);
    }
}

void start_audio_test(void) {
    xTaskCreatePinnedToCore(
        audio_test_task,
        "audio_test",
        4096,
        NULL,
        5,  /* Medium priority */
        NULL,
        1   /* Core 1 (not RT core) */
    );
}