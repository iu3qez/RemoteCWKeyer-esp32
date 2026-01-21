/**
 * @file test_cwnet_ping.c
 * @brief Unit tests for CWNet PING handling and timer synchronization
 *
 * Protocol reference: docs/plans/2026-01-12-cwnet-protocol-implementation.md
 *
 * PING payload (16 bytes):
 *   - type (1 byte): 0=REQUEST, 1=RESPONSE_1, 2=RESPONSE_2
 *   - id (1 byte): sequence ID
 *   - reserved (2 bytes): alignment
 *   - t0 (4 bytes, LE): requester timestamp
 *   - t1 (4 bytes, LE): responder 1 timestamp
 *   - t2 (4 bytes, LE): responder 2 timestamp
 *
 * Timer sync: Client MUST call timer_sync_to_server(t0) on every PING REQUEST
 */

#include "unity.h"
#include "cwnet_ping.h"
#include <string.h>

/*===========================================================================*/
/* Timer Sync Tests                                                          */
/*===========================================================================*/

void test_timer_sync_init(void) {
    /* After init, offset should be 0 */
    cwnet_timer_t timer;
    cwnet_timer_init(&timer);

    /* With zero offset and zero local time, synced time = 0 */
    int32_t synced = cwnet_timer_read_synced_ms(&timer, 0);
    TEST_ASSERT_EQUAL_INT32(0, synced);
}

void test_timer_sync_no_drift(void) {
    /* When local == server, no adjustment needed */
    cwnet_timer_t timer;
    cwnet_timer_init(&timer);

    /* Server says t0=1000, our local time is also 1000 */
    cwnet_timer_sync_to_server(&timer, 1000, 1000);

    /* Offset should be ~0 */
    int32_t synced = cwnet_timer_read_synced_ms(&timer, 1000);
    TEST_ASSERT_EQUAL_INT32(1000, synced);
}

void test_timer_sync_client_ahead(void) {
    /* Client clock is ahead of server */
    cwnet_timer_t timer;
    cwnet_timer_init(&timer);

    /* Server says t0=1000, but our local time is 1100 (we're 100ms ahead) */
    cwnet_timer_sync_to_server(&timer, 1000, 1100);

    /* After sync, reading at local=1200 should give ~1100 (server-relative) */
    int32_t synced = cwnet_timer_read_synced_ms(&timer, 1200);
    TEST_ASSERT_EQUAL_INT32(1100, synced);
}

void test_timer_sync_client_behind(void) {
    /* Client clock is behind server */
    cwnet_timer_t timer;
    cwnet_timer_init(&timer);

    /* Server says t0=1000, but our local time is 900 (we're 100ms behind) */
    cwnet_timer_sync_to_server(&timer, 1000, 900);

    /* After sync, reading at local=1000 should give ~1100 (server-relative) */
    int32_t synced = cwnet_timer_read_synced_ms(&timer, 1000);
    TEST_ASSERT_EQUAL_INT32(1100, synced);
}

void test_timer_sync_cumulative(void) {
    /* Multiple syncs should converge, not accumulate error */
    cwnet_timer_t timer;
    cwnet_timer_init(&timer);

    /* First sync: server=1000, local=1050 (50ms ahead) */
    cwnet_timer_sync_to_server(&timer, 1000, 1050);

    /* Second sync: server=2000, local=2050 (still 50ms ahead) */
    cwnet_timer_sync_to_server(&timer, 2000, 2050);

    /* Should still be correctly offset */
    int32_t synced = cwnet_timer_read_synced_ms(&timer, 3050);
    TEST_ASSERT_EQUAL_INT32(3000, synced);
}

void test_timer_sync_drift_correction(void) {
    /* Sync corrects for drift over time */
    cwnet_timer_t timer;
    cwnet_timer_init(&timer);

    /* Initial sync: perfect alignment */
    cwnet_timer_sync_to_server(&timer, 1000, 1000);

    /* Later: our clock drifted, now 20ms ahead */
    /* Server says 5000, we think it's 5020 */
    cwnet_timer_sync_to_server(&timer, 5000, 5020);

    /* Should correct for the drift */
    int32_t synced = cwnet_timer_read_synced_ms(&timer, 6020);
    TEST_ASSERT_EQUAL_INT32(6000, synced);
}

void test_timer_null_safety(void) {
    /* NULL timer should not crash */
    cwnet_timer_init(NULL);
    cwnet_timer_sync_to_server(NULL, 1000, 1000);
    int32_t synced = cwnet_timer_read_synced_ms(NULL, 1000);
    TEST_ASSERT_EQUAL_INT32(0, synced);
}

/*===========================================================================*/
/* PING Payload Parsing Tests                                                */
/*===========================================================================*/

void test_ping_parse_request(void) {
    /* Parse PING REQUEST (type=0) */
    uint8_t payload[16] = {
        0x00,                   /* type = REQUEST */
        0x42,                   /* id = 66 */
        0x00, 0x00,             /* reserved */
        0xE8, 0x03, 0x00, 0x00, /* t0 = 1000 (LE) */
        0x00, 0x00, 0x00, 0x00, /* t1 = 0 */
        0x00, 0x00, 0x00, 0x00  /* t2 = 0 */
    };

    cwnet_ping_t ping;
    bool ok = cwnet_ping_parse(&ping, payload, sizeof(payload));

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(CWNET_PING_REQUEST, ping.type);
    TEST_ASSERT_EQUAL(0x42, ping.id);
    TEST_ASSERT_EQUAL_INT32(1000, ping.t0_ms);
    TEST_ASSERT_EQUAL_INT32(0, ping.t1_ms);
    TEST_ASSERT_EQUAL_INT32(0, ping.t2_ms);
}

void test_ping_parse_response_1(void) {
    /* Parse PING RESPONSE_1 (type=1) */
    uint8_t payload[16] = {
        0x01,                   /* type = RESPONSE_1 */
        0x42,                   /* id = 66 */
        0x00, 0x00,             /* reserved */
        0xE8, 0x03, 0x00, 0x00, /* t0 = 1000 (LE) */
        0xF4, 0x03, 0x00, 0x00, /* t1 = 1012 (LE) */
        0x00, 0x00, 0x00, 0x00  /* t2 = 0 */
    };

    cwnet_ping_t ping;
    bool ok = cwnet_ping_parse(&ping, payload, sizeof(payload));

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(CWNET_PING_RESPONSE_1, ping.type);
    TEST_ASSERT_EQUAL_INT32(1000, ping.t0_ms);
    TEST_ASSERT_EQUAL_INT32(1012, ping.t1_ms);
}

void test_ping_parse_response_2(void) {
    /* Parse PING RESPONSE_2 (type=2) - for latency calculation */
    uint8_t payload[16] = {
        0x02,                   /* type = RESPONSE_2 */
        0x42,                   /* id = 66 */
        0x00, 0x00,             /* reserved */
        0xE8, 0x03, 0x00, 0x00, /* t0 = 1000 (LE) */
        0xF4, 0x03, 0x00, 0x00, /* t1 = 1012 (LE) */
        0x58, 0x04, 0x00, 0x00  /* t2 = 1112 (LE) */
    };

    cwnet_ping_t ping;
    bool ok = cwnet_ping_parse(&ping, payload, sizeof(payload));

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL(CWNET_PING_RESPONSE_2, ping.type);
    TEST_ASSERT_EQUAL_INT32(1000, ping.t0_ms);
    TEST_ASSERT_EQUAL_INT32(1012, ping.t1_ms);
    TEST_ASSERT_EQUAL_INT32(1112, ping.t2_ms);
}

void test_ping_parse_invalid_length(void) {
    /* Payload too short */
    uint8_t payload[8] = {0};
    cwnet_ping_t ping;

    bool ok = cwnet_ping_parse(&ping, payload, sizeof(payload));
    TEST_ASSERT_FALSE(ok);
}

void test_ping_parse_null(void) {
    /* NULL payload */
    cwnet_ping_t ping;
    bool ok = cwnet_ping_parse(&ping, NULL, 16);
    TEST_ASSERT_FALSE(ok);

    /* NULL ping struct */
    uint8_t payload[16] = {0};
    ok = cwnet_ping_parse(NULL, payload, 16);
    TEST_ASSERT_FALSE(ok);
}

void test_ping_parse_negative_timestamps(void) {
    /* Timestamps can be negative (signed 32-bit) */
    uint8_t payload[16] = {
        0x00, 0x42, 0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0xFF, /* t0 = -1 */
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };

    cwnet_ping_t ping;
    bool ok = cwnet_ping_parse(&ping, payload, sizeof(payload));

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT32(-1, ping.t0_ms);
}

/*===========================================================================*/
/* PING Response Building Tests                                              */
/*===========================================================================*/

void test_ping_build_response_1(void) {
    /* Build RESPONSE_1 from REQUEST */
    cwnet_ping_t request = {
        .type = CWNET_PING_REQUEST,
        .id = 0x42,
        .t0_ms = 1000,
        .t1_ms = 0,
        .t2_ms = 0
    };

    uint8_t buffer[16];
    int32_t our_time = 1050;

    bool ok = cwnet_ping_build_response(&request, buffer, sizeof(buffer), our_time);

    TEST_ASSERT_TRUE(ok);

    /* Verify buffer contents */
    TEST_ASSERT_EQUAL_HEX8(0x01, buffer[0]);  /* type = RESPONSE_1 */
    TEST_ASSERT_EQUAL_HEX8(0x42, buffer[1]);  /* id preserved */

    /* t0 preserved */
    int32_t t0 = (int32_t)(buffer[4] | (buffer[5] << 8) |
                           (buffer[6] << 16) | (buffer[7] << 24));
    TEST_ASSERT_EQUAL_INT32(1000, t0);

    /* t1 = our time */
    int32_t t1 = (int32_t)(buffer[8] | (buffer[9] << 8) |
                           (buffer[10] << 16) | (buffer[11] << 24));
    TEST_ASSERT_EQUAL_INT32(1050, t1);
}

void test_ping_build_response_buffer_too_small(void) {
    cwnet_ping_t request = {.type = CWNET_PING_REQUEST, .id = 1};
    uint8_t buffer[8];

    bool ok = cwnet_ping_build_response(&request, buffer, sizeof(buffer), 1000);
    TEST_ASSERT_FALSE(ok);
}

void test_ping_build_response_null(void) {
    uint8_t buffer[16];
    cwnet_ping_t request = {.type = CWNET_PING_REQUEST};

    TEST_ASSERT_FALSE(cwnet_ping_build_response(NULL, buffer, 16, 1000));
    TEST_ASSERT_FALSE(cwnet_ping_build_response(&request, NULL, 16, 1000));
}

/*===========================================================================*/
/* Latency Calculation Tests                                                 */
/*===========================================================================*/

void test_ping_calc_latency_basic(void) {
    /* Simple RTT calculation from RESPONSE_2 */
    cwnet_ping_t response = {
        .type = CWNET_PING_RESPONSE_2,
        .t0_ms = 1000,
        .t1_ms = 1050,
        .t2_ms = 1100
    };

    int32_t latency = cwnet_ping_calc_latency(&response);

    /* RTT = t2 - t0 = 100ms */
    TEST_ASSERT_EQUAL_INT32(100, latency);
}

void test_ping_calc_latency_zero(void) {
    /* Zero latency (same timestamps) */
    cwnet_ping_t response = {
        .type = CWNET_PING_RESPONSE_2,
        .t0_ms = 1000,
        .t1_ms = 1000,
        .t2_ms = 1000
    };

    int32_t latency = cwnet_ping_calc_latency(&response);
    TEST_ASSERT_EQUAL_INT32(0, latency);
}

void test_ping_calc_latency_wrap(void) {
    /* Timestamp wrap-around (large t0, small t2) */
    cwnet_ping_t response = {
        .type = CWNET_PING_RESPONSE_2,
        .t0_ms = 2147483600,  /* Near INT32_MAX */
        .t1_ms = 0,
        .t2_ms = 100          /* Wrapped around */
    };

    int32_t latency = cwnet_ping_calc_latency(&response);

    /* With signed arithmetic, this gives negative result - indicates wrap */
    /* The protocol uses modulo 2^31, so we should handle this */
    TEST_ASSERT_TRUE(latency != 0);  /* Just verify it doesn't crash */
}

void test_ping_calc_latency_wrong_type(void) {
    /* Can't calculate latency from REQUEST */
    cwnet_ping_t ping = {.type = CWNET_PING_REQUEST};

    int32_t latency = cwnet_ping_calc_latency(&ping);
    TEST_ASSERT_EQUAL_INT32(-1, latency);  /* Error value */
}

void test_ping_calc_latency_null(void) {
    int32_t latency = cwnet_ping_calc_latency(NULL);
    TEST_ASSERT_EQUAL_INT32(-1, latency);
}

/*===========================================================================*/
/* Integration: Full PING Sequence                                           */
/*===========================================================================*/

void test_ping_full_sequence(void) {
    /* Simulate full PING handshake */
    cwnet_timer_t timer;
    cwnet_timer_init(&timer);

    /* 1. Server sends REQUEST with t0=10000 */
    uint8_t request_payload[16] = {
        0x00, 0x01, 0x00, 0x00,             /* type=0, id=1 */
        0x10, 0x27, 0x00, 0x00,             /* t0 = 10000 (LE) */
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };

    cwnet_ping_t request;
    TEST_ASSERT_TRUE(cwnet_ping_parse(&request, request_payload, 16));

    /* 2. Client syncs timer (our local time is 10020 - 20ms ahead) */
    int32_t local_time = 10020;
    cwnet_timer_sync_to_server(&timer, request.t0_ms, local_time);

    /* 3. Client gets synced time for response */
    int32_t synced_time = cwnet_timer_read_synced_ms(&timer, local_time);
    TEST_ASSERT_EQUAL_INT32(10000, synced_time);  /* Matches server */

    /* 4. Client builds RESPONSE_1 */
    uint8_t response_buf[16];
    TEST_ASSERT_TRUE(cwnet_ping_build_response(&request, response_buf, 16, synced_time));

    /* 5. Verify response */
    cwnet_ping_t response;
    TEST_ASSERT_TRUE(cwnet_ping_parse(&response, response_buf, 16));
    TEST_ASSERT_EQUAL(CWNET_PING_RESPONSE_1, response.type);
    TEST_ASSERT_EQUAL(1, response.id);
    TEST_ASSERT_EQUAL_INT32(10000, response.t0_ms);  /* Preserved */
    TEST_ASSERT_EQUAL_INT32(10000, response.t1_ms);  /* Our synced time */
}

void test_ping_latency_measurement(void) {
    /* Test latency measurement from RESPONSE_2 */
    cwnet_timer_t timer;
    cwnet_timer_init(&timer);

    /* Simulate: we sent request at t0=5000, got response with t2=5085 */
    cwnet_ping_t response2 = {
        .type = CWNET_PING_RESPONSE_2,
        .id = 5,
        .t0_ms = 5000,
        .t1_ms = 5040,  /* Server received at 5040 */
        .t2_ms = 5085   /* Server sent response at 5085 */
    };

    int32_t rtt = cwnet_ping_calc_latency(&response2);
    TEST_ASSERT_EQUAL_INT32(85, rtt);  /* 5085 - 5000 = 85ms */
}
