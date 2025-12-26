/**
 * @file test_decoder.c
 * @brief Unit tests for CW decoder
 */

#include "unity.h"
#include "decoder.h"
#include "timing_classifier.h"

void test_decoder_init(void) {
    decoder_init();

    TEST_ASSERT_TRUE(decoder_is_enabled());
    TEST_ASSERT_EQUAL(DECODER_STATE_IDLE, decoder_get_state());
    TEST_ASSERT_EQUAL(0, decoder_get_buffer_count());
    TEST_ASSERT_EQUAL(128, decoder_get_buffer_capacity());
}

void test_decoder_decode_letter_a(void) {
    decoder_reset();

    /* 'A' = .- (dit, intra-gap, dah, char-gap) */
    decoder_handle_event(KEY_EVENT_DIT, 1000);
    TEST_ASSERT_EQUAL(DECODER_STATE_RECEIVING, decoder_get_state());

    decoder_handle_event(KEY_EVENT_INTRA_GAP, 2000);
    TEST_ASSERT_EQUAL(DECODER_STATE_RECEIVING, decoder_get_state());

    decoder_handle_event(KEY_EVENT_DAH, 3000);
    TEST_ASSERT_EQUAL(DECODER_STATE_RECEIVING, decoder_get_state());

    /* Char gap finalizes the character */
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 4000);
    TEST_ASSERT_EQUAL(DECODER_STATE_IDLE, decoder_get_state());

    /* Should have decoded 'A' */
    decoded_char_t last = decoder_get_last_char();
    TEST_ASSERT_EQUAL_CHAR('A', last.character);
    TEST_ASSERT_EQUAL(4000, last.timestamp_us);
}

void test_decoder_decode_letter_e(void) {
    decoder_reset();

    /* 'E' = . (just one dit) */
    decoder_handle_event(KEY_EVENT_DIT, 1000);
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 2000);

    decoded_char_t last = decoder_get_last_char();
    TEST_ASSERT_EQUAL_CHAR('E', last.character);
}

void test_decoder_decode_letter_t(void) {
    decoder_reset();

    /* 'T' = - (just one dah) */
    decoder_handle_event(KEY_EVENT_DAH, 1000);
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 2000);

    decoded_char_t last = decoder_get_last_char();
    TEST_ASSERT_EQUAL_CHAR('T', last.character);
}

void test_decoder_decode_sos(void) {
    decoder_reset();

    /* S = ... */
    decoder_handle_event(KEY_EVENT_DIT, 1000);
    decoder_handle_event(KEY_EVENT_INTRA_GAP, 1500);
    decoder_handle_event(KEY_EVENT_DIT, 2000);
    decoder_handle_event(KEY_EVENT_INTRA_GAP, 2500);
    decoder_handle_event(KEY_EVENT_DIT, 3000);
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 4000);

    /* O = --- */
    decoder_handle_event(KEY_EVENT_DAH, 5000);
    decoder_handle_event(KEY_EVENT_INTRA_GAP, 6000);
    decoder_handle_event(KEY_EVENT_DAH, 7000);
    decoder_handle_event(KEY_EVENT_INTRA_GAP, 8000);
    decoder_handle_event(KEY_EVENT_DAH, 9000);
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 10000);

    /* S = ... */
    decoder_handle_event(KEY_EVENT_DIT, 11000);
    decoder_handle_event(KEY_EVENT_INTRA_GAP, 11500);
    decoder_handle_event(KEY_EVENT_DIT, 12000);
    decoder_handle_event(KEY_EVENT_INTRA_GAP, 12500);
    decoder_handle_event(KEY_EVENT_DIT, 13000);
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 14000);

    /* Should have "SOS" */
    char buf[16];
    size_t len = decoder_get_text(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(3, len);
    TEST_ASSERT_EQUAL_STRING("SOS", buf);
}

void test_decoder_word_gap_adds_space(void) {
    decoder_reset();

    /* H = .... */
    decoder_handle_event(KEY_EVENT_DIT, 1000);
    decoder_handle_event(KEY_EVENT_INTRA_GAP, 1100);
    decoder_handle_event(KEY_EVENT_DIT, 1200);
    decoder_handle_event(KEY_EVENT_INTRA_GAP, 1300);
    decoder_handle_event(KEY_EVENT_DIT, 1400);
    decoder_handle_event(KEY_EVENT_INTRA_GAP, 1500);
    decoder_handle_event(KEY_EVENT_DIT, 1600);
    decoder_handle_event(KEY_EVENT_WORD_GAP, 2000);  /* Word gap after H */

    /* I = .. */
    decoder_handle_event(KEY_EVENT_DIT, 3000);
    decoder_handle_event(KEY_EVENT_INTRA_GAP, 3100);
    decoder_handle_event(KEY_EVENT_DIT, 3200);
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 4000);

    /* Should have "H I" */
    char buf[16];
    decoder_get_text(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("H I", buf);
}

void test_decoder_get_current_pattern(void) {
    decoder_reset();

    /* Start receiving a pattern */
    decoder_handle_event(KEY_EVENT_DIT, 1000);
    decoder_handle_event(KEY_EVENT_INTRA_GAP, 1500);
    decoder_handle_event(KEY_EVENT_DAH, 2000);

    char pattern[16];
    size_t len = decoder_get_current_pattern(pattern, sizeof(pattern));

    TEST_ASSERT_EQUAL(2, len);
    TEST_ASSERT_EQUAL_STRING(".-", pattern);
}

void test_decoder_unknown_pattern(void) {
    decoder_reset();

    /* Invalid pattern (10 dits - no such letter) */
    for (int i = 0; i < 10; i++) {
        decoder_handle_event(KEY_EVENT_DIT, (int64_t)(1000 + i * 100));
        if (i < 9) {
            decoder_handle_event(KEY_EVENT_INTRA_GAP, (int64_t)(1050 + i * 100));
        }
    }
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 2000);

    /* Should not have added a character (or added error marker) */
    decoder_stats_t stats;
    decoder_get_stats(&stats);
    TEST_ASSERT_GREATER_THAN(0, stats.errors);
}

void test_decoder_enable_disable(void) {
    decoder_init();

    TEST_ASSERT_TRUE(decoder_is_enabled());

    decoder_set_enabled(false);
    TEST_ASSERT_FALSE(decoder_is_enabled());

    decoder_set_enabled(true);
    TEST_ASSERT_TRUE(decoder_is_enabled());
}

void test_decoder_reset(void) {
    decoder_init();

    /* Add some data */
    decoder_handle_event(KEY_EVENT_DIT, 1000);
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 2000);

    TEST_ASSERT_GREATER_THAN(0, decoder_get_buffer_count());

    /* Reset */
    decoder_reset();

    TEST_ASSERT_EQUAL(0, decoder_get_buffer_count());
    TEST_ASSERT_EQUAL(DECODER_STATE_IDLE, decoder_get_state());
}

void test_decoder_stats(void) {
    decoder_reset();

    /* Decode a few characters */
    decoder_handle_event(KEY_EVENT_DIT, 1000);
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 2000);  /* E */

    decoder_handle_event(KEY_EVENT_DAH, 3000);
    decoder_handle_event(KEY_EVENT_WORD_GAP, 4000);  /* T + space */

    decoder_stats_t stats;
    decoder_get_stats(&stats);

    TEST_ASSERT_EQUAL(2, stats.chars_decoded);
    TEST_ASSERT_EQUAL(1, stats.words_decoded);
}

void test_decoder_timing_access(void) {
    decoder_init();

    const timing_classifier_t *tc = decoder_get_timing();
    TEST_ASSERT_NOT_NULL(tc);
    /* Should have default ~20 WPM timing */
    TEST_ASSERT_INT64_WITHIN(10000, 60000, tc->dit_avg_us);
}

void test_decoder_state_str(void) {
    TEST_ASSERT_EQUAL_STRING("IDLE", decoder_state_str(DECODER_STATE_IDLE));
    TEST_ASSERT_EQUAL_STRING("RECEIVING", decoder_state_str(DECODER_STATE_RECEIVING));
}

void test_decoder_buffer_circular(void) {
    decoder_reset();

    /* Fill buffer with characters beyond capacity */
    for (int i = 0; i < 150; i++) {
        decoder_handle_event(KEY_EVENT_DIT, (int64_t)(i * 1000));
        decoder_handle_event(KEY_EVENT_CHAR_GAP, (int64_t)(i * 1000 + 500));
    }

    /* Buffer should be at capacity */
    TEST_ASSERT_EQUAL(128, decoder_get_buffer_count());

    /* Should still be able to get text */
    char buf[64];
    size_t len = decoder_get_text(buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, len);
}

void test_decoder_get_text_with_timestamps(void) {
    decoder_reset();

    /* Decode E and T */
    decoder_handle_event(KEY_EVENT_DIT, 1000);
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 2000);

    decoder_handle_event(KEY_EVENT_DAH, 3000);
    decoder_handle_event(KEY_EVENT_CHAR_GAP, 4000);

    decoded_char_t chars[10];
    size_t count = decoder_get_text_with_timestamps(chars, 10);

    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_EQUAL_CHAR('E', chars[0].character);
    TEST_ASSERT_EQUAL(2000, chars[0].timestamp_us);
    TEST_ASSERT_EQUAL_CHAR('T', chars[1].character);
    TEST_ASSERT_EQUAL(4000, chars[1].timestamp_us);
}
