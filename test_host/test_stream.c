/**
 * @file test_stream.c
 * @brief Unit tests for keying stream
 */

#include "unity.h"
#include "stream.h"
#include "sample.h"
#include "consumer.h"
#include "stubs/esp_stubs.h"

/* Test buffer */
#define TEST_BUFFER_SIZE 64
static stream_sample_t s_test_buffer[TEST_BUFFER_SIZE];
static keying_stream_t s_stream;

void test_stream_init(void) {
    stream_init(&s_stream, s_test_buffer, TEST_BUFFER_SIZE);

    TEST_ASSERT_EQUAL(TEST_BUFFER_SIZE, s_stream.capacity);
    TEST_ASSERT_EQUAL(TEST_BUFFER_SIZE - 1, s_stream.mask);
    TEST_ASSERT_EQUAL(0, stream_write_position(&s_stream));
}

void test_stream_push_pop(void) {
    stream_init(&s_stream, s_test_buffer, TEST_BUFFER_SIZE);

    /* Create a sample */
    stream_sample_t sample = STREAM_SAMPLE_EMPTY;
    sample.local_key = 1;
    sample.audio_level = 128;

    /* Push sample */
    bool pushed = stream_push(&s_stream, sample);
    TEST_ASSERT_TRUE(pushed);
    TEST_ASSERT_EQUAL(1, stream_write_position(&s_stream));

    /* Read sample */
    stream_sample_t read_sample;
    bool ok = stream_read(&s_stream, 0, &read_sample);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(1, read_sample.local_key);
    TEST_ASSERT_EQUAL(128, read_sample.audio_level);
}

void test_stream_wrap_around(void) {
    stream_init(&s_stream, s_test_buffer, TEST_BUFFER_SIZE);

    /* Fill buffer */
    for (size_t i = 0; i < TEST_BUFFER_SIZE - 1; i++) {
        stream_sample_t sample = STREAM_SAMPLE_EMPTY;
        sample.audio_level = (uint8_t)(i & 0xFF);
        bool pushed = stream_push(&s_stream, sample);
        TEST_ASSERT_TRUE(pushed);
    }

    /* Verify wrap position */
    TEST_ASSERT_EQUAL(TEST_BUFFER_SIZE - 1, stream_write_position(&s_stream));

    /* Push one more (should wrap) */
    stream_sample_t sample = STREAM_SAMPLE_EMPTY;
    sample.audio_level = 0xFF;
    bool pushed = stream_push(&s_stream, sample);
    TEST_ASSERT_TRUE(pushed);

    /* Verify wrapped */
    TEST_ASSERT_EQUAL(0, stream_write_position(&s_stream) % TEST_BUFFER_SIZE);
}

void test_stream_overrun_detection(void) {
    stream_init(&s_stream, s_test_buffer, TEST_BUFFER_SIZE);

    /* Initially no overrun */
    TEST_ASSERT_FALSE(stream_is_overrun(&s_stream, 0));

    /* Push samples */
    for (size_t i = 0; i < 10; i++) {
        stream_sample_t sample = STREAM_SAMPLE_EMPTY;
        stream_push(&s_stream, sample);
    }

    /* Consumer at position 0 */
    size_t lag = stream_lag(&s_stream, 0);
    TEST_ASSERT_EQUAL(10, lag);

    /* Consumer at position 5 */
    lag = stream_lag(&s_stream, 5);
    TEST_ASSERT_EQUAL(5, lag);
}

void test_stream_multiple_consumers(void) {
    stream_init(&s_stream, s_test_buffer, TEST_BUFFER_SIZE);

    /* Push 5 samples */
    for (int i = 0; i < 5; i++) {
        stream_sample_t sample = STREAM_SAMPLE_EMPTY;
        sample.local_key = (uint8_t)i;
        stream_push(&s_stream, sample);
    }

    /* Consumer 1 reads from beginning */
    stream_sample_t sample1;
    bool ok = stream_read(&s_stream, 0, &sample1);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(0, sample1.local_key);

    /* Consumer 2 reads from position 3 */
    stream_sample_t sample2;
    ok = stream_read(&s_stream, 3, &sample2);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(3, sample2.local_key);

    /* Consumers don't interfere with each other */
    stream_sample_t sample3;
    ok = stream_read(&s_stream, 0, &sample3);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(0, sample3.local_key);
}
