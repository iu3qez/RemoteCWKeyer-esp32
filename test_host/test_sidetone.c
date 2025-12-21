/**
 * @file test_sidetone.c
 * @brief Unit tests for sidetone generator
 */

#include "unity.h"
#include "sidetone.h"
#include "stubs/esp_stubs.h"

static sidetone_gen_t s_sidetone;

void test_sidetone_init(void) {
    sidetone_init(&s_sidetone, 600, 8000, 40);  /* 600Hz, 8kHz, 5ms fade */

    TEST_ASSERT_EQUAL(8000, s_sidetone.sample_rate);
    TEST_ASSERT_EQUAL(FADE_SILENT, s_sidetone.fade_state);
    TEST_ASSERT_EQUAL(0, s_sidetone.phase);
    /* phase_inc = (freq * 2^32) / sample_rate, verify it's non-zero */
    TEST_ASSERT_NOT_EQUAL(0, s_sidetone.phase_inc);
}

void test_sidetone_keying(void) {
    sidetone_init(&s_sidetone, 600, 8000, 40);

    /* Key up - should produce silence */
    int16_t sample = sidetone_next_sample(&s_sidetone, false);
    TEST_ASSERT_EQUAL(0, sample);
    TEST_ASSERT_EQUAL(FADE_SILENT, s_sidetone.fade_state);

    /* Key down - should start producing audio */
    sample = sidetone_next_sample(&s_sidetone, true);
    TEST_ASSERT_EQUAL(FADE_IN, s_sidetone.fade_state);
    /* During fade-in, audio level increases */

    /* Generate several samples to reach sustain */
    for (int i = 0; i < 100; i++) {
        sample = sidetone_next_sample(&s_sidetone, true);
    }

    /* Should be in sustain state */
    TEST_ASSERT_EQUAL(FADE_SUSTAIN, s_sidetone.fade_state);
    /* Sample should be non-zero during sustain */
    TEST_ASSERT_NOT_EQUAL(0, sidetone_next_sample(&s_sidetone, true));
}

void test_sidetone_fade(void) {
    sidetone_init(&s_sidetone, 600, 8000, 40);

    /* Bring to sustain state */
    for (int i = 0; i < 100; i++) {
        sidetone_next_sample(&s_sidetone, true);
    }
    TEST_ASSERT_EQUAL(FADE_SUSTAIN, s_sidetone.fade_state);

    /* Key up - should start fade out */
    int16_t sample = sidetone_next_sample(&s_sidetone, false);
    TEST_ASSERT_EQUAL(FADE_OUT, s_sidetone.fade_state);
    (void)sample;

    /* Generate samples until fade out completes */
    int fade_count = 0;
    while (s_sidetone.fade_state != FADE_SILENT && fade_count < 1000) {
        sample = sidetone_next_sample(&s_sidetone, false);
        fade_count++;
    }

    /* Should reach silent state */
    TEST_ASSERT_EQUAL(FADE_SILENT, s_sidetone.fade_state);
    TEST_ASSERT_LESS_THAN(1000, fade_count);  /* Fade should complete reasonably quickly */

    /* Once silent, output should be 0 */
    sample = sidetone_next_sample(&s_sidetone, false);
    TEST_ASSERT_EQUAL(0, sample);
}
