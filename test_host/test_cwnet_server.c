/**
 * @file test_cwnet_server.c
 * @brief The station server's core, against the 2026-09-05 capture
 *
 * The server is a pure state machine: no socket, no clock, no log. Here it
 * is fed the bytes a real client sends and its answers are compared with
 * the bytes the reference server put on the wire (cwnet_fixtures.h) or
 * with the layout its source documents. Nothing is compared with what the
 * implementation happens to produce.
 */

#include "unity.h"

#include "cwnet_fixtures.h"
#include "cwnet_frame.h"
#include "cwnet_ping.h"
#include "cwnet_play.h"
#include "cwnet_server.h"

#include <stdlib.h>
#include <string.h>

/*===========================================================================*/
/* Fake wire: one buffer per client index                                    */
/*===========================================================================*/

#define FAKE_CLIENTS (CWNET_SERVER_MAX_CLIENTS + 1)
#define FAKE_TX_SIZE 4096

static cwnet_server_t srv;
static cwnet_server_result_t res;

static uint8_t fake_tx[FAKE_CLIENTS][FAKE_TX_SIZE];
static size_t fake_tx_len[FAKE_CLIENTS];
static bool fake_send_fails;

/** Sends to this one client fail; 0 means nobody. Everything else goes out. */
static int fake_send_fail_idx;

static int fake_send(int client_idx, const uint8_t *data, size_t len, void *user_data) {
    (void)user_data;
    if (fake_send_fails || client_idx == fake_send_fail_idx) {
        return -1;
    }
    TEST_ASSERT_TRUE(client_idx >= CWNET_SERVER_FIRST_CLIENT && client_idx < FAKE_CLIENTS);
    size_t s = (size_t)client_idx;
    TEST_ASSERT_TRUE(fake_tx_len[s] + len <= FAKE_TX_SIZE);
    memcpy(fake_tx[s] + fake_tx_len[s], data, len);
    fake_tx_len[s] += len;
    return (int)len;
}

static void wire_clear(void) {
    memset(fake_tx, 0, sizeof(fake_tx));
    memset(fake_tx_len, 0, sizeof(fake_tx_len));
}

static const uint8_t *wire(int client_idx) {
    return fake_tx[(size_t)client_idx];
}

static size_t wire_len(int client_idx) {
    return fake_tx_len[(size_t)client_idx];
}

/**
 * @brief The nth frame with this command on a client's wire, header included
 *
 * Walks the bytes the server sent with the real parser, so a test compares
 * whole frames with a fixture, not payloads it reassembled itself.
 *
 * @param nth 0 for the first, 1 for the second, SIZE_MAX for the last
 */
static bool wire_frame(int client_idx, uint8_t cmd, size_t nth,
                       uint8_t *out, size_t out_size, size_t *out_len) {
    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);
    const uint8_t *buf = wire(client_idx);
    size_t len = wire_len(client_idx);
    size_t off = 0;
    size_t seen = 0;
    bool found = false;
    while (off < len) {
        cwnet_parse_result_t r = cwnet_frame_parse(&parser, buf + off, len - off);
        if (r.status != CWNET_PARSE_OK) {
            break;
        }
        if (r.command == cmd) {
            if (seen == nth || nth == SIZE_MAX) {
                TEST_ASSERT_TRUE(r.bytes_consumed <= out_size);
                memcpy(out, buf + off, r.bytes_consumed);
                *out_len = r.bytes_consumed;
                found = true;
                if (nth != SIZE_MAX) {
                    return true;
                }
            }
            seen++;
        }
        off += r.bytes_consumed;
        cwnet_frame_parser_reset(&parser);
    }
    return found;
}

/** How many frames with this command the server sent to a client */
static size_t wire_frame_count(int client_idx, uint8_t cmd) {
    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);
    const uint8_t *buf = wire(client_idx);
    size_t len = wire_len(client_idx);
    size_t off = 0;
    size_t seen = 0;
    while (off < len) {
        cwnet_parse_result_t r = cwnet_frame_parse(&parser, buf + off, len - off);
        if (r.status != CWNET_PARSE_OK) {
            break;
        }
        if (r.command == cmd) {
            seen++;
        }
        off += r.bytes_consumed;
        cwnet_frame_parser_reset(&parser);
    }
    return seen;
}

/*===========================================================================*/
/* Reading the result                                                        */
/*===========================================================================*/

static size_t event_count(cwnet_server_event_type_t type) {
    size_t n = 0;
    for (size_t i = 0; i < res.count; i++) {
        if (res.ev[i].type == type) {
            n++;
        }
    }
    return n;
}

static const cwnet_server_event_t *event_nth(cwnet_server_event_type_t type, size_t nth) {
    size_t seen = 0;
    for (size_t i = 0; i < res.count; i++) {
        if (res.ev[i].type == type) {
            if (seen == nth) {
                return &res.ev[i];
            }
            seen++;
        }
    }
    return NULL;
}

static const cwnet_server_event_t *event_first(cwnet_server_event_type_t type) {
    return event_nth(type, 0);
}

/*===========================================================================*/
/* Building what a client sends                                              */
/*===========================================================================*/

/**
 * @brief The bytes a client's CONNECT is, from its two fields
 *
 * The capture holds no client-to-server CONNECT to copy (the plan says the
 * record is rebuilt from its layout): 44 bytes of user name, 44 of
 * callsign, 4 of permissions, zero from the client. What comes back is
 * what gets compared with the capture.
 */
static size_t build_connect(uint8_t *out, size_t out_size,
                            const char *username, const char *callsign,
                            size_t payload_len) {
    uint8_t payload[CWNET_CONNECT_PAYLOAD_LEN];
    memset(payload, 0, sizeof(payload));
    memcpy(payload, username, strlen(username));
    memcpy(payload + CWNET_CONNECT_USERNAME_LEN, callsign, strlen(callsign));
    size_t frame_len = 0;
    TEST_ASSERT_TRUE(cwnet_frame_build((uint8_t)CWNET_CMD_CONNECT, payload, payload_len,
                                       out, out_size, &frame_len));
    return frame_len;
}

/** One frame around a payload, as a client would put it on the wire */
static size_t build_frame(uint8_t *out, size_t out_size, uint8_t cmd,
                          const void *payload, size_t payload_len) {
    size_t frame_len = 0;
    TEST_ASSERT_TRUE(cwnet_frame_build(cmd, (const uint8_t *)payload, payload_len,
                                       out, out_size, &frame_len));
    return frame_len;
}

/**
 * @brief A key-down that stays down: one MORSE frame of two capture bytes
 *
 * 0x80 is the key-down with wait 0 that opens ref_first_over, 0x60 the
 * key-up after 669 ms that closes it. Between them the played key stays
 * down, which is what a safety-net test needs: a lone key-down byte is an
 * underrun by design (R8), and the engine lifts it at once.
 */
static const uint8_t morse_key_down_held[] = { 0x50, 0x02, 0x80, 0x60 };

/*===========================================================================*/
/* Setup helpers                                                             */
/*===========================================================================*/

static void server_setup_cfg(const cwnet_server_cfg_t *cfg) {
    wire_clear();
    fake_send_fails = false;
    fake_send_fail_idx = 0;
    TEST_ASSERT_TRUE(cwnet_server_init(&srv, cfg));
}

static void server_setup(void) {
    cwnet_server_cfg_t cfg;
    cwnet_server_cfg_defaults(&cfg);
    cfg.send_cb = fake_send;
    cfg.user_data = NULL;
    server_setup_cfg(&cfg);
}

/** A connected client that has completed its CONNECT; the wire is cleared */
static int ready_client(const char *username, const char *callsign, int64_t now_ms) {
    int idx = cwnet_server_on_connected(&srv, now_ms, &res);
    TEST_ASSERT_TRUE(idx >= CWNET_SERVER_FIRST_CLIENT);
    uint8_t connect[2 + CWNET_CONNECT_PAYLOAD_LEN];
    size_t len = build_connect(connect, sizeof(connect), username, callsign,
                               CWNET_CONNECT_PAYLOAD_LEN);
    cwnet_server_on_data(&srv, idx, connect, len, now_ms, &res);
    TEST_ASSERT_TRUE(cwnet_server_client_ready(&srv, idx));
    wire_clear();
    return idx;
}

/**
 * @brief One complete PING exchange, so a client ends with a known peak-hold
 *
 * The server is the initiator (R4): poll makes the REQUEST, the test
 * answers it as the client would with cwnet_ping_build_response(), and the
 * answer arrives rtt_ms later.
 */
static void ping_exchange(int idx, int64_t request_at_ms, int32_t rtt_ms) {
    cwnet_server_poll(&srv, request_at_ms, &res);

    uint8_t frame[2 + CWNET_PING_PAYLOAD_SIZE];
    size_t frame_len = 0;
    TEST_ASSERT_TRUE(wire_frame(idx, (uint8_t)CWNET_CMD_PING, SIZE_MAX,
                                frame, sizeof(frame), &frame_len));
    cwnet_ping_t request;
    TEST_ASSERT_TRUE(cwnet_ping_parse(&request, frame + 2, frame_len - 2));
    TEST_ASSERT_EQUAL_INT(CWNET_PING_REQUEST, request.type);

    uint8_t payload[CWNET_PING_PAYLOAD_SIZE];
    TEST_ASSERT_TRUE(cwnet_ping_build_response(&request, payload, sizeof(payload),
                                               request.t0_ms + rtt_ms / 2));
    uint8_t reply[2 + CWNET_PING_PAYLOAD_SIZE];
    size_t reply_len = build_frame(reply, sizeof(reply), (uint8_t)CWNET_CMD_PING,
                                   payload, sizeof(payload));
    cwnet_server_on_data(&srv, idx, reply, reply_len, request_at_ms + rtt_ms, &res);
}

/*===========================================================================*/
/* R2: the log-in, byte for byte against the capture                         */
/*===========================================================================*/

void test_server_connect_echo_matches_the_capture(void) {
    server_setup();

    int idx = cwnet_server_on_connected(&srv, 1000, &res);
    TEST_ASSERT_EQUAL_INT(CWNET_SERVER_FIRST_CLIENT, idx);

    uint8_t connect[2 + CWNET_CONNECT_PAYLOAD_LEN];
    size_t len = build_connect(connect, sizeof(connect), "Moritz", "Moritz",
                               CWNET_CONNECT_PAYLOAD_LEN);
    cwnet_server_on_data(&srv, idx, connect, len, 1000, &res);

    /* The echo, with the permissions filled in: session 10 of the capture */
    TEST_ASSERT_TRUE(wire_len(idx) >= sizeof(ref_connect_echo));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ref_connect_echo, wire(idx), sizeof(ref_connect_echo));

    /* Then the welcome, then the state of the key */
    uint8_t frame[128];
    size_t frame_len = 0;
    TEST_ASSERT_TRUE(wire_frame(idx, (uint8_t)CWNET_SERVER_CMD_PRINT, 0,
                                frame, sizeof(frame), &frame_len));
    TEST_ASSERT_EQUAL_UINT8(0u, frame[frame_len - 1]);   /* the NUL goes on the wire */
    TEST_ASSERT_NOT_NULL(strstr((const char *)frame + 2, "Welcome Moritz"));

    TEST_ASSERT_TRUE(wire_frame(idx, (uint8_t)CWNET_CMD_TX_INFO, 0,
                                frame, sizeof(frame), &frame_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(ref_tx_info_nobody), frame_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ref_tx_info_nobody, frame, sizeof(ref_tx_info_nobody));

    TEST_ASSERT_EQUAL_size_t(1u, event_count(CWNET_SERVER_EV_CLIENT_READY));
    TEST_ASSERT_EQUAL_INT(idx, event_first(CWNET_SERVER_EV_CLIENT_READY)->client_idx);
}

void test_server_connect_of_the_wrong_length_closes_the_client(void) {
    server_setup();
    int idx = cwnet_server_on_connected(&srv, 1000, &res);

    uint8_t connect[2 + CWNET_CONNECT_PAYLOAD_LEN];
    size_t len = build_connect(connect, sizeof(connect), "Moritz", "Moritz",
                               CWNET_CONNECT_PAYLOAD_LEN - 1);
    cwnet_server_on_data(&srv, idx, connect, len, 1000, &res);

    TEST_ASSERT_EQUAL_size_t(0u, wire_len(idx));
    TEST_ASSERT_FALSE(cwnet_server_client_ready(&srv, idx));
    TEST_ASSERT_EQUAL_size_t(0u, cwnet_server_client_count(&srv));
    const cwnet_server_event_t *ev = event_first(CWNET_SERVER_EV_CLIENT_CLOSED);
    TEST_ASSERT_NOT_NULL(ev);
    TEST_ASSERT_EQUAL_INT((int32_t)CWNET_SERVER_CLOSE_BAD_CONNECT, ev->value);
}

void test_server_connect_arriving_one_byte_at_a_time_still_logs_in(void) {
    server_setup();
    int idx = cwnet_server_on_connected(&srv, 1000, &res);

    uint8_t connect[2 + CWNET_CONNECT_PAYLOAD_LEN];
    size_t len = build_connect(connect, sizeof(connect), "Moritz", "Moritz",
                               CWNET_CONNECT_PAYLOAD_LEN);
    for (size_t i = 0; i < len; i++) {
        cwnet_server_on_data(&srv, idx, connect + i, 1u, 1000, &res);
    }

    TEST_ASSERT_TRUE(cwnet_server_client_ready(&srv, idx));
    TEST_ASSERT_TRUE(wire_len(idx) >= sizeof(ref_connect_echo));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ref_connect_echo, wire(idx), sizeof(ref_connect_echo));
}

void test_server_an_empty_callsign_is_announced_as_nocall(void) {
    server_setup();
    int idx = ready_client("Moritz", "", 1000);

    TEST_ASSERT_EQUAL_STRING("NoCall #1", cwnet_server_client_name(&srv, idx));

    /* And that is what goes out when it takes the key */
    uint8_t morse[] = { 0x50, 0x01, 0x80 };
    cwnet_server_on_data(&srv, idx, morse, sizeof(morse), 2000, &res);

    uint8_t frame[64];
    size_t frame_len = 0;
    TEST_ASSERT_TRUE(wire_frame(idx, (uint8_t)CWNET_CMD_TX_INFO, 0,
                                frame, sizeof(frame), &frame_len));
    TEST_ASSERT_EQUAL_UINT8(0x45u, frame[0]);
    TEST_ASSERT_EQUAL_UINT8(1u, frame[2]);   /* the client index, from 1 */
    TEST_ASSERT_EQUAL_STRING("NoCall #1", (const char *)frame + 3);
}

void test_server_connect_fields_without_a_nul_are_read_no_further(void) {
    server_setup();
    int idx = cwnet_server_on_connected(&srv, 1000, &res);

    /* Both 44-byte fields filled to the brim: no terminator anywhere */
    uint8_t payload[CWNET_CONNECT_PAYLOAD_LEN];
    memset(payload, 'X', CWNET_CONNECT_USERNAME_LEN);
    memset(payload + CWNET_CONNECT_USERNAME_LEN, 'Y', CWNET_CONNECT_CALLSIGN_LEN);
    memset(payload + CWNET_CONNECT_PERMISSIONS_OFFSET, 0, 4);
    uint8_t connect[2 + CWNET_CONNECT_PAYLOAD_LEN];
    size_t len = build_frame(connect, sizeof(connect), (uint8_t)CWNET_CMD_CONNECT,
                             payload, sizeof(payload));
    cwnet_server_on_data(&srv, idx, connect, len, 1000, &res);

    TEST_ASSERT_TRUE(cwnet_server_client_ready(&srv, idx));
    const char *name = cwnet_server_client_name(&srv, idx);
    TEST_ASSERT_EQUAL_size_t((size_t)CWNET_CONNECT_CALLSIGN_LEN, strlen(name));
    for (size_t i = 0; i < strlen(name); i++) {
        TEST_ASSERT_EQUAL_CHAR('Y', name[i]);
    }

    /* The announcement carries it terminated, and nothing beyond it */
    uint8_t frame[128];
    size_t frame_len = 0;
    TEST_ASSERT_TRUE(wire_frame(idx, (uint8_t)CWNET_CMD_TX_INFO, 0,
                                frame, sizeof(frame), &frame_len));
    TEST_ASSERT_EQUAL_UINT8(0u, frame[frame_len - 1]);
}

/*===========================================================================*/
/* R1: the client table                                                      */
/*===========================================================================*/

void test_server_a_connection_past_the_limit_is_accepted_and_closed(void) {
    server_setup();

    int idx[CWNET_SERVER_DEFAULT_MAX_CLIENTS];
    for (unsigned i = 0; i < CWNET_SERVER_DEFAULT_MAX_CLIENTS; i++) {
        idx[i] = ready_client("Moritz", "Moritz", 1000);
        TEST_ASSERT_EQUAL_INT((int)i + CWNET_SERVER_FIRST_CLIENT, idx[i]);
    }

    TEST_ASSERT_EQUAL_INT(CWNET_SERVER_NO_SLOT, cwnet_server_on_connected(&srv, 1000, &res));
    const cwnet_server_event_t *ev = event_first(CWNET_SERVER_EV_CLIENT_CLOSED);
    TEST_ASSERT_NOT_NULL(ev);
    TEST_ASSERT_EQUAL_INT((int32_t)CWNET_SERVER_CLOSE_NO_SLOT, ev->value);

    /* The four that were already in are untouched */
    TEST_ASSERT_EQUAL_size_t((size_t)CWNET_SERVER_DEFAULT_MAX_CLIENTS,
                             cwnet_server_client_count(&srv));
    for (unsigned i = 0; i < CWNET_SERVER_DEFAULT_MAX_CLIENTS; i++) {
        TEST_ASSERT_TRUE(cwnet_server_client_ready(&srv, idx[i]));
    }
}

/*===========================================================================*/
/* R16: the timeouts                                                         */
/*===========================================================================*/

void test_server_a_client_that_never_logs_in_is_closed(void) {
    server_setup();
    int idx = cwnet_server_on_connected(&srv, 1000, &res);

    uint8_t connect[2 + CWNET_CONNECT_PAYLOAD_LEN];
    size_t len = build_connect(connect, sizeof(connect), "Moritz", "Moritz",
                               CWNET_CONNECT_PAYLOAD_LEN);
    cwnet_server_on_data(&srv, idx, connect, 1u, 1000, &res);   /* one byte, then silence */
    (void)len;

    cwnet_server_poll(&srv, 1000 + CWNET_SERVER_DEFAULT_HANDSHAKE_MS - 1, &res);
    TEST_ASSERT_EQUAL_size_t(1u, cwnet_server_client_count(&srv));

    cwnet_server_poll(&srv, 1000 + CWNET_SERVER_DEFAULT_HANDSHAKE_MS, &res);
    TEST_ASSERT_EQUAL_size_t(0u, cwnet_server_client_count(&srv));
    const cwnet_server_event_t *ev = event_first(CWNET_SERVER_EV_CLIENT_CLOSED);
    TEST_ASSERT_NOT_NULL(ev);
    TEST_ASSERT_EQUAL_INT((int32_t)CWNET_SERVER_CLOSE_HANDSHAKE, ev->value);
}

void test_server_three_unanswered_pings_close_the_client(void) {
    server_setup();
    int idx = ready_client("Moritz", "Moritz", 1000);

    for (unsigned n = 1; n <= CWNET_SERVER_PING_MISSES; n++) {
        cwnet_server_poll(&srv, 1000 + (int64_t)n * CWNET_SERVER_DEFAULT_PING_INTERVAL_MS, &res);
        TEST_ASSERT_EQUAL_size_t(1u, cwnet_server_client_count(&srv));
    }
    TEST_ASSERT_EQUAL_size_t((size_t)CWNET_SERVER_PING_MISSES,
                             wire_frame_count(idx, (uint8_t)CWNET_CMD_PING));

    cwnet_server_poll(&srv,
                      1000 + (int64_t)(CWNET_SERVER_PING_MISSES + 1u) *
                                 CWNET_SERVER_DEFAULT_PING_INTERVAL_MS,
                      &res);
    TEST_ASSERT_EQUAL_size_t(0u, cwnet_server_client_count(&srv));
    const cwnet_server_event_t *ev = event_first(CWNET_SERVER_EV_CLIENT_CLOSED);
    TEST_ASSERT_NOT_NULL(ev);
    TEST_ASSERT_EQUAL_INT((int32_t)CWNET_SERVER_CLOSE_PING_TIMEOUT, ev->value);
}

/*===========================================================================*/
/* R4: the PING, as the initiator                                            */
/*===========================================================================*/

void test_server_ping_request_and_response2_carry_the_reference_layout(void) {
    server_setup();
    int idx = ready_client("Moritz", "Moritz", 1000);

    /* At t + 2 s the REQUEST goes out: [0, idx, 0, 0, t0, 0, 0] */
    cwnet_server_poll(&srv, 3000, &res);
    uint8_t frame[2 + CWNET_PING_PAYLOAD_SIZE];
    size_t frame_len = 0;
    TEST_ASSERT_TRUE(wire_frame(idx, (uint8_t)CWNET_CMD_PING, 0,
                                frame, sizeof(frame), &frame_len));
    TEST_ASSERT_EQUAL_size_t(2u + CWNET_PING_PAYLOAD_SIZE, frame_len);
    TEST_ASSERT_EQUAL_UINT8(0x40u | CWNET_CMD_PING, frame[0]);
    TEST_ASSERT_EQUAL_UINT8(CWNET_PING_PAYLOAD_SIZE, frame[1]);
    TEST_ASSERT_EQUAL_UINT8(0u, frame[2]);              /* type REQUEST */
    TEST_ASSERT_EQUAL_UINT8((uint8_t)idx, frame[3]);    /* id = the client index */
    TEST_ASSERT_EQUAL_UINT8(0u, frame[4]);
    TEST_ASSERT_EQUAL_UINT8(0u, frame[5]);
    cwnet_ping_t request;
    TEST_ASSERT_TRUE(cwnet_ping_parse(&request, frame + 2, frame_len - 2));
    TEST_ASSERT_EQUAL_INT32(3000, request.t0_ms);
    TEST_ASSERT_EQUAL_INT32(0, request.t1_ms);
    TEST_ASSERT_EQUAL_INT32(0, request.t2_ms);

    /* The client answers; the RESPONSE_2 closes the loop and t2 - t0 is the latency */
    uint8_t payload[CWNET_PING_PAYLOAD_SIZE];
    TEST_ASSERT_TRUE(cwnet_ping_build_response(&request, payload, sizeof(payload), 3040));
    uint8_t reply[2 + CWNET_PING_PAYLOAD_SIZE];
    size_t reply_len = build_frame(reply, sizeof(reply), (uint8_t)CWNET_CMD_PING,
                                   payload, sizeof(payload));
    cwnet_server_on_data(&srv, idx, reply, reply_len, 3080, &res);

    TEST_ASSERT_TRUE(wire_frame(idx, (uint8_t)CWNET_CMD_PING, 1,
                                frame, sizeof(frame), &frame_len));
    cwnet_ping_t response2;
    TEST_ASSERT_TRUE(cwnet_ping_parse(&response2, frame + 2, frame_len - 2));
    TEST_ASSERT_EQUAL_INT(CWNET_PING_RESPONSE_2, response2.type);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)idx, response2.id);
    TEST_ASSERT_EQUAL_INT32(3000, response2.t0_ms);
    TEST_ASSERT_EQUAL_INT32(3040, response2.t1_ms);
    TEST_ASSERT_EQUAL_INT32(3080, response2.t2_ms);

    TEST_ASSERT_EQUAL_INT32(80, cwnet_server_client_latency_ms(&srv, idx));
    TEST_ASSERT_EQUAL_INT32(80, cwnet_server_client_peak_ms(&srv, idx));
    const cwnet_server_event_t *ev = event_first(CWNET_SERVER_EV_LATENCY);
    TEST_ASSERT_NOT_NULL(ev);
    TEST_ASSERT_EQUAL_INT32(80, ev->value);
    TEST_ASSERT_EQUAL_INT32(80, ev->peak_ms);
}

void test_server_a_response_that_matches_no_pending_request_is_ignored(void) {
    server_setup();
    int idx = ready_client("Moritz", "Moritz", 1000);
    ping_exchange(idx, 3000, 80);
    TEST_ASSERT_EQUAL_INT32(80, cwnet_server_client_peak_ms(&srv, idx));

    cwnet_server_poll(&srv, 5000, &res);   /* a second request is pending now */
    uint8_t frame[2 + CWNET_PING_PAYLOAD_SIZE];
    size_t frame_len = 0;
    TEST_ASSERT_TRUE(wire_frame(idx, (uint8_t)CWNET_CMD_PING, SIZE_MAX,
                                frame, sizeof(frame), &frame_len));
    cwnet_ping_t request;
    TEST_ASSERT_TRUE(cwnet_ping_parse(&request, frame + 2, frame_len - 2));

    /* Another client's id: the peak-hold belongs to the connection */
    cwnet_ping_t forged = request;
    forged.id = (uint8_t)(request.id + 1u);
    uint8_t payload[CWNET_PING_PAYLOAD_SIZE];
    uint8_t reply[2 + CWNET_PING_PAYLOAD_SIZE];
    TEST_ASSERT_TRUE(cwnet_ping_build_response(&forged, payload, sizeof(payload), 5300));
    size_t reply_len = build_frame(reply, sizeof(reply), (uint8_t)CWNET_CMD_PING,
                                   payload, sizeof(payload));
    cwnet_server_on_data(&srv, idx, reply, reply_len, 5600, &res);
    TEST_ASSERT_EQUAL_INT32(80, cwnet_server_client_peak_ms(&srv, idx));
    TEST_ASSERT_EQUAL_size_t(0u, event_count(CWNET_SERVER_EV_LATENCY));

    /* A different t0: same answer */
    forged = request;
    forged.t0_ms = request.t0_ms + 1;
    TEST_ASSERT_TRUE(cwnet_ping_build_response(&forged, payload, sizeof(payload), 5300));
    reply_len = build_frame(reply, sizeof(reply), (uint8_t)CWNET_CMD_PING,
                            payload, sizeof(payload));
    cwnet_server_on_data(&srv, idx, reply, reply_len, 5600, &res);
    TEST_ASSERT_EQUAL_INT32(80, cwnet_server_client_peak_ms(&srv, idx));
    TEST_ASSERT_EQUAL_size_t(0u, event_count(CWNET_SERVER_EV_LATENCY));

    /* A PING payload shorter than the reference's 16 bytes is not a framing
     * error: it is dropped, and the client stays */
    uint8_t stub[8] = { 1, 0, 0, 0, 0, 0, 0, 0 };
    reply_len = build_frame(reply, sizeof(reply), (uint8_t)CWNET_CMD_PING, stub, sizeof(stub));
    cwnet_server_on_data(&srv, idx, reply, reply_len, 5700, &res);
    TEST_ASSERT_TRUE(cwnet_server_client_ready(&srv, idx));
}

void test_server_answers_a_ping_request_from_the_client(void) {
    server_setup();
    int idx = ready_client("Moritz", "Moritz", 1000);

    uint8_t payload[CWNET_PING_PAYLOAD_SIZE];
    TEST_ASSERT_TRUE(cwnet_ping_build_request(9u, 12345, payload, sizeof(payload)));
    uint8_t frame[2 + CWNET_PING_PAYLOAD_SIZE];
    size_t frame_len = build_frame(frame, sizeof(frame), (uint8_t)CWNET_CMD_PING,
                                   payload, sizeof(payload));
    cwnet_server_on_data(&srv, idx, frame, frame_len, 2000, &res);

    uint8_t sent[2 + CWNET_PING_PAYLOAD_SIZE];
    size_t sent_len = 0;
    TEST_ASSERT_TRUE(wire_frame(idx, (uint8_t)CWNET_CMD_PING, 0, sent, sizeof(sent), &sent_len));
    cwnet_ping_t response1;
    TEST_ASSERT_TRUE(cwnet_ping_parse(&response1, sent + 2, sent_len - 2));
    TEST_ASSERT_EQUAL_INT(CWNET_PING_RESPONSE_1, response1.type);
    TEST_ASSERT_EQUAL_UINT8(9u, response1.id);
    TEST_ASSERT_EQUAL_INT32(12345, response1.t0_ms);
    TEST_ASSERT_EQUAL_INT32(2000, response1.t1_ms);
}

/*===========================================================================*/
/* R5: the rig-control strings                                               */
/*===========================================================================*/

/** Feed one 0x06 string and read back the "RPRT n" the server answered */
static int32_t rig_string_result(int idx, const char *text, size_t len, int64_t now_ms) {
    uint8_t frame[2 + 96];
    size_t frame_len = build_frame(frame, sizeof(frame), (uint8_t)CWNET_CMD_RIG_STRING,
                                   text, len);
    size_t before = wire_frame_count(idx, (uint8_t)CWNET_CMD_RIG_STRING);
    cwnet_server_on_data(&srv, idx, frame, frame_len, now_ms, &res);
    TEST_ASSERT_EQUAL_size_t(before + 1u, wire_frame_count(idx, (uint8_t)CWNET_CMD_RIG_STRING));

    uint8_t answer[2 + 32];
    size_t answer_len = 0;
    TEST_ASSERT_TRUE(wire_frame(idx, (uint8_t)CWNET_CMD_RIG_STRING, SIZE_MAX,
                                answer, sizeof(answer), &answer_len));
    TEST_ASSERT_EQUAL_UINT8(0u, answer[answer_len - 1]);
    const char *body = (const char *)answer + 2;
    TEST_ASSERT_EQUAL_size_t(0u, (size_t)strncmp(body, "RPRT ", 5));
    return (int32_t)atoi(body + 5);
}

void test_server_set_ptt_is_acknowledged_and_never_applied(void) {
    server_setup();
    int idx = ready_client("Moritz", "Moritz", 1000);

    TEST_ASSERT_EQUAL_INT32(CWNET_SERVER_RPRT_OK,
                            rig_string_result(idx, "set_ptt 1\n", 11u, 2000));
    TEST_ASSERT_EQUAL_INT32(CWNET_SERVER_RPRT_OK,
                            rig_string_result(idx, "set_ptt 0\n", 11u, 2000));

    /* R10: the string moves nothing. The PTT follows the keying played. */
    TEST_ASSERT_FALSE(cwnet_server_ptt_on(&srv));
    TEST_ASSERT_FALSE(cwnet_server_key_down(&srv));
    TEST_ASSERT_EQUAL_size_t(0u, event_count(CWNET_SERVER_EV_PTT_ON));
}

void test_server_any_other_rig_string_gets_a_negative_code(void) {
    server_setup();
    int idx = ready_client("Moritz", "Moritz", 1000);

    /* Not a command we have: HAMLIB_RESULT_NOT_AVAILABLE, the code CwNet.c
     * itself sends when the software, not the rig, declines (CwNet.c:4114) */
    TEST_ASSERT_EQUAL_INT32(CWNET_SERVER_RPRT_NO_FUNC,
                            rig_string_result(idx, "get_freq\n", 10u, 2000));
    TEST_ASSERT_EQUAL_INT32(CWNET_SERVER_RPRT_NO_FUNC,
                            rig_string_result(idx, "set_pttx 1\n", 12u, 2000));

    /* An argument that is not 0 or 1: HAMLIB_RESULT_ARG_OUT_OF_DOM, the code
     * CwNet_Rigctld_OnSetPTT() returns for one it cannot parse (CwNet.c:4102) */
    TEST_ASSERT_EQUAL_INT32(CWNET_SERVER_RPRT_BAD_ARG,
                            rig_string_result(idx, "set_ptt 7\n", 11u, 2000));
    TEST_ASSERT_EQUAL_INT32(CWNET_SERVER_RPRT_BAD_ARG,
                            rig_string_result(idx, "set_ptt\n", 9u, 2000));

    TEST_ASSERT_TRUE(cwnet_server_client_ready(&srv, idx));
}

void test_server_a_rig_string_without_a_nul_closes_the_client(void) {
    server_setup();
    int idx = ready_client("Moritz", "Moritz", 1000);

    uint8_t text[64];
    memset(text, 'A', sizeof(text));       /* 64 bytes, no terminator anywhere */
    uint8_t frame[2 + sizeof(text)];
    size_t frame_len = build_frame(frame, sizeof(frame), (uint8_t)CWNET_CMD_RIG_STRING,
                                   text, sizeof(text));
    cwnet_server_on_data(&srv, idx, frame, frame_len, 2000, &res);

    TEST_ASSERT_EQUAL_size_t(0u, cwnet_server_client_count(&srv));
    const cwnet_server_event_t *ev = event_first(CWNET_SERVER_EV_CLIENT_CLOSED);
    TEST_ASSERT_NOT_NULL(ev);
    TEST_ASSERT_EQUAL_INT((int32_t)CWNET_SERVER_CLOSE_BAD_STRING, ev->value);
}

/*===========================================================================*/
/* R15: what is ignored and what is fatal                                    */
/*===========================================================================*/

void test_server_ignores_ci_v_and_spectrum_and_closes_on_a_parse_error(void) {
    server_setup();
    int idx = ready_client("Moritz", "Moritz", 1000);

    uint8_t junk[4] = { 0xFE, 0xFE, 0x00, 0xFD };
    uint8_t frame[2 + sizeof(junk)];
    size_t frame_len = build_frame(frame, sizeof(frame), (uint8_t)CWNET_CMD_CI_V,
                                   junk, sizeof(junk));
    cwnet_server_on_data(&srv, idx, frame, frame_len, 2000, &res);
    frame_len = build_frame(frame, sizeof(frame), (uint8_t)CWNET_CMD_SPECTRUM,
                            junk, sizeof(junk));
    cwnet_server_on_data(&srv, idx, frame, frame_len, 2000, &res);

    TEST_ASSERT_TRUE(cwnet_server_client_ready(&srv, idx));
    TEST_ASSERT_EQUAL_size_t(0u, wire_len(idx));

    /* Category 11 is reserved: a byte in it is the one framing error there is */
    uint8_t reserved[1] = { 0xC5 };
    cwnet_server_on_data(&srv, idx, reserved, sizeof(reserved), 2000, &res);
    TEST_ASSERT_EQUAL_size_t(0u, cwnet_server_client_count(&srv));
    const cwnet_server_event_t *ev = event_first(CWNET_SERVER_EV_CLIENT_CLOSED);
    TEST_ASSERT_NOT_NULL(ev);
    TEST_ASSERT_EQUAL_INT((int32_t)CWNET_SERVER_CLOSE_PARSE_ERROR, ev->value);
}

/*===========================================================================*/
/* R6, R7, R9: one whole over, against the capture                           */
/*===========================================================================*/

void test_server_plays_the_first_over_of_the_capture_and_announces_both_ends(void) {
    server_setup();
    int idx = ready_client("Moritz", "Moritz", 1000);

    /* No PING has been answered: B falls to the floor, 50 ms (R7) */
    cwnet_server_on_data(&srv, idx, ref_first_over, sizeof(ref_first_over), 2000, &res);

    /* AE1/R3: the announcement is the capture's, byte for byte */
    uint8_t frame[64];
    size_t frame_len = 0;
    TEST_ASSERT_TRUE(wire_frame(idx, (uint8_t)CWNET_CMD_TX_INFO, 0,
                                frame, sizeof(frame), &frame_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(ref_tx_info_moritz), frame_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ref_tx_info_moritz, frame, sizeof(ref_tx_info_moritz));
    TEST_ASSERT_EQUAL_INT(idx, cwnet_server_key_holder(&srv));
    TEST_ASSERT_EQUAL_UINT32(CWNET_SERVER_DEFAULT_BUFFER_FLOOR_MS,
                             cwnet_server_buffer_ms(&srv));

    /* The two "set_ptt" strings of the capture were both answered */
    TEST_ASSERT_EQUAL_size_t(2u, wire_frame_count(idx, (uint8_t)CWNET_CMD_RIG_STRING));

    /* One late poll: the edges land where they were scheduled, not where the
     * poll happened. 'A' at 25 WPM: down at +B, up +48, down +48, up +144. */
    cwnet_server_poll(&srv, 4000, &res);

    TEST_ASSERT_EQUAL_size_t(2u, event_count(CWNET_SERVER_EV_KEY_DOWN));
    TEST_ASSERT_EQUAL_size_t(2u, event_count(CWNET_SERVER_EV_KEY_UP));
    /* Written against B, not against a number: the reference fixes the
     * intervals (+48, +48, +144), the configured floor fixes where they start. */
    const int64_t start = 2000 + (int64_t)CWNET_SERVER_DEFAULT_BUFFER_FLOOR_MS;
    TEST_ASSERT_EQUAL_INT64(start, event_nth(CWNET_SERVER_EV_KEY_DOWN, 0)->at_ms);
    TEST_ASSERT_EQUAL_INT64(start + 48, event_nth(CWNET_SERVER_EV_KEY_UP, 0)->at_ms);
    TEST_ASSERT_EQUAL_INT64(start + 96, event_nth(CWNET_SERVER_EV_KEY_DOWN, 1)->at_ms);
    TEST_ASSERT_EQUAL_INT64(start + 240, event_nth(CWNET_SERVER_EV_KEY_UP, 1)->at_ms);

    /* The PTT rises with the first key-down and drops a tail after the last
     * key-up (R9), and the key comes back when it does (R6) */
    TEST_ASSERT_NOT_NULL(event_first(CWNET_SERVER_EV_PTT_ON));
    TEST_ASSERT_NOT_NULL(event_first(CWNET_SERVER_EV_PTT_OFF));
    TEST_ASSERT_NOT_NULL(event_first(CWNET_SERVER_EV_KEY_HOLDER));
    TEST_ASSERT_EQUAL_INT64(start, event_first(CWNET_SERVER_EV_PTT_ON)->at_ms);
    TEST_ASSERT_EQUAL_INT64(start + 240 + (int64_t)CWNET_PLAY_DEFAULT_PTT_TAIL_MS,
                            event_first(CWNET_SERVER_EV_PTT_OFF)->at_ms);
    TEST_ASSERT_EQUAL_INT(CWNET_SERVER_NOBODY, cwnet_server_key_holder(&srv));
    TEST_ASSERT_EQUAL_INT(CWNET_SERVER_NOBODY,
                          event_first(CWNET_SERVER_EV_KEY_HOLDER)->client_idx);

    TEST_ASSERT_TRUE(wire_frame(idx, (uint8_t)CWNET_CMD_TX_INFO, SIZE_MAX,
                                frame, sizeof(frame), &frame_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(ref_tx_info_nobody), frame_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ref_tx_info_nobody, frame, sizeof(ref_tx_info_nobody));
}

void test_server_morse_from_a_client_without_the_key_is_dropped_in_silence(void) {
    server_setup();
    int first = ready_client("Moritz", "Moritz", 1000);
    int second = ready_client("Karl", "Karl", 1000);

    uint8_t morse[] = { 0x50, 0x01, 0x80 };
    cwnet_server_on_data(&srv, first, morse, sizeof(morse), 2000, &res);
    TEST_ASSERT_EQUAL_INT(first, cwnet_server_key_holder(&srv));
    wire_clear();

    cwnet_server_on_data(&srv, second, morse, sizeof(morse), 2010, &res);

    TEST_ASSERT_EQUAL_INT(first, cwnet_server_key_holder(&srv));
    TEST_ASSERT_EQUAL_size_t(0u, wire_frame_count(first, (uint8_t)CWNET_CMD_TX_INFO));
    TEST_ASSERT_EQUAL_size_t(0u, wire_frame_count(second, (uint8_t)CWNET_CMD_TX_INFO));
    TEST_ASSERT_EQUAL_size_t(0u, event_count(CWNET_SERVER_EV_KEY_HOLDER));
    TEST_ASSERT_EQUAL_UINT32(1u, cwnet_server_morse_ignored(&srv));
}

void test_server_announces_the_holder_to_every_client(void) {
    server_setup();
    int first = ready_client("Moritz", "Moritz", 1000);
    int second = ready_client("Karl", "Karl", 1000);

    uint8_t morse[] = { 0x50, 0x01, 0x80 };
    cwnet_server_on_data(&srv, first, morse, sizeof(morse), 2000, &res);

    uint8_t frame[64];
    size_t frame_len = 0;
    for (int idx = first; idx <= second; idx++) {
        TEST_ASSERT_TRUE(wire_frame(idx, (uint8_t)CWNET_CMD_TX_INFO, 0,
                                    frame, sizeof(frame), &frame_len));
        TEST_ASSERT_EQUAL_size_t(sizeof(ref_tx_info_moritz), frame_len);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(ref_tx_info_moritz, frame, sizeof(ref_tx_info_moritz));
    }
}

/*===========================================================================*/
/* R7: the eligibility ceiling                                               */
/*===========================================================================*/

void test_server_a_link_over_the_ceiling_does_not_get_the_key(void) {
    server_setup();
    int idx = ready_client("Moritz", "Moritz", 1000);

    ping_exchange(idx, 3000, 1200);   /* over the 1000 ms ceiling */
    TEST_ASSERT_EQUAL_INT32(1200, cwnet_server_client_peak_ms(&srv, idx));
    wire_clear();

    uint8_t morse[] = { 0x50, 0x01, 0x80 };
    cwnet_server_on_data(&srv, idx, morse, sizeof(morse), 5000, &res);
    cwnet_server_poll(&srv, 7000, &res);

    TEST_ASSERT_EQUAL_INT(CWNET_SERVER_NOBODY, cwnet_server_key_holder(&srv));
    TEST_ASSERT_EQUAL_size_t(0u, wire_frame_count(idx, (uint8_t)CWNET_CMD_TX_INFO));
    TEST_ASSERT_FALSE(cwnet_server_key_down(&srv));
    TEST_ASSERT_FALSE(cwnet_server_ptt_on(&srv));

    /* It is told so, with the measured value (R7, R12) */
    uint8_t frame[128];
    size_t frame_len = 0;
    TEST_ASSERT_TRUE(wire_frame(idx, (uint8_t)CWNET_SERVER_CMD_PRINT, 0,
                                frame, sizeof(frame), &frame_len));
    TEST_ASSERT_NOT_NULL(strstr((const char *)frame + 2, "not fit"));
    TEST_ASSERT_NOT_NULL(strstr((const char *)frame + 2, "1200"));
}

void test_server_a_link_under_the_ceiling_sets_the_buffer_to_its_peak(void) {
    server_setup();
    int idx = ready_client("Moritz", "Moritz", 1000);

    ping_exchange(idx, 3000, 900);
    TEST_ASSERT_EQUAL_INT32(900, cwnet_server_client_peak_ms(&srv, idx));
    wire_clear();

    uint8_t morse[] = { 0x50, 0x01, 0x80 };
    cwnet_server_on_data(&srv, idx, morse, sizeof(morse), 5000, &res);

    TEST_ASSERT_EQUAL_INT(idx, cwnet_server_key_holder(&srv));
    TEST_ASSERT_EQUAL_UINT32(900u, cwnet_server_buffer_ms(&srv));
    const cwnet_server_event_t *ev = event_first(CWNET_SERVER_EV_OVER_BUFFER);
    TEST_ASSERT_NOT_NULL(ev);
    TEST_ASSERT_EQUAL_INT32(900, ev->value);

    /* And the first edge lands B after the byte arrived */
    cwnet_server_poll(&srv, 6000, &res);
    TEST_ASSERT_NOT_NULL(event_first(CWNET_SERVER_EV_KEY_DOWN));
    TEST_ASSERT_EQUAL_INT64(5900, event_first(CWNET_SERVER_EV_KEY_DOWN)->at_ms);
}

/*===========================================================================*/
/* R6: the safety nets                                                       */
/*===========================================================================*/

/*
 * A holder that vanishes inside the buffer B, before a single edge has been
 * played, leaves no PTT raised — so the playback engine finishes the over
 * inside the very call that forces the release, and the key is free before
 * the fault is emitted. The fault must still name the client that had the
 * key: that line is how the operator learns who dropped, and "nobody
 * dropped" tells him nothing.
 */
void test_server_a_holder_lost_inside_the_buffer_is_still_named_in_the_fault(void) {
    server_setup();
    int holder = ready_client("Moritz", "Moritz", 1000);

    /* The key is taken at 2000; with B at the floor nothing plays until 2050 */
    cwnet_server_on_data(&srv, holder, morse_key_down_held, sizeof(morse_key_down_held),
                         2000, &res);
    TEST_ASSERT_EQUAL_INT(holder, cwnet_server_key_holder(&srv));
    TEST_ASSERT_FALSE(cwnet_server_ptt_on(&srv));

    cwnet_server_on_disconnected(&srv, holder, 2020, &res);

    const cwnet_server_event_t *fault = event_first(CWNET_SERVER_EV_FAULT);
    TEST_ASSERT_NOT_NULL(fault);
    TEST_ASSERT_EQUAL_INT((int32_t)CWNET_SERVER_FAULT_HOLDER_GONE, fault->value);
    TEST_ASSERT_EQUAL_INT(holder, fault->client_idx);
    TEST_ASSERT_EQUAL_INT(CWNET_SERVER_NOBODY, cwnet_server_key_holder(&srv));
}

void test_server_the_holder_disconnecting_frees_the_key_for_the_others(void) {
    server_setup();
    int first = ready_client("Moritz", "Moritz", 1000);
    int second = ready_client("Karl", "Karl", 1000);

    cwnet_server_on_data(&srv, first, morse_key_down_held, sizeof(morse_key_down_held),
                         2000, &res);
    cwnet_server_poll(&srv, 2100, &res);
    TEST_ASSERT_TRUE(cwnet_server_key_down(&srv));
    TEST_ASSERT_TRUE(cwnet_server_ptt_on(&srv));
    wire_clear();

    cwnet_server_on_disconnected(&srv, first, 2200, &res);

    TEST_ASSERT_FALSE(cwnet_server_key_down(&srv));
    TEST_ASSERT_EQUAL_INT(CWNET_SERVER_NOBODY, cwnet_server_key_holder(&srv));
    const cwnet_server_event_t *fault = event_first(CWNET_SERVER_EV_FAULT);
    TEST_ASSERT_NOT_NULL(fault);
    TEST_ASSERT_EQUAL_INT((int32_t)CWNET_SERVER_FAULT_HOLDER_GONE, fault->value);
    /* The fault names the client that lost the key, not the emptiness left
     * behind: the daemon prints this line and the operator has to know who. */
    TEST_ASSERT_EQUAL_INT(first, fault->client_idx);
    TEST_ASSERT_NOT_NULL(event_first(CWNET_SERVER_EV_KEY_UP));
    TEST_ASSERT_EQUAL_INT64(2200, event_first(CWNET_SERVER_EV_KEY_UP)->at_ms);

    /* The others hear that the key is free; the one that left hears nothing */
    uint8_t frame[64];
    size_t frame_len = 0;
    TEST_ASSERT_TRUE(wire_frame(second, (uint8_t)CWNET_CMD_TX_INFO, SIZE_MAX,
                                frame, sizeof(frame), &frame_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(ref_tx_info_nobody), frame_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ref_tx_info_nobody, frame, sizeof(ref_tx_info_nobody));
    TEST_ASSERT_EQUAL_size_t(0u, wire_len(first));

    /* The PTT drops at the tail, no later */
    cwnet_server_poll(&srv, 2200 + CWNET_PLAY_DEFAULT_PTT_TAIL_MS, &res);
    TEST_ASSERT_FALSE(cwnet_server_ptt_on(&srv));
}

void test_server_silence_from_the_holder_releases_the_key_with_a_fault(void) {
    server_setup();
    int idx = ready_client("Moritz", "Moritz", 1000);

    cwnet_server_on_data(&srv, idx, morse_key_down_held, sizeof(morse_key_down_held),
                         2000, &res);
    cwnet_server_poll(&srv, 2100, &res);
    TEST_ASSERT_TRUE(cwnet_server_key_down(&srv));

    cwnet_server_poll(&srv, 2000 + CWNET_SERVER_DEFAULT_IDLE_MS - 1, &res);
    TEST_ASSERT_EQUAL_INT(idx, cwnet_server_key_holder(&srv));

    cwnet_server_poll(&srv, 2000 + CWNET_SERVER_DEFAULT_IDLE_MS, &res);
    TEST_ASSERT_EQUAL_INT(CWNET_SERVER_NOBODY, cwnet_server_key_holder(&srv));
    TEST_ASSERT_FALSE(cwnet_server_key_down(&srv));
    const cwnet_server_event_t *fault = event_first(CWNET_SERVER_EV_FAULT);
    TEST_ASSERT_NOT_NULL(fault);
    TEST_ASSERT_EQUAL_INT((int32_t)CWNET_SERVER_FAULT_IDLE, fault->value);
}

void test_server_an_over_past_its_ceiling_is_cut_off(void) {
    cwnet_server_cfg_t cfg;
    cwnet_server_cfg_defaults(&cfg);
    cfg.send_cb = fake_send;
    cfg.idle_timeout_ms = 600000u;   /* out of the way: the ceiling is on trial */
    cfg.over_max_ms = 3000u;
    server_setup_cfg(&cfg);

    int idx = ready_client("Moritz", "Moritz", 1000);
    cwnet_server_on_data(&srv, idx, morse_key_down_held, sizeof(morse_key_down_held),
                         2000, &res);
    TEST_ASSERT_EQUAL_INT(idx, cwnet_server_key_holder(&srv));

    cwnet_server_poll(&srv, 4999, &res);
    TEST_ASSERT_EQUAL_INT(idx, cwnet_server_key_holder(&srv));

    cwnet_server_poll(&srv, 5000, &res);
    TEST_ASSERT_EQUAL_INT(CWNET_SERVER_NOBODY, cwnet_server_key_holder(&srv));
    const cwnet_server_event_t *fault = event_first(CWNET_SERVER_EV_FAULT);
    TEST_ASSERT_NOT_NULL(fault);
    TEST_ASSERT_EQUAL_INT((int32_t)CWNET_SERVER_FAULT_OVER_TOO_LONG, fault->value);
}

/*===========================================================================*/
/* Robustness                                                                */
/*===========================================================================*/

static uint8_t huge_morse[3 + 65535];

void test_server_a_huge_morse_frame_fills_the_engine_and_counts_the_rest(void) {
    server_setup();
    int idx = ready_client("Moritz", "Moritz", 1000);

    huge_morse[0] = 0x80u | CWNET_CMD_MORSE;   /* long block */
    huge_morse[1] = 0xFFu;
    huge_morse[2] = 0xFFu;
    memset(huge_morse + 3, 0x80u, 65535);
    cwnet_server_on_data(&srv, idx, huge_morse, sizeof(huge_morse), 2000, &res);

    TEST_ASSERT_EQUAL_INT(idx, cwnet_server_key_holder(&srv));
    /* The engine holds the FIFO plus the byte whose deadline is running */
    TEST_ASSERT_EQUAL_UINT32(65535u - (CWNET_PLAY_FIFO_SIZE + 1u),
                             cwnet_server_play_dropped(&srv));
}

void test_server_survives_null_and_unknown_indices(void) {
    server_setup();
    int idx = ready_client("Moritz", "Moritz", 1000);

    TEST_ASSERT_FALSE(cwnet_server_init(NULL, NULL));
    cwnet_server_cfg_defaults(NULL);
    TEST_ASSERT_EQUAL_INT(CWNET_SERVER_NO_SLOT, cwnet_server_on_connected(NULL, 0, &res));
    cwnet_server_on_data(NULL, 1, NULL, 0, 0, &res);
    cwnet_server_on_disconnected(NULL, 1, 0, &res);
    cwnet_server_poll(NULL, 0, &res);

    int64_t at = 0;
    TEST_ASSERT_FALSE(cwnet_server_next_deadline(NULL, &at));
    TEST_ASSERT_FALSE(cwnet_server_next_deadline(&srv, NULL));
    TEST_ASSERT_TRUE(cwnet_server_next_deadline(&srv, &at));
    TEST_ASSERT_EQUAL_INT64(1000 + CWNET_SERVER_DEFAULT_PING_INTERVAL_MS, at);

    uint8_t morse[] = { 0x50, 0x01, 0x80 };
    cwnet_server_on_data(&srv, 99, morse, sizeof(morse), 2000, &res);
    cwnet_server_on_disconnected(&srv, 99, 2000, &res);
    cwnet_server_on_data(&srv, idx, NULL, 0, 2000, &res);

    TEST_ASSERT_EQUAL_INT(CWNET_SERVER_NOBODY, cwnet_server_key_holder(&srv));
    TEST_ASSERT_EQUAL_STRING("", cwnet_server_client_name(&srv, 99));
    TEST_ASSERT_EQUAL_INT32(-1, cwnet_server_client_latency_ms(&srv, 99));
    TEST_ASSERT_EQUAL_INT32(-1, cwnet_server_client_peak_ms(&srv, 99));
    TEST_ASSERT_EQUAL_size_t(1u, cwnet_server_client_count(&srv));
}

void test_server_a_failed_send_closes_that_client(void) {
    server_setup();
    int idx = cwnet_server_on_connected(&srv, 1000, &res);

    fake_send_fails = true;
    uint8_t connect[2 + CWNET_CONNECT_PAYLOAD_LEN];
    size_t len = build_connect(connect, sizeof(connect), "Moritz", "Moritz",
                               CWNET_CONNECT_PAYLOAD_LEN);
    cwnet_server_on_data(&srv, idx, connect, len, 1000, &res);

    TEST_ASSERT_EQUAL_size_t(0u, cwnet_server_client_count(&srv));
    const cwnet_server_event_t *ev = event_first(CWNET_SERVER_EV_CLIENT_CLOSED);
    TEST_ASSERT_NOT_NULL(ev);
    TEST_ASSERT_EQUAL_INT((int32_t)CWNET_SERVER_CLOSE_SEND_FAILED, ev->value);
}

/*===========================================================================*/
/* R3: the announcement, when one of its sends dies halfway through          */
/*===========================================================================*/

/*
 * The TX_INFO is one payload sent to everybody, and a send inside that loop
 * can close the client it is writing to. When that client is the one that
 * has just taken the key, closing it releases the key and announces the
 * release from inside the loop; whatever the outer loop has not reached yet
 * would then be told about a holder that no longer exists. The last word a
 * client hears on the key has to be the state as it is.
 */
void test_server_a_send_dying_mid_announcement_leaves_no_stale_tx_info(void) {
    server_setup();
    int first = ready_client("Moritz", "Moritz", 1000);
    int second = ready_client("Karl", "Karl", 1000);
    int third = ready_client("Anna", "Anna", 1000);
    TEST_ASSERT_EQUAL_INT(CWNET_SERVER_FIRST_CLIENT + 1, second);
    TEST_ASSERT_EQUAL_INT(CWNET_SERVER_FIRST_CLIENT + 2, third);
    wire_clear();

    /* The middle client takes the key and its own copy of the announcement
     * cannot go out: it is closed, and the key is free again at once */
    fake_send_fail_idx = second;
    uint8_t morse[] = { 0x50, 0x01, 0x80 };
    cwnet_server_on_data(&srv, second, morse, sizeof(morse), 2000, &res);

    TEST_ASSERT_EQUAL_INT(CWNET_SERVER_NOBODY, cwnet_server_key_holder(&srv));
    TEST_ASSERT_EQUAL_size_t(2u, cwnet_server_client_count(&srv));
    const cwnet_server_event_t *closed = event_first(CWNET_SERVER_EV_CLIENT_CLOSED);
    TEST_ASSERT_NOT_NULL(closed);
    TEST_ASSERT_EQUAL_INT(second, closed->client_idx);
    TEST_ASSERT_EQUAL_INT((int32_t)CWNET_SERVER_CLOSE_SEND_FAILED, closed->value);

    /* Neither survivor is left believing client 2 is on the key. The one
     * the loop had not reached yet is the one that used to get it wrong. */
    uint8_t frame[64];
    size_t frame_len = 0;
    TEST_ASSERT_TRUE(wire_frame(first, (uint8_t)CWNET_CMD_TX_INFO, SIZE_MAX,
                                frame, sizeof(frame), &frame_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(ref_tx_info_nobody), frame_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ref_tx_info_nobody, frame, sizeof(ref_tx_info_nobody));

    TEST_ASSERT_TRUE(wire_frame(third, (uint8_t)CWNET_CMD_TX_INFO, SIZE_MAX,
                                frame, sizeof(frame), &frame_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(ref_tx_info_nobody), frame_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ref_tx_info_nobody, frame, sizeof(ref_tx_info_nobody));
}

/*===========================================================================*/
/* R7: a link that answers, but never inside the window                      */
/*===========================================================================*/

/*
 * The peak-hold only moves when a sample lands inside the gate's 0..2000 ms
 * window. A link whose every answer is slower than that never records one,
 * so its peak stays at the -1 that also means "never pinged". The two are
 * not the same client: one has produced no evidence yet, the other has
 * produced nothing but evidence that it is too slow to be heard.
 */
void test_server_a_link_that_never_answers_inside_the_window_is_not_fit(void) {
    cwnet_server_cfg_t cfg;
    cwnet_server_cfg_defaults(&cfg);
    cfg.send_cb = fake_send;
    /* Wide enough that the slow answer still matches the pending request */
    cfg.ping_interval_ms = 10000u;
    server_setup_cfg(&cfg);

    int idx = ready_client("Moritz", "Moritz", 1000);
    TEST_ASSERT_EQUAL_INT32(-1, cwnet_server_client_peak_ms(&srv, idx));

    /* Three answers, none of them inside the window: no peak is ever held */
    ping_exchange(idx, 11000, 2500);
    ping_exchange(idx, 21000, 3000);
    ping_exchange(idx, 31000, 4000);
    TEST_ASSERT_EQUAL_INT32(-1, cwnet_server_client_peak_ms(&srv, idx));
    TEST_ASSERT_EQUAL_INT32(-1, cwnet_server_client_latency_ms(&srv, idx));
    TEST_ASSERT_EQUAL_size_t(0u, event_count(CWNET_SERVER_EV_LATENCY));
    wire_clear();

    uint8_t morse[] = { 0x50, 0x01, 0x80 };
    cwnet_server_on_data(&srv, idx, morse, sizeof(morse), 40000, &res);

    /* It does not take the key, nothing of its is played, and it is told */
    TEST_ASSERT_EQUAL_INT(CWNET_SERVER_NOBODY, cwnet_server_key_holder(&srv));
    TEST_ASSERT_EQUAL_size_t(0u, wire_frame_count(idx, (uint8_t)CWNET_CMD_TX_INFO));
    TEST_ASSERT_EQUAL_UINT32(1u, cwnet_server_morse_ignored(&srv));
    TEST_ASSERT_EQUAL_size_t(1u, event_count(CWNET_SERVER_EV_LINK_UNFIT));

    uint8_t frame[128];
    size_t frame_len = 0;
    TEST_ASSERT_TRUE(wire_frame(idx, (uint8_t)CWNET_SERVER_CMD_PRINT, 0,
                                frame, sizeof(frame), &frame_len));
    TEST_ASSERT_NOT_NULL(strstr((const char *)frame + 2, "not fit"));

    cwnet_server_poll(&srv, 41000, &res);
    TEST_ASSERT_FALSE(cwnet_server_key_down(&srv));
    TEST_ASSERT_FALSE(cwnet_server_ptt_on(&srv));
}

void test_server_a_link_that_has_never_been_pinged_still_gets_the_key(void) {
    server_setup();
    int idx = ready_client("Moritz", "Moritz", 1000);

    /* No PING has been answered at all: no measurement is not a bad one, or
     * every client would be refused for its first two seconds */
    TEST_ASSERT_EQUAL_INT32(-1, cwnet_server_client_peak_ms(&srv, idx));

    uint8_t morse[] = { 0x50, 0x01, 0x80 };
    cwnet_server_on_data(&srv, idx, morse, sizeof(morse), 2000, &res);

    TEST_ASSERT_EQUAL_INT(idx, cwnet_server_key_holder(&srv));
    TEST_ASSERT_EQUAL_UINT32(CWNET_SERVER_DEFAULT_BUFFER_FLOOR_MS,
                             cwnet_server_buffer_ms(&srv));
    TEST_ASSERT_EQUAL_size_t(0u, event_count(CWNET_SERVER_EV_LINK_UNFIT));
}

/*
 * A refused client keeps sending: it does not know it is refused until the
 * PRINT reaches it, and it may not stop even then. One event per rejected
 * byte fills the result's array and starts costing the daemon the events it
 * needs, so the refusal is reported exactly as often as it is printed: once,
 * until a sample brings the peak back under the ceiling.
 */
void test_server_an_unfit_link_is_reported_once_not_once_per_frame(void) {
    server_setup();
    int idx = ready_client("Moritz", "Moritz", 1000);

    ping_exchange(idx, 3000, 1200);   /* over the 1000 ms ceiling */
    TEST_ASSERT_EQUAL_INT32(1200, cwnet_server_client_peak_ms(&srv, idx));
    wire_clear();

    /* Forty MORSE frames in one read: forty asks for a key it cannot have */
    uint8_t stream[40u * 3u];
    for (size_t i = 0; i < 40u; i++) {
        static const uint8_t key_down = 0x80u;
        uint8_t one[3];
        TEST_ASSERT_EQUAL_size_t(sizeof(one),
                                 build_frame(one, sizeof(one), (uint8_t)CWNET_CMD_MORSE,
                                             &key_down, 1u));
        memcpy(stream + i * 3u, one, sizeof(one));
    }
    cwnet_server_on_data(&srv, idx, stream, sizeof(stream), 5000, &res);

    TEST_ASSERT_EQUAL_INT(CWNET_SERVER_NOBODY, cwnet_server_key_holder(&srv));
    TEST_ASSERT_EQUAL_UINT32(40u, cwnet_server_morse_ignored(&srv));
    TEST_ASSERT_EQUAL_size_t(1u, event_count(CWNET_SERVER_EV_LINK_UNFIT));
    TEST_ASSERT_EQUAL_size_t(1u, wire_frame_count(idx, (uint8_t)CWNET_SERVER_CMD_PRINT));
    /* And the result still has room for the events that matter */
    TEST_ASSERT_EQUAL_size_t(0u, res.dropped);
}

/*===========================================================================*/
/* The next deadline, with several of them in competition                    */
/*===========================================================================*/

/*
 * cwnet_server_next_deadline() is the daemon's loop timeout: everything the
 * server does on time does it because this number was the true minimum.
 */
void test_server_next_deadline_is_the_nearest_of_the_over_and_the_pings(void) {
    cwnet_server_cfg_t cfg;
    cwnet_server_cfg_defaults(&cfg);
    cfg.send_cb = fake_send;
    cfg.ping_interval_ms = 10000u;
    cfg.idle_timeout_ms = 4000u;
    server_setup_cfg(&cfg);

    int first = ready_client("Moritz", "Moritz", 1000);   /* PING due at 11000 */
    int second = ready_client("Karl", "Karl", 5000);      /* PING due at 15000 */
    TEST_ASSERT_EQUAL_INT(CWNET_SERVER_FIRST_CLIENT + 1, second);

    int64_t at = 0;
    TEST_ASSERT_TRUE(cwnet_server_next_deadline(&srv, &at));
    TEST_ASSERT_EQUAL_INT64(11000, at);   /* the nearer of the two PINGs */

    /* The key is taken at 6000. With no measurement B is the floor, so the
     * first edge is due a floor after it: nearer than either PING and than the
     * idle net at 10000 and the over ceiling at 126000. */
    cwnet_server_on_data(&srv, first, morse_key_down_held, sizeof(morse_key_down_held),
                         6000, &res);
    TEST_ASSERT_EQUAL_INT(first, cwnet_server_key_holder(&srv));
    TEST_ASSERT_TRUE(cwnet_server_next_deadline(&srv, &at));
    TEST_ASSERT_EQUAL_INT64(6000 + (int64_t)CWNET_SERVER_DEFAULT_BUFFER_FLOOR_MS, at);

    /* Played out — the two bytes hold the key down and then lift it, and
     * with an empty FIFO under them that is an underrun (R8), not the end of
     * an over: the holder keeps the key and the idle net is now the nearest
     * thing the server has to do. */
    cwnet_server_poll(&srv, 7000, &res);
    TEST_ASSERT_EQUAL_INT(first, cwnet_server_key_holder(&srv));
    TEST_ASSERT_TRUE(cwnet_server_next_deadline(&srv, &at));
    TEST_ASSERT_EQUAL_INT64(10000, at);   /* 6000 + the idle timeout */

    /* And that instant is the real one */
    cwnet_server_poll(&srv, 10000, &res);
    TEST_ASSERT_EQUAL_INT(CWNET_SERVER_NOBODY, cwnet_server_key_holder(&srv));
    const cwnet_server_event_t *fault = event_first(CWNET_SERVER_EV_FAULT);
    TEST_ASSERT_NOT_NULL(fault);
    TEST_ASSERT_EQUAL_INT((int32_t)CWNET_SERVER_FAULT_IDLE, fault->value);

    /* With the over gone the nearer PING comes back to the front */
    TEST_ASSERT_TRUE(cwnet_server_next_deadline(&srv, &at));
    TEST_ASSERT_EQUAL_INT64(11000, at);
}

void test_server_next_deadline_takes_an_expiring_handshake_before_the_rest(void) {
    cwnet_server_cfg_t cfg;
    cwnet_server_cfg_defaults(&cfg);
    cfg.send_cb = fake_send;
    cfg.handshake_timeout_ms = 200u;
    server_setup_cfg(&cfg);

    int ready = ready_client("Moritz", "Moritz", 1000);   /* PING due at 3000 */
    int64_t at = 0;
    TEST_ASSERT_TRUE(cwnet_server_next_deadline(&srv, &at));
    TEST_ASSERT_EQUAL_INT64(1000 + CWNET_SERVER_DEFAULT_PING_INTERVAL_MS, at);

    /* A second connection that has not logged in: its handshake runs out at
     * 2700, before anything else the server owes anybody */
    int silent = cwnet_server_on_connected(&srv, 2500, &res);
    TEST_ASSERT_EQUAL_INT(CWNET_SERVER_FIRST_CLIENT + 1, silent);
    TEST_ASSERT_TRUE(cwnet_server_next_deadline(&srv, &at));
    TEST_ASSERT_EQUAL_INT64(2700, at);

    cwnet_server_poll(&srv, at, &res);
    TEST_ASSERT_EQUAL_size_t(1u, cwnet_server_client_count(&srv));
    TEST_ASSERT_TRUE(cwnet_server_client_ready(&srv, ready));
    const cwnet_server_event_t *closed = event_first(CWNET_SERVER_EV_CLIENT_CLOSED);
    TEST_ASSERT_NOT_NULL(closed);
    TEST_ASSERT_EQUAL_INT(silent, closed->client_idx);
    TEST_ASSERT_EQUAL_INT((int32_t)CWNET_SERVER_CLOSE_HANDSHAKE, closed->value);

    /* With it gone the PING is the front of the queue again */
    TEST_ASSERT_TRUE(cwnet_server_next_deadline(&srv, &at));
    TEST_ASSERT_EQUAL_INT64(1000 + CWNET_SERVER_DEFAULT_PING_INTERVAL_MS, at);
}
