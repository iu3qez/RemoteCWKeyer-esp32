/**
 * @file test_stream.c
 * @brief Unit tests for keying stream
 */

#include "unity.h"
#include "stream.h"
#include "sample.h"
#include "consumer.h"
#include "stubs/esp_stubs.h"
#include <pthread.h>
#include <stdatomic.h>

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

    /* Push samples. stream_push() compresses identical samples into idle
     * ticks, so use the raw writer: this test is about lag arithmetic. */
    for (size_t i = 0; i < 10; i++) {
        stream_sample_t sample = STREAM_SAMPLE_EMPTY;
        stream_push_raw(&s_stream, sample);
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

/* ============================================================================
 * The slot being overwritten is not readable (#57)
 * ============================================================================ */

/* A sample that carries its own index, so a stale or torn copy shows */
static stream_sample_t sample_for_index(size_t idx) {
    stream_sample_t s = STREAM_SAMPLE_EMPTY;
    s.local_key = (uint8_t)((idx >> 8) & 0xFF);
    s.audio_level = (uint8_t)(idx & 0xFF);
    s.config_gen = (uint16_t)((idx * 7919u) & 0xFFFF);
    return s;
}

static bool sample_is_index(const stream_sample_t *s, size_t idx) {
    stream_sample_t want = sample_for_index(idx);
    return s->local_key == want.local_key && s->audio_level == want.audio_level &&
           s->config_gen == want.config_gen;
}

void test_stream_read_rejects_the_slot_being_overwritten(void) {
    stream_init(&s_stream, s_test_buffer, TEST_BUFFER_SIZE);
    for (size_t i = 0; i < TEST_BUFFER_SIZE; i++) {
        stream_push_raw(&s_stream, sample_for_index(i));
    }

    /* Index 0 lives in the slot the producer writes next: gone, whatever
     * the slot still holds. Index 1 is the oldest readable. */
    stream_sample_t out;
    TEST_ASSERT_EQUAL(TEST_BUFFER_SIZE, stream_lag(&s_stream, 0));
    TEST_ASSERT_FALSE(stream_read(&s_stream, 0, &out));
    TEST_ASSERT_TRUE(stream_is_overrun(&s_stream, 0));
    TEST_ASSERT_TRUE(stream_read(&s_stream, 1, &out));
    TEST_ASSERT_TRUE(sample_is_index(&out, 1));
    TEST_ASSERT_FALSE(stream_is_overrun(&s_stream, 1));
}

void test_stream_resync_lands_on_the_oldest_readable(void) {
    stream_init(&s_stream, s_test_buffer, TEST_BUFFER_SIZE);
    stream_consumer_t consumer;
    consumer_init(&consumer, &s_stream);
    for (size_t i = 0; i < 100; i++) {
        stream_push_raw(&s_stream, sample_for_index(i));
    }

    TEST_ASSERT_TRUE(consumer_is_overrun(&consumer));
    consumer_resync(&consumer);
    TEST_ASSERT_FALSE(consumer_is_overrun(&consumer));

    /* 100 written, 64 slots: 37..99 are readable, 36 is the next slot written */
    stream_sample_t out;
    TEST_ASSERT_TRUE(consumer_next(&consumer, &out));
    TEST_ASSERT_TRUE(sample_is_index(&out, 100 - (TEST_BUFFER_SIZE - 1)));
}

/* Producer and consumer on two threads, a 64-slot ring, the producer never
 * waiting: every sample the consumer accepts must be the one its index
 * says. A publish before the store, or a read of the slot being written,
 * hands out a stale or torn sample. */
#define STRESS_SAMPLES 400000u

static atomic_size_t s_stress_produced;

static void *stress_producer(void *arg) {
    (void)arg;
    for (size_t i = 0; i < STRESS_SAMPLES; i++) {
        stream_push_raw(&s_stream, sample_for_index(i));
        atomic_store_explicit(&s_stress_produced, i + 1, memory_order_release);
    }
    return NULL;
}

void test_stream_two_threads_never_accept_a_stale_or_torn_sample(void) {
    stream_init(&s_stream, s_test_buffer, TEST_BUFFER_SIZE);
    atomic_store(&s_stress_produced, 0);
    stream_consumer_t consumer;
    consumer_init(&consumer, &s_stream);

    pthread_t producer;
    TEST_ASSERT_EQUAL(0, pthread_create(&producer, NULL, stress_producer, NULL));

    size_t accepted = 0;
    size_t wrong = 0;
    size_t resyncs = 0;
    for (;;) {
        stream_sample_t out;
        size_t idx = consumer.read_idx;
        if (consumer_next(&consumer, &out)) {
            accepted++;
            if (!sample_is_index(&out, idx)) {
                wrong++;
            }
            continue;
        }
        if (consumer_is_overrun(&consumer)) {
            consumer_resync(&consumer);
            resyncs++;
            continue;
        }
        if (atomic_load_explicit(&s_stress_produced, memory_order_acquire) == STRESS_SAMPLES &&
            consumer.read_idx == stream_write_position(&s_stream)) {
            break;
        }
    }
    TEST_ASSERT_EQUAL(0, pthread_join(producer, NULL));

    TEST_ASSERT_GREATER_THAN(0, accepted);
    TEST_ASSERT_EQUAL_MESSAGE(0, wrong, "a consumer accepted a sample that is not the one its index says");
    (void)resyncs;
}
