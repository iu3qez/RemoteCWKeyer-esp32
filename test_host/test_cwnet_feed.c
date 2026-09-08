/**
 * @file test_cwnet_feed.c
 * @brief The CWNet feed: from the keying stream to MORSE frames, on stream time
 *
 * Driven only through the stream, the way rt_task drives it: one sample per
 * 1 ms tick, unchanged samples compressed into silence markers.
 */

#include "unity.h"
#include "cwnet_fixtures.h"
#include "cwnet_feed.h"
#include "stream.h"
#include "sample.h"
#include "consumer.h"
#include <string.h>

#define FEED_STREAM_CAPACITY 256
static stream_sample_t s_buf[FEED_STREAM_CAPACITY];
static keying_stream_t s_stream;
static cwnet_client_t s_client;
static cwnet_feed_t s_feed;

/* Everything the client sends, in order */
static uint8_t s_tx[512];
static size_t s_tx_len;
static int s_fail_sends;   /* the next N sends fail, like a full socket */

static int send_accumulate(const uint8_t *data, size_t len, void *user_data) {
    (void)user_data;
    if (s_fail_sends > 0) {
        s_fail_sends--;
        return -1;
    }
    if (s_tx_len + len > sizeof(s_tx)) {
        return -1;
    }
    memcpy(s_tx + s_tx_len, data, len);
    s_tx_len += len;
    return (int)len;
}

/* PING only; keying never reads this clock */
static int32_t time_cb(void *user_data) {
    (void)user_data;
    return 0;
}

static void setup_stream(void) {
    stream_init(&s_stream, s_buf, FEED_STREAM_CAPACITY);
}

static void make_ready(void) {
    cwnet_client_on_connected(&s_client);
    cwnet_client_on_data(&s_client, ref_connect_echo, sizeof(ref_connect_echo));
    TEST_ASSERT_EQUAL(CWNET_STATE_READY, cwnet_client_get_state(&s_client));
    s_tx_len = 0;  /* drop the CONNECT */
}

/* A client on the accumulating sink; READY with TRANSMIT granted if asked */
static void setup_client(bool ready) {
    cwnet_client_config_t cfg = {
        .server_host = "test.server.com",
        .server_port = 7373,
        .username = "TEST",
        .send_cb = send_accumulate,
        .get_time_ms_cb = time_cb,
        .user_data = NULL
    };
    memset(&s_client, 0, sizeof(s_client));
    s_tx_len = 0;
    s_fail_sends = 0;
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_init(&s_client, &cfg));
    if (ready) {
        make_ready();
    }
}

/* rt_task pushes one sample per tick; the stream compresses the unchanged ones */
static void key_for_ticks(uint8_t key, uint32_t ticks) {
    stream_sample_t s = STREAM_SAMPLE_EMPTY;
    s.local_key = key;
    for (uint32_t i = 0; i < ticks; i++) {
        TEST_ASSERT_TRUE(stream_push(&s_stream, s));
    }
}

/* The letter A at 25 WPM (dot 48 ms), then the key-up edge */
static void key_letter_a(void) {
    key_for_ticks(1, 48);
    key_for_ticks(0, 48);
    key_for_ticks(1, 144);
    key_for_ticks(0, 1);
}

static const uint8_t frames_letter_a[] = {
    0x50, 0x01, 0x80,   /* down, wait 0 */
    0x50, 0x01, 0x24,   /* up after 48 */
    0x50, 0x01, 0xA4,   /* down after 48 */
    0x50, 0x01, 0x3C,   /* up after 144 */
};

void test_feed_waits_are_tick_distances_whatever_the_drain(void) {
    setup_stream();
    setup_client(true);
    cwnet_feed_init(&s_feed, &s_stream, 0);

    key_for_ticks(0, 500);  /* idle before the over: no entry until something changes */
    key_letter_a();

    /* One drain for the whole over. Stamped at drain time, every wait here
     * would be 0; on stream time they are the tick distances. */
    cwnet_feed_result_t r = cwnet_feed_process(&s_feed, &s_client, 1000000, 48);
    TEST_ASSERT_EQUAL(4, r.edges);
    TEST_ASSERT_FALSE(r.end_of_over);
    TEST_ASSERT_FALSE(r.aborted);

    TEST_ASSERT_EQUAL(sizeof(frames_letter_a), s_tx_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(frames_letter_a, s_tx, sizeof(frames_letter_a));
    TEST_ASSERT_EQUAL_UINT32(500 + 48 + 48 + 144 + 1, cwnet_feed_stream_ms(&s_feed));
}

void test_feed_first_over_from_stream_matches_reference_capture(void) {
    setup_stream();
    setup_client(true);
    cwnet_client_set_ptt_tail_ms(&s_client, 100);   /* the box's timing.ptt_tail_ms default */
    cwnet_feed_init(&s_feed, &s_stream, 0);

    key_for_ticks(0, 100);
    key_letter_a();
    cwnet_feed_result_t r = cwnet_feed_process(&s_feed, &s_client, 1000000, 48);
    TEST_ASSERT_EQUAL(4, r.edges);
    TEST_ASSERT_TRUE(r.ptt_on);

    /* Nothing else happens on the key: 673 ms later on the caller's clock
     * PTT has been up past its tail and the over is closed, on stream time
     * aged by that clock, in that order. */
    r = cwnet_feed_process(&s_feed, &s_client, 1000000 + 673000, 48);
    TEST_ASSERT_TRUE(r.ptt_off);
    TEST_ASSERT_TRUE(r.end_of_over);

    /* The whole slice of the capture, PTT strings included */
    TEST_ASSERT_EQUAL(sizeof(ref_first_over), s_tx_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ref_first_over, s_tx, sizeof(ref_first_over));
}

void test_feed_idle_stream_ages_on_the_caller_clock(void) {
    setup_stream();
    setup_client(true);
    cwnet_feed_init(&s_feed, &s_stream, 0);

    key_for_ticks(1, 48);
    key_for_ticks(0, 1);
    cwnet_feed_result_t r = cwnet_feed_process(&s_feed, &s_client, 1000000, 48);
    TEST_ASSERT_EQUAL(2, r.edges);
    TEST_ASSERT_EQUAL_UINT32(49, cwnet_feed_stream_ms(&s_feed));

    /* rt_task keeps pushing the unchanged sample every tick, and the stream
     * writes no entry for it: the caller's clock ages stream time */
    key_for_ticks(0, 672);
    r = cwnet_feed_process(&s_feed, &s_client, 1000000 + 672000, 48);
    TEST_ASSERT_FALSE(r.end_of_over);
    key_for_ticks(0, 1);
    r = cwnet_feed_process(&s_feed, &s_client, 1000000 + 673000, 48);
    TEST_ASSERT_TRUE(r.end_of_over);
    r = cwnet_feed_process(&s_feed, &s_client, 1000000 + 5000000, 48);
    TEST_ASSERT_FALSE(r.end_of_over);
    /* Ageing is not stream time: nothing was consumed */
    TEST_ASSERT_EQUAL_UINT32(49, cwnet_feed_stream_ms(&s_feed));

    /* The next edge opens a new over with wait 0 */
    key_for_ticks(1, 10);
    r = cwnet_feed_process(&s_feed, &s_client, 1000000 + 6000000, 48);
    TEST_ASSERT_EQUAL(1, r.edges);

    static const uint8_t expected[] = {
        0x50, 0x01, 0x80,
        0x50, 0x01, 0x24,
        0x50, 0x01, 0x60,
        0x50, 0x01, 0x80,
    };
    TEST_ASSERT_EQUAL(sizeof(expected), s_tx_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, s_tx, sizeof(expected));
}

void test_feed_drains_while_client_not_ready_and_does_not_replay(void) {
    setup_stream();
    setup_client(false);
    cwnet_feed_init(&s_feed, &s_stream, 0);

    /* Keying while disconnected is consumed and dropped, not queued */
    key_letter_a();
    cwnet_feed_result_t r = cwnet_feed_process(&s_feed, &s_client, 1000000, 48);
    TEST_ASSERT_EQUAL(0, r.edges);
    TEST_ASSERT_EQUAL(0, s_tx_len);
    TEST_ASSERT_EQUAL_UINT32(48 + 48 + 144 + 1, cwnet_feed_stream_ms(&s_feed));

    /* Key down when the session comes up: its key-up is not a transition on
     * the wire and sends nothing; the next key-down opens the first over. */
    key_for_ticks(1, 20);
    r = cwnet_feed_process(&s_feed, &s_client, 1100000, 48);
    TEST_ASSERT_EQUAL(0, r.edges);
    make_ready();
    key_for_ticks(0, 100);
    r = cwnet_feed_process(&s_feed, &s_client, 1200000, 48);
    TEST_ASSERT_EQUAL(0, r.edges);
    TEST_ASSERT_EQUAL(0, s_tx_len);

    key_for_ticks(1, 30);
    key_for_ticks(0, 1);
    r = cwnet_feed_process(&s_feed, &s_client, 1300000, 48);
    TEST_ASSERT_EQUAL(2, r.edges);

    static const uint8_t expected[] = {0x50, 0x01, 0x80, 0x50, 0x01, 0x1E};
    TEST_ASSERT_EQUAL(sizeof(expected), s_tx_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, s_tx, sizeof(expected));
}

void test_feed_overrun_closes_the_over_on_the_wire(void) {
    setup_stream();
    setup_client(true);
    cwnet_feed_init(&s_feed, &s_stream, 0);

    key_for_ticks(0, 10);
    key_for_ticks(1, 20);
    cwnet_feed_result_t r = cwnet_feed_process(&s_feed, &s_client, 1000000, 48);
    TEST_ASSERT_EQUAL(1, r.edges);
    TEST_ASSERT_EQUAL(3, s_tx_len);  /* 0x80: key down on the wire */
    s_tx_len = 0;

    /* The background loop stalls while the stream keeps writing: more
     * entries than the buffer holds, key still down. An entry per tick is
     * forced through audio_level, which the producer does not write today;
     * on the box the same overrun takes a buffer's worth of transitions. */
    for (uint32_t i = 0; i < FEED_STREAM_CAPACITY + 50; i++) {
        stream_sample_t s = STREAM_SAMPLE_EMPTY;
        s.local_key = 1;
        s.audio_level = (i & 1u) ? 100 : 0;
        TEST_ASSERT_TRUE(stream_push(&s_stream, s));
    }

    /* Time is lost: the over is closed now (key up, then the end-of-over
     * mark), and since the key is still down a fresh over opens at once. */
    r = cwnet_feed_process(&s_feed, &s_client, 2000000, 48);
    TEST_ASSERT_TRUE(r.aborted);
    TEST_ASSERT_GREATER_THAN(0, best_effort_consumer_dropped(&s_feed.consumer));
    static const uint8_t expected[] = {
        0x50, 0x01, 0x00,   /* key up, now */
        0x50, 0x01, 0x00,   /* end of over */
        0x50, 0x01, 0x80,   /* the key is down: new over, wait 0 */
    };
    TEST_ASSERT_EQUAL(sizeof(expected), s_tx_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, s_tx, sizeof(expected));

    /* The new over is timed from its own first edge */
    s_tx_len = 0;
    key_for_ticks(0, 1);
    r = cwnet_feed_process(&s_feed, &s_client, 2100000, 48);
    TEST_ASSERT_EQUAL(1, r.edges);
    TEST_ASSERT_FALSE(r.aborted);
    TEST_ASSERT_EQUAL(3, s_tx_len);
    TEST_ASSERT_EQUAL_HEX8(0x00, s_tx[2] & 0x80);  /* key up, a small wait */
}

void test_feed_no_dot_time_no_end_of_over(void) {
    setup_stream();
    setup_client(true);
    cwnet_feed_init(&s_feed, &s_stream, 0);

    key_for_ticks(1, 48);
    key_for_ticks(0, 1);
    cwnet_feed_result_t r = cwnet_feed_process(&s_feed, &s_client, 1000000, 0);
    TEST_ASSERT_EQUAL(2, r.edges);
    r = cwnet_feed_process(&s_feed, &s_client, 6000000, 0);
    TEST_ASSERT_FALSE(r.end_of_over);
    TEST_ASSERT_EQUAL(6, s_tx_len);

    /* With a dot time the same idle closes it: 5000 ms of key-up, split as
     * the reference splits a wait above 1165 ms, four full bytes and 340 */
    r = cwnet_feed_process(&s_feed, &s_client, 6000000, 48);
    TEST_ASSERT_TRUE(r.end_of_over);
    static const uint8_t expected[] = {
        0x50, 0x01, 0x80,
        0x50, 0x01, 0x24,
        0x50, 0x05, 0x7F, 0x7F, 0x7F, 0x7F, 0x4B,
    };
    TEST_ASSERT_EQUAL(sizeof(expected), s_tx_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, s_tx, sizeof(expected));
}

void test_feed_drain_granularity_does_not_change_the_bytes(void) {
    /* Tick by tick: a process() after every single push */
    setup_stream();
    setup_client(true);
    cwnet_feed_init(&s_feed, &s_stream, 0);
    static const struct { uint8_t key; uint32_t ticks; } over[] = {
        {0, 100}, {1, 48}, {0, 48}, {1, 144}, {0, 1},
    };
    int64_t tick = 0;
    for (size_t i = 0; i < sizeof(over) / sizeof(over[0]); i++) {
        for (uint32_t t = 0; t < over[i].ticks; t++) {
            key_for_ticks(over[i].key, 1);
            tick++;
            cwnet_feed_result_t r = cwnet_feed_process(&s_feed, &s_client, tick * 1000, 48);
            TEST_ASSERT_FALSE(r.end_of_over);
        }
    }
    uint8_t fine[64];
    size_t fine_len = s_tx_len;
    TEST_ASSERT_LESS_OR_EQUAL(sizeof(fine), fine_len);
    memcpy(fine, s_tx, fine_len);

    /* All at once */
    setup_stream();
    setup_client(true);
    cwnet_feed_init(&s_feed, &s_stream, 0);
    key_for_ticks(0, 100);
    key_letter_a();
    cwnet_feed_process(&s_feed, &s_client, tick * 1000, 48);

    TEST_ASSERT_EQUAL(sizeof(frames_letter_a), fine_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(frames_letter_a, fine, fine_len);
    TEST_ASSERT_EQUAL(fine_len, s_tx_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(fine, s_tx, fine_len);
}

void test_feed_send_failure_closes_the_over_and_retries(void) {
    setup_stream();
    setup_client(true);
    cwnet_feed_init(&s_feed, &s_stream, 0);

    key_for_ticks(1, 20);
    cwnet_feed_result_t r = cwnet_feed_process(&s_feed, &s_client, 1000000, 48);
    TEST_ASSERT_EQUAL(1, r.edges);
    TEST_ASSERT_TRUE(cwnet_client_key_on_wire(&s_client));
    s_tx_len = 0;

    /* The socket is full: the key-up does not go out, nor does the key-up
     * of the first attempt to close the over. The wire still says down. */
    s_fail_sends = 2;
    key_for_ticks(0, 1);
    r = cwnet_feed_process(&s_feed, &s_client, 1020000, 48);
    TEST_ASSERT_EQUAL(0, r.edges);
    TEST_ASSERT_TRUE(r.stuck);
    TEST_ASSERT_FALSE(r.aborted);
    TEST_ASSERT_EQUAL(0, s_tx_len);
    TEST_ASSERT_TRUE(cwnet_client_key_on_wire(&s_client));

    /* Next pass, socket drained: the over is closed before anything else */
    r = cwnet_feed_process(&s_feed, &s_client, 1030000, 48);
    TEST_ASSERT_TRUE(r.aborted);
    TEST_ASSERT_FALSE(r.stuck);
    TEST_ASSERT_FALSE(cwnet_client_key_on_wire(&s_client));
    TEST_ASSERT_FALSE(cwnet_client_over_open(&s_client));

    /* And keying goes on from a clean wire */
    key_for_ticks(1, 30);
    key_for_ticks(0, 1);
    r = cwnet_feed_process(&s_feed, &s_client, 1100000, 48);
    TEST_ASSERT_EQUAL(2, r.edges);

    static const uint8_t expected[] = {
        0x50, 0x01, 0x00,   /* key up, now */
        0x50, 0x01, 0x00,   /* end of over */
        0x50, 0x01, 0x80,
        0x50, 0x01, 0x1E,
    };
    TEST_ASSERT_EQUAL(sizeof(expected), s_tx_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, s_tx, sizeof(expected));
}

void test_feed_disconnect_mid_over_then_reconnect(void) {
    setup_stream();
    setup_client(true);
    cwnet_feed_init(&s_feed, &s_stream, 0);

    key_for_ticks(1, 20);
    cwnet_feed_result_t r = cwnet_feed_process(&s_feed, &s_client, 1000000, 48);
    TEST_ASSERT_EQUAL(1, r.edges);

    /* The server goes away with our key down on its side */
    cwnet_client_on_disconnected(&s_client);
    key_for_ticks(0, 1);
    r = cwnet_feed_process(&s_feed, &s_client, 1020000, 48);
    TEST_ASSERT_EQUAL(0, r.edges);
    TEST_ASSERT_FALSE(r.end_of_over);

    /* New session: it opens with the next key-down, wait 0 */
    make_ready();
    key_for_ticks(0, 50);
    key_for_ticks(1, 10);
    key_for_ticks(0, 1);
    r = cwnet_feed_process(&s_feed, &s_client, 1100000, 48);
    TEST_ASSERT_EQUAL(2, r.edges);

    static const uint8_t expected[] = {0x50, 0x01, 0x80, 0x50, 0x01, 0x0A};
    TEST_ASSERT_EQUAL(sizeof(expected), s_tx_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, s_tx, sizeof(expected));
}

void test_feed_long_key_down_is_sent_in_full(void) {
    setup_stream();
    setup_client(true);
    cwnet_feed_init(&s_feed, &s_stream, 0);

    /* A 60 s key-down (tuning up): one frame of 51 full bytes and the
     * remainder, then the next element timed from the key-up, not from
     * where a shorter frame would have left the stopwatch. */
    key_for_ticks(1, 60000);
    key_for_ticks(0, 100);
    key_for_ticks(1, 1);
    cwnet_feed_result_t r = cwnet_feed_process(&s_feed, &s_client, 1000000, 48);
    TEST_ASSERT_EQUAL(3, r.edges);

    uint8_t expected[3 + 2 + 52 + 3];
    size_t n = 0;
    expected[n++] = 0x50; expected[n++] = 0x01; expected[n++] = 0x80;
    expected[n++] = 0x50; expected[n++] = 52;
    for (int i = 0; i < 51; i++) {
        expected[n++] = 0x7F;                  /* 1165 ms each, key up */
    }
    expected[n++] = 0x5A;                      /* 60000 - 51 * 1165 = 585 ms, 573 on the wire */
    /* The 12 ms the 16 ms bucket could not carry are owed to the next wait:
     * 100 + 12 = 112 ms, as the reference's adjusted stopwatch does */
    expected[n++] = 0x50; expected[n++] = 0x01; expected[n++] = 0xB4;
    TEST_ASSERT_EQUAL(n, s_tx_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, s_tx, n);
}
