/**
 * @file test_timing_classifier.c
 * @brief Unit tests for timing classifier
 */

#include "unity.h"
#include "timing_classifier.h"

static timing_classifier_t s_tc;

/* At 20 WPM, dit = 60ms = 60000us */
#define DIT_20WPM_US 60000
#define DAH_20WPM_US (DIT_20WPM_US * 3)

void test_timing_init(void) {
    timing_classifier_init(&s_tc, 20.0f);

    /* Initial dit should be around 60ms for 20 WPM */
    TEST_ASSERT_INT64_WITHIN(1000, DIT_20WPM_US, s_tc.dit_avg_us);
    TEST_ASSERT_INT64_WITHIN(3000, DAH_20WPM_US, s_tc.dah_avg_us);
    TEST_ASSERT_EQUAL(0, s_tc.dit_count);
    TEST_ASSERT_EQUAL(0, s_tc.dah_count);
    TEST_ASSERT_FALSE(timing_classifier_is_calibrated(&s_tc));
}

void test_timing_classify_dit(void) {
    timing_classifier_init(&s_tc, 20.0f);

    /* A 60ms mark should classify as dit */
    key_event_t event = timing_classifier_classify(&s_tc, DIT_20WPM_US, true);
    TEST_ASSERT_EQUAL(KEY_EVENT_DIT, event);
    TEST_ASSERT_EQUAL(1, s_tc.dit_count);

    /* A 50ms mark should also be dit */
    event = timing_classifier_classify(&s_tc, 50000, true);
    TEST_ASSERT_EQUAL(KEY_EVENT_DIT, event);
    TEST_ASSERT_EQUAL(2, s_tc.dit_count);
}

void test_timing_classify_dah(void) {
    timing_classifier_init(&s_tc, 20.0f);

    /* A 180ms mark (3x dit) should classify as dah */
    key_event_t event = timing_classifier_classify(&s_tc, DAH_20WPM_US, true);
    TEST_ASSERT_EQUAL(KEY_EVENT_DAH, event);
    TEST_ASSERT_EQUAL(1, s_tc.dah_count);

    /* A 200ms mark should also be dah */
    event = timing_classifier_classify(&s_tc, 200000, true);
    TEST_ASSERT_EQUAL(KEY_EVENT_DAH, event);
    TEST_ASSERT_EQUAL(2, s_tc.dah_count);
}

void test_timing_classify_gaps(void) {
    timing_classifier_init(&s_tc, 20.0f);

    /* Intra-character gap: ~1 dit (< 2 dit) */
    key_event_t event = timing_classifier_classify(&s_tc, DIT_20WPM_US, false);
    TEST_ASSERT_EQUAL(KEY_EVENT_INTRA_GAP, event);

    /* Character gap: ~3 dit (2-5 dit) */
    event = timing_classifier_classify(&s_tc, DIT_20WPM_US * 3, false);
    TEST_ASSERT_EQUAL(KEY_EVENT_CHAR_GAP, event);

    /* Word gap: ~7 dit (> 5 dit) */
    event = timing_classifier_classify(&s_tc, DIT_20WPM_US * 7, false);
    TEST_ASSERT_EQUAL(KEY_EVENT_WORD_GAP, event);
}

void test_timing_warmup(void) {
    timing_classifier_init(&s_tc, 20.0f);

    TEST_ASSERT_FALSE(timing_classifier_is_calibrated(&s_tc));
    TEST_ASSERT_EQUAL(0, timing_classifier_get_wpm(&s_tc));

    /* Send 3 marks to complete warmup */
    timing_classifier_classify(&s_tc, DIT_20WPM_US, true);
    TEST_ASSERT_FALSE(timing_classifier_is_calibrated(&s_tc));

    timing_classifier_classify(&s_tc, DIT_20WPM_US, true);
    TEST_ASSERT_FALSE(timing_classifier_is_calibrated(&s_tc));

    timing_classifier_classify(&s_tc, DIT_20WPM_US, true);
    TEST_ASSERT_TRUE(timing_classifier_is_calibrated(&s_tc));

    /* Now WPM should be reported */
    uint32_t wpm = timing_classifier_get_wpm(&s_tc);
    TEST_ASSERT_INT_WITHIN(2, 20, (int)wpm);
}

void test_timing_ema_adaptation(void) {
    timing_classifier_init(&s_tc, 20.0f);

    /* Start with 20 WPM baseline */
    int64_t initial_dit = s_tc.dit_avg_us;

    /* Send faster dits (25 WPM = 48ms) */
    for (int i = 0; i < 10; i++) {
        timing_classifier_classify(&s_tc, 48000, true);
    }

    /* Dit average should have decreased (faster) */
    TEST_ASSERT_LESS_THAN(initial_dit, s_tc.dit_avg_us);

    /* WPM should have increased */
    uint32_t wpm = timing_classifier_get_wpm(&s_tc);
    TEST_ASSERT_GREATER_THAN(20, wpm);
}

void test_timing_ratio(void) {
    timing_classifier_init(&s_tc, 20.0f);

    /* Initial ratio should be ~3.0 */
    float ratio = timing_classifier_get_ratio(&s_tc);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 3.0f, ratio);
}

void test_timing_ignore_short_durations(void) {
    timing_classifier_init(&s_tc, 20.0f);

    /* Very short durations (< 5ms) should be ignored */
    key_event_t event = timing_classifier_classify(&s_tc, 1000, true);
    TEST_ASSERT_EQUAL(KEY_EVENT_UNKNOWN, event);
    TEST_ASSERT_EQUAL(0, s_tc.dit_count);
}

void test_timing_ignore_long_durations(void) {
    timing_classifier_init(&s_tc, 20.0f);

    /* Very long durations (> 5s) should be ignored */
    key_event_t event = timing_classifier_classify(&s_tc, 6000000, true);
    TEST_ASSERT_EQUAL(KEY_EVENT_UNKNOWN, event);
    TEST_ASSERT_EQUAL(0, s_tc.dit_count);
}

void test_timing_reset(void) {
    timing_classifier_init(&s_tc, 20.0f);

    /* Calibrate */
    for (int i = 0; i < 5; i++) {
        timing_classifier_classify(&s_tc, DIT_20WPM_US, true);
    }
    TEST_ASSERT_TRUE(timing_classifier_is_calibrated(&s_tc));

    /* Reset */
    timing_classifier_reset(&s_tc, 15.0f);  /* Reset to 15 WPM */

    TEST_ASSERT_FALSE(timing_classifier_is_calibrated(&s_tc));
    TEST_ASSERT_EQUAL(0, s_tc.dit_count);
    /* Dit should be longer now (15 WPM = 80ms) */
    TEST_ASSERT_INT64_WITHIN(1000, 80000, s_tc.dit_avg_us);
}

void test_timing_null_safety(void) {
    /* Should not crash with NULL */
    timing_classifier_init(NULL, 20.0f);
    key_event_t event = timing_classifier_classify(NULL, 60000, true);
    TEST_ASSERT_EQUAL(KEY_EVENT_UNKNOWN, event);
    TEST_ASSERT_EQUAL(0, timing_classifier_get_wpm(NULL));
    TEST_ASSERT_FALSE(timing_classifier_is_calibrated(NULL));
}

void test_key_event_str(void) {
    TEST_ASSERT_EQUAL_STRING("DIT", key_event_str(KEY_EVENT_DIT));
    TEST_ASSERT_EQUAL_STRING("DAH", key_event_str(KEY_EVENT_DAH));
    TEST_ASSERT_EQUAL_STRING("INTRA_GAP", key_event_str(KEY_EVENT_INTRA_GAP));
    TEST_ASSERT_EQUAL_STRING("CHAR_GAP", key_event_str(KEY_EVENT_CHAR_GAP));
    TEST_ASSERT_EQUAL_STRING("WORD_GAP", key_event_str(KEY_EVENT_WORD_GAP));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", key_event_str(KEY_EVENT_UNKNOWN));
}
