/**
 * @file test_cwnet_client.c
 * @brief Unit tests for CWNet TCP client state machine
 *
 * Tests the client state machine logic in isolation (no actual sockets).
 * Socket operations are abstracted through callbacks/interface.
 */

#include "unity.h"
#include "cwnet_client.h"
#include "cwnet_frame.h"
#include "cwnet_ping.h"
#include <string.h>

/*===========================================================================*/
/* Test Fixtures                                                             */
/*===========================================================================*/

static cwnet_client_t client;

/* Mock data for testing */
static uint8_t mock_tx_buffer[256];
static size_t mock_tx_len;
static bool mock_connected;
static int32_t mock_time_ms;

/* Mock callbacks */
static int mock_send(const uint8_t *data, size_t len, void *user_data) {
    (void)user_data;
    if (len > sizeof(mock_tx_buffer)) {
        return -1;
    }
    memcpy(mock_tx_buffer, data, len);
    mock_tx_len = len;
    return (int)len;
}

static int32_t mock_get_time_ms(void *user_data) {
    (void)user_data;
    return mock_time_ms;
}

/* Local setup helper - call at start of each test */
static void test_setup(void) {
    memset(&client, 0, sizeof(client));
    memset(mock_tx_buffer, 0, sizeof(mock_tx_buffer));
    mock_tx_len = 0;
    mock_connected = false;
    mock_time_ms = 1000;
}

/*
 * The reference protocol has no WELCOME: 0x00 is CWNET_CMD_NONE, "dummy command
 * to send HTTP instead of our binary protocol" (CwNet.h). A real server confirms
 * a connection by echoing the 92-byte CONNECT record back with the permissions
 * field filled in, then sending a PRINT with the greeting.
 *
 * These are the first 94 bytes of the server-to-client direction of session 10
 * of the 2026-09-05 capture of the official DL4YHF client and server: the
 * CONNECT echo, user "Moritz", permissions 0x07 where the client had sent 0.
 */
static const uint8_t ref_connect_echo[] = {
    0x41, 0x5C, 0x4D, 0x6F, 0x72, 0x69, 0x74, 0x7A, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4D, 0x6F,
    0x72, 0x69, 0x74, 0x7A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
};

/* Drive the client to READY the way a real server does. */
static void feed_connect_echo(void) {
    cwnet_client_on_data(&client, ref_connect_echo, sizeof(ref_connect_echo));
}

/* Everything the client sends, in order: mock_send keeps only the last frame. */
static uint8_t mock_tx_all[512];
static size_t mock_tx_all_len;

static int mock_send_accumulate(const uint8_t *data, size_t len, void *user_data) {
    (void)user_data;
    if (mock_tx_all_len + len > sizeof(mock_tx_all)) {
        return -1;
    }
    memcpy(mock_tx_all + mock_tx_all_len, data, len);
    mock_tx_all_len += len;
    return (int)len;
}

/* A client at READY with TRANSMIT granted, capturing every byte it sends
 * from here on (the CONNECT it sent is dropped). */
static void ready_client_accumulating(void) {
    cwnet_client_config_t config = {
        .server_host = "test.server.com",
        .server_port = 7373,
        .username = "TEST",
        .send_cb = mock_send_accumulate,
        .get_time_ms_cb = mock_get_time_ms,
        .user_data = NULL
    };

    test_setup();
    mock_tx_all_len = 0;
    cwnet_client_init(&client, &config);
    cwnet_client_on_connected(&client);
    feed_connect_echo();
    mock_tx_all_len = 0;
}

/*===========================================================================*/
/* Initialization Tests                                                      */
/*===========================================================================*/

void test_client_init_basic(void) {
    test_setup();
    cwnet_client_config_t config = {
        .server_host = "test.server.com",
        .server_port = 7373,
        .username = "TEST",
        .send_cb = mock_send,
        .get_time_ms_cb = mock_get_time_ms,
        .user_data = NULL
    };

    cwnet_client_err_t err = cwnet_client_init(&client, &config);
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, err);
    TEST_ASSERT_EQUAL(CWNET_STATE_DISCONNECTED, cwnet_client_get_state(&client));
}

void test_client_init_null_client(void) {
    cwnet_client_config_t config = {
        .server_host = "test.server.com",
        .server_port = 7373,
        .username = "TEST",
        .send_cb = mock_send,
        .get_time_ms_cb = mock_get_time_ms,
        .user_data = NULL
    };

    cwnet_client_err_t err = cwnet_client_init(NULL, &config);
    TEST_ASSERT_EQUAL(CWNET_CLIENT_ERR_INVALID_ARG, err);
}

void test_client_init_null_config(void) {
    cwnet_client_err_t err = cwnet_client_init(&client, NULL);
    TEST_ASSERT_EQUAL(CWNET_CLIENT_ERR_INVALID_ARG, err);
}

void test_client_init_null_callbacks(void) {
    cwnet_client_config_t config = {
        .server_host = "test.server.com",
        .server_port = 7373,
        .username = "TEST",
        .send_cb = NULL,  /* Missing required callback */
        .get_time_ms_cb = mock_get_time_ms,
        .user_data = NULL
    };

    cwnet_client_err_t err = cwnet_client_init(&client, &config);
    TEST_ASSERT_EQUAL(CWNET_CLIENT_ERR_INVALID_ARG, err);
}

void test_client_init_empty_host(void) {
    cwnet_client_config_t config = {
        .server_host = "",
        .server_port = 7373,
        .username = "TEST",
        .send_cb = mock_send,
        .get_time_ms_cb = mock_get_time_ms,
        .user_data = NULL
    };

    cwnet_client_err_t err = cwnet_client_init(&client, &config);
    TEST_ASSERT_EQUAL(CWNET_CLIENT_ERR_INVALID_ARG, err);
}

void test_client_init_empty_username(void) {
    cwnet_client_config_t config = {
        .server_host = "test.server.com",
        .server_port = 7373,
        .username = "",  /* Empty username should be allowed - server will assign */
        .send_cb = mock_send,
        .get_time_ms_cb = mock_get_time_ms,
        .user_data = NULL
    };

    cwnet_client_err_t err = cwnet_client_init(&client, &config);
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, err);
}

/*===========================================================================*/
/* State Transition Tests                                                    */
/*===========================================================================*/

void test_client_connect_transitions_to_connecting(void) {
    cwnet_client_config_t config = {
        .server_host = "test.server.com",
        .server_port = 7373,
        .username = "TEST",
        .send_cb = mock_send,
        .get_time_ms_cb = mock_get_time_ms,
        .user_data = NULL
    };

    cwnet_client_init(&client, &config);
    TEST_ASSERT_EQUAL(CWNET_STATE_DISCONNECTED, cwnet_client_get_state(&client));

    /* Simulate connection established externally */
    cwnet_client_on_connected(&client);
    TEST_ASSERT_EQUAL(CWNET_STATE_CONNECTING, cwnet_client_get_state(&client));
}

void test_client_disconnect_from_any_state(void) {
    cwnet_client_config_t config = {
        .server_host = "test.server.com",
        .server_port = 7373,
        .username = "TEST",
        .send_cb = mock_send,
        .get_time_ms_cb = mock_get_time_ms,
        .user_data = NULL
    };

    cwnet_client_init(&client, &config);
    cwnet_client_on_connected(&client);
    TEST_ASSERT_EQUAL(CWNET_STATE_CONNECTING, cwnet_client_get_state(&client));

    cwnet_client_on_disconnected(&client);
    TEST_ASSERT_EQUAL(CWNET_STATE_DISCONNECTED, cwnet_client_get_state(&client));
}

/*===========================================================================*/
/* Protocol Handshake Tests                                                  */
/*===========================================================================*/

void test_client_sends_ident_on_connect(void) {
    cwnet_client_config_t config = {
        .server_host = "test.server.com",
        .server_port = 7373,
        .username = "IK1TEST",
        .send_cb = mock_send,
        .get_time_ms_cb = mock_get_time_ms,
        .user_data = NULL
    };

    cwnet_client_init(&client, &config);
    cwnet_client_on_connected(&client);

    /* Should have sent IDENT frame */
    TEST_ASSERT_GREATER_THAN(0, mock_tx_len);

    /* Verify frame structure: CONNECT (0x01) with short payload (cat=1): (1<<6)|0x01 = 0x41 */
    TEST_ASSERT_EQUAL(0x41, mock_tx_buffer[0]);  /* CONNECT with category 1 (short payload) */
}




void test_client_reaches_ready_on_connect_echo(void) {
    test_setup();
    cwnet_client_config_t config = {
        .server_host = "test.server.com",
        .server_port = 7373,
        .username = "Moritz",
        .send_cb = mock_send,
        .get_time_ms_cb = mock_get_time_ms,
        .user_data = NULL
    };

    cwnet_client_init(&client, &config);
    cwnet_client_on_connected(&client);

    cwnet_client_on_data(&client, ref_connect_echo, sizeof(ref_connect_echo));

    TEST_ASSERT_EQUAL(CWNET_STATE_READY, cwnet_client_get_state(&client));
}


/*
 * The permissions bitmask (H4) is how the client learns what the server allows:
 * it sends zero and the server returns the granted bits in the echo. In the
 * session-10 capture that is 0x07 = TALK | TRANSMIT | CTRL_RIG.
 */
void test_client_keeps_permissions_from_connect_echo(void) {
    test_setup();
    cwnet_client_config_t config = {
        .server_host = "test.server.com",
        .server_port = 7373,
        .username = "Moritz",
        .send_cb = mock_send,
        .get_time_ms_cb = mock_get_time_ms,
        .user_data = NULL
    };

    cwnet_client_init(&client, &config);
    cwnet_client_on_connected(&client);

    feed_connect_echo();

    TEST_ASSERT_EQUAL_HEX32(0x00000007, cwnet_client_get_permissions(&client));
}


/*
 * Without TRANSMIT the server silently discards our keying: no rejection
 * frame, so the operator would hear their own sidetone and believe they were
 * on the air. Refusing is the fault-philosophy reading -- corrupted or ignored
 * CW is worse than silence.
 *
 * The bytes are the session-10 echo with the permissions field changed to TALK
 * only. That combination is one the reference supports and our capture never
 * exercised, because the test account was granted 0x07.
 */
void test_client_refuses_to_key_without_transmit_permission(void) {
    test_setup();
    cwnet_client_config_t config = {
        .server_host = "test.server.com",
        .server_port = 7373,
        .username = "Moritz",
        .send_cb = mock_send,
        .get_time_ms_cb = mock_get_time_ms,
        .user_data = NULL
    };

    cwnet_client_init(&client, &config);
    cwnet_client_on_connected(&client);

    uint8_t echo[sizeof(ref_connect_echo)];
    memcpy(echo, ref_connect_echo, sizeof(echo));
    echo[2 + CWNET_CONNECT_PERMISSIONS_OFFSET] = CWNET_PERMISSION_TALK;
    cwnet_client_on_data(&client, echo, sizeof(echo));

    /* The connection is up: this is not a connection failure. */
    TEST_ASSERT_EQUAL(CWNET_STATE_READY, cwnet_client_get_state(&client));

    TEST_ASSERT_EQUAL(CWNET_CLIENT_ERR_NOT_PERMITTED,
                      cwnet_client_send_key_event(&client, true, 1000));
}

/*===========================================================================*/
/* PING Handling Tests                                                       */
/*===========================================================================*/

void test_client_responds_to_ping_request(void) {
    cwnet_client_config_t config = {
        .server_host = "test.server.com",
        .server_port = 7373,
        .username = "TEST",
        .send_cb = mock_send,
        .get_time_ms_cb = mock_get_time_ms,
        .user_data = NULL
    };

    cwnet_client_init(&client, &config);
    cwnet_client_on_connected(&client);

    /* Get to READY state */
    feed_connect_echo();
    mock_tx_len = 0;

    /* Simulate PING REQUEST from server */
    /* CMD_PING = 0x03, category 1 = short payload -> (1 << 6) | 0x03 = 0x43 */
    /* Frame: cmd(1) + len(1) + payload(16) */
    uint8_t ping_request[2 + CWNET_PING_PAYLOAD_SIZE] = {
        0x43,  /* CMD_PING with short payload category */
        0x10,  /* Length: 16 bytes */
        /* PING payload: type=REQUEST, id=1, t0=1000, t1=0, t2=0 */
        0x00, 0x01, 0x00, 0x00,  /* type, id, reserved */
        0xE8, 0x03, 0x00, 0x00,  /* t0 = 1000 (little-endian) */
        0x00, 0x00, 0x00, 0x00,  /* t1 = 0 */
        0x00, 0x00, 0x00, 0x00   /* t2 = 0 */
    };

    mock_time_ms = 1050;  /* Our local time when receiving */
    cwnet_client_on_data(&client, ping_request, sizeof(ping_request));

    /* Should have sent PING RESPONSE_1 */
    TEST_ASSERT_GREATER_THAN(0, mock_tx_len);

    /* Verify response has type=RESPONSE_1 and preserves t0 */
    /* Skip frame header (2 bytes for short payload), check payload */
    TEST_ASSERT_EQUAL(0x01, mock_tx_buffer[2]);  /* type = RESPONSE_1 */
    TEST_ASSERT_EQUAL(0x01, mock_tx_buffer[3]);  /* id preserved */
}

void test_client_syncs_timer_on_ping_request(void) {
    cwnet_client_config_t config = {
        .server_host = "test.server.com",
        .server_port = 7373,
        .username = "TEST",
        .send_cb = mock_send,
        .get_time_ms_cb = mock_get_time_ms,
        .user_data = NULL
    };

    cwnet_client_init(&client, &config);
    cwnet_client_on_connected(&client);

    feed_connect_echo();

    /* Initial timer offset should be 0 */
    mock_time_ms = 0;
    int32_t synced_before = cwnet_client_get_synced_time(&client);
    TEST_ASSERT_EQUAL(0, synced_before);

    /* Receive PING with server time = 5000 */
    uint8_t ping_request[2 + CWNET_PING_PAYLOAD_SIZE] = {
        0x43,  /* CMD_PING with short payload category */
        0x10,  /* Length: 16 bytes */
        0x00, 0x01, 0x00, 0x00,
        0x88, 0x13, 0x00, 0x00,  /* t0 = 5000 */
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };

    mock_time_ms = 100;  /* Our local time is 100 */
    cwnet_client_on_data(&client, ping_request, sizeof(ping_request));

    /* After sync, our synced time should be approximately server time */
    int32_t synced_after = cwnet_client_get_synced_time(&client);
    TEST_ASSERT_EQUAL(5000, synced_after);
}

void test_client_updates_latency_on_ping_response2(void) {
    cwnet_client_config_t config = {
        .server_host = "test.server.com",
        .server_port = 7373,
        .username = "TEST",
        .send_cb = mock_send,
        .get_time_ms_cb = mock_get_time_ms,
        .user_data = NULL
    };

    cwnet_client_init(&client, &config);
    cwnet_client_on_connected(&client);

    feed_connect_echo();

    /* Initial latency should be -1 (unknown) */
    TEST_ASSERT_EQUAL(-1, cwnet_client_get_latency_ms(&client));

    /* Receive PING RESPONSE_2 with RTT data */
    /* t0 = 1000 (when we sent), t2 = 1050 (server echoed back) */
    uint8_t ping_response2[2 + CWNET_PING_PAYLOAD_SIZE] = {
        0x43,  /* CMD_PING with short payload category */
        0x10,  /* Length: 16 bytes */
        0x02, 0x01, 0x00, 0x00,  /* type = RESPONSE_2, id = 1 */
        0xE8, 0x03, 0x00, 0x00,  /* t0 = 1000 */
        0x14, 0x04, 0x00, 0x00,  /* t1 = 1044 */
        0x1A, 0x04, 0x00, 0x00   /* t2 = 1050 */
    };

    cwnet_client_on_data(&client, ping_response2, sizeof(ping_response2));

    /* Latency = t2 - t0 = 1050 - 1000 = 50ms */
    TEST_ASSERT_EQUAL(50, cwnet_client_get_latency_ms(&client));
}

/*===========================================================================*/
/* CW Event Tests                                                            */
/*===========================================================================*/

/*
 * Keying goes out as MORSE 0x10: one byte per transition, bit 7 the new key
 * state, bits 6..0 the 7-bit wait since the previous transition. Reference:
 * DL4YHF CwStreamEnc.c and KeyerThread.c (sw_MorseTxFifo); the synthetic
 * expectations below were produced by tools/cwnet/keyer_sim.c, which runs
 * that algorithm on the reference's own encoder.
 */

void test_client_tx_first_transition_of_an_over_waits_zero(void) {
    ready_client_accumulating();

    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, true, 5000));

    /* 0x50 = short block | MORSE; one byte: key down, wait 0 */
    static const uint8_t expected[] = {0x50, 0x01, 0x80};
    TEST_ASSERT_EQUAL(sizeof(expected), mock_tx_all_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mock_tx_all, sizeof(expected));
}

void test_client_tx_wait_is_measured_from_previous_transition(void) {
    ready_client_accumulating();

    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, true, 5000));
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, false, 5048));

    /* 48 ms is in the 4 ms range: 0x20 + (48 - 32) / 4 = 0x24, key up */
    static const uint8_t expected[] = {0x50, 0x01, 0x80, 0x50, 0x01, 0x24};
    TEST_ASSERT_EQUAL(sizeof(expected), mock_tx_all_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mock_tx_all, sizeof(expected));
}

/*
 * Client-to-server bytes 238..278 of session 12 of the 2026-09-05 capture of
 * the official DL4YHF client: its first over, the letter A at 25 WPM (dot
 * 48 ms) and then silence. Five MORSE frames, with the two rigctld set_ptt
 * frames the reference interleaves around an over (PTT is #13; we do not
 * send it, so the comparison is on the MORSE frames alone).
 */
static const uint8_t ref_first_over[] = {
    0x50, 0x01, 0x80,                                     /* key down, wait 0      */
    0x46, 0x0B, 0x73, 0x65, 0x74, 0x5F, 0x70, 0x74, 0x74,
    0x20, 0x31, 0x0A, 0x00,                               /* RIGCTLD "set_ptt 1"   */
    0x50, 0x01, 0x24,                                     /* key up   after  48 ms */
    0x50, 0x01, 0xA4,                                     /* key down after  48 ms */
    0x50, 0x01, 0x3C,                                     /* key up   after 144 ms */
    0x46, 0x0B, 0x73, 0x65, 0x74, 0x5F, 0x70, 0x74, 0x74,
    0x20, 0x30, 0x0A, 0x00,                               /* RIGCTLD "set_ptt 0"   */
    0x50, 0x01, 0x60,                                     /* end of over, 669 ms   */
};

void test_client_tx_first_over_matches_reference_capture(void) {
    ready_client_accumulating();

    /* The edges the reference client saw, reconstructed from its own bytes */
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, true,  1000));
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, false, 1048));
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, true,  1096));
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, false, 1240));
    /* 14 dot-times = 672 ms: not at exactly 672, sent at the first poll past it */
    TEST_ASSERT_FALSE(cwnet_client_poll(&client, 1240 + 672, 48));
    TEST_ASSERT_TRUE(cwnet_client_poll(&client, 1240 + 673, 48));

    /* Expected: the raw bytes of every MORSE frame in the capture, in order */
    uint8_t expected[sizeof(ref_first_over)];
    size_t expected_len = 0;
    cwnet_frame_parser_t parser;
    cwnet_frame_parser_init(&parser);
    size_t off = 0;
    while (off < sizeof(ref_first_over)) {
        cwnet_parse_result_t r = cwnet_frame_parse(&parser, ref_first_over + off,
                                                   sizeof(ref_first_over) - off);
        TEST_ASSERT_EQUAL(CWNET_PARSE_OK, r.status);
        if (r.command == CWNET_CMD_MORSE) {
            memcpy(expected + expected_len, ref_first_over + off, r.bytes_consumed);
            expected_len += r.bytes_consumed;
        }
        off += r.bytes_consumed;
    }
    TEST_ASSERT_EQUAL(15, expected_len);  /* five frames of three bytes */

    TEST_ASSERT_EQUAL(expected_len, mock_tx_all_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mock_tx_all, expected_len);
}

void test_client_tx_advances_by_encoded_not_measured_ms(void) {
    ready_client_accumulating();

    /* 33 ms edges: 33 encodes as 32 (0x20). The reference advances its
     * stopwatch by the encoded 32, so the next interval measures 34, then
     * 35, then 36, which finally encodes as 0x21. Advancing by the measured
     * 33 would send 0x20 forever and let the receiver drift 1 ms per edge. */
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, true,  0));
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, false, 33));
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, true,  66));
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, false, 99));
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, true,  132));

    static const uint8_t expected[] = {
        0x50, 0x01, 0x80,
        0x50, 0x01, 0x20,
        0x50, 0x01, 0xA0,
        0x50, 0x01, 0x20,
        0x50, 0x01, 0xA1,
    };
    TEST_ASSERT_EQUAL(sizeof(expected), mock_tx_all_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mock_tx_all, sizeof(expected));
}

void test_client_tx_splits_wait_above_1165_ms(void) {
    ready_client_accumulating();

    /* A 2000 ms gap inside an over (below the end-of-over threshold at slow
     * speed): 1165 ms as 0x7F, then 835 ms as 0x6A, both key down, in one
     * frame. The stopwatch advances by 1165 + 829 = 1994, so the next
     * interval measures 30 + 6 = 36 ms. */
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, true,  0));
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, false, 20));
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, true,  2020));
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, false, 2050));

    static const uint8_t expected[] = {
        0x50, 0x01, 0x80,
        0x50, 0x01, 0x14,
        0x50, 0x02, 0xFF, 0xEA,
        0x50, 0x01, 0x21,
    };
    TEST_ASSERT_EQUAL(sizeof(expected), mock_tx_all_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mock_tx_all, sizeof(expected));
}

void test_client_tx_end_of_over_after_14_dot_times(void) {
    ready_client_accumulating();

    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, true,  0));
    /* Key still down: nothing to close, whatever the elapsed time */
    TEST_ASSERT_FALSE(cwnet_client_poll(&client, 5000, 48));
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, false, 48));

    /* Strictly more than 14 dot-times */
    TEST_ASSERT_FALSE(cwnet_client_poll(&client, 48 + 672, 48));
    TEST_ASSERT_TRUE(cwnet_client_poll(&client, 48 + 673, 48));
    /* Once: the over is closed until the next transition */
    TEST_ASSERT_FALSE(cwnet_client_poll(&client, 48 + 2000, 48));
    /* The next transition opens a new over with wait 0 */
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, true, 5000));

    static const uint8_t expected[] = {
        0x50, 0x01, 0x80,
        0x50, 0x01, 0x24,
        0x50, 0x01, 0x60,   /* 673 ms in the 16 ms range: 0x40 + (673 - 157) / 16 */
        0x50, 0x01, 0x80,
    };
    TEST_ASSERT_EQUAL(sizeof(expected), mock_tx_all_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mock_tx_all, sizeof(expected));
}

void test_client_tx_end_of_over_splits_at_slow_speed(void) {
    ready_client_accumulating();

    /* At 12 WPM (dot 100 ms) 14 dot-times are 1400 ms, more than one byte
     * can carry: the end of the over is 1165 + 236 ms, two key-up bytes in
     * one frame, three consecutive key-ups in the stream. */
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, true,  0));
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, false, 100));
    TEST_ASSERT_FALSE(cwnet_client_poll(&client, 100 + 1400, 100));
    TEST_ASSERT_TRUE(cwnet_client_poll(&client, 100 + 1401, 100));

    static const uint8_t expected[] = {
        0x50, 0x01, 0x80,
        0x50, 0x01, 0x31,
        0x50, 0x02, 0x7F, 0x44,
    };
    TEST_ASSERT_EQUAL(sizeof(expected), mock_tx_all_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mock_tx_all, sizeof(expected));
}

void test_client_tx_ignores_repeated_key_state(void) {
    ready_client_accumulating();

    /* The reference encodes only on a change of its keying output */
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, true,  0));
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, true,  10));
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, false, 48));
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, cwnet_client_send_key_event(&client, false, 60));

    static const uint8_t expected[] = {0x50, 0x01, 0x80, 0x50, 0x01, 0x24};
    TEST_ASSERT_EQUAL(sizeof(expected), mock_tx_all_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, mock_tx_all, sizeof(expected));
}

void test_client_rejects_events_when_not_ready(void) {
    cwnet_client_config_t config = {
        .server_host = "test.server.com",
        .server_port = 7373,
        .username = "TEST",
        .send_cb = mock_send,
        .get_time_ms_cb = mock_get_time_ms,
        .user_data = NULL
    };

    cwnet_client_init(&client, &config);
    /* Don't connect - stay in DISCONNECTED state */

    cwnet_client_err_t err = cwnet_client_send_key_event(&client, true, 1000);
    TEST_ASSERT_EQUAL(CWNET_CLIENT_ERR_NOT_READY, err);
}

/*===========================================================================*/
/* Error Handling Tests                                                      */
/*===========================================================================*/

void test_client_handles_invalid_frame(void) {
    cwnet_client_config_t config = {
        .server_host = "test.server.com",
        .server_port = 7373,
        .username = "TEST",
        .send_cb = mock_send,
        .get_time_ms_cb = mock_get_time_ms,
        .user_data = NULL
    };

    cwnet_client_init(&client, &config);
    cwnet_client_on_connected(&client);

    feed_connect_echo();

    /* Send garbage data */
    uint8_t garbage[] = {0xFF, 0xFF, 0xFF, 0xFF};
    cwnet_client_on_data(&client, garbage, sizeof(garbage));

    /* Client should remain in READY state (graceful handling) */
    TEST_ASSERT_EQUAL(CWNET_STATE_READY, cwnet_client_get_state(&client));
}

void test_client_handles_disconnect_during_operation(void) {
    cwnet_client_config_t config = {
        .server_host = "test.server.com",
        .server_port = 7373,
        .username = "TEST",
        .send_cb = mock_send,
        .get_time_ms_cb = mock_get_time_ms,
        .user_data = NULL
    };

    cwnet_client_init(&client, &config);
    cwnet_client_on_connected(&client);

    feed_connect_echo();
    TEST_ASSERT_EQUAL(CWNET_STATE_READY, cwnet_client_get_state(&client));

    cwnet_client_on_disconnected(&client);
    TEST_ASSERT_EQUAL(CWNET_STATE_DISCONNECTED, cwnet_client_get_state(&client));

    /* Verify can't send events after disconnect */
    cwnet_client_err_t err = cwnet_client_send_key_event(&client, true, 1000);
    TEST_ASSERT_EQUAL(CWNET_CLIENT_ERR_NOT_READY, err);
}

/*===========================================================================*/
/* Fragmentation Tests                                                       */
/*===========================================================================*/

void test_client_handles_fragmented_frame(void) {
    cwnet_client_config_t config = {
        .server_host = "test.server.com",
        .server_port = 7373,
        .username = "TEST",
        .send_cb = mock_send,
        .get_time_ms_cb = mock_get_time_ms,
        .user_data = NULL
    };

    cwnet_client_init(&client, &config);
    cwnet_client_on_connected(&client);

    /* A 94-byte CONNECT echo does not arrive in one TCP segment. Split it. */
    cwnet_client_on_data(&client, ref_connect_echo, 40);
    TEST_ASSERT_EQUAL(CWNET_STATE_CONNECTING, cwnet_client_get_state(&client));

    cwnet_client_on_data(&client, ref_connect_echo + 40, sizeof(ref_connect_echo) - 40);
    TEST_ASSERT_EQUAL(CWNET_STATE_READY, cwnet_client_get_state(&client));
}

void test_client_handles_ping_in_fragments(void) {
    cwnet_client_config_t config = {
        .server_host = "test.server.com",
        .server_port = 7373,
        .username = "TEST",
        .send_cb = mock_send,
        .get_time_ms_cb = mock_get_time_ms,
        .user_data = NULL
    };

    cwnet_client_init(&client, &config);
    cwnet_client_on_connected(&client);

    feed_connect_echo();
    mock_tx_len = 0;

    /* Send PING REQUEST in fragments */
    uint8_t ping_full[2 + CWNET_PING_PAYLOAD_SIZE] = {
        0x43,  /* CMD_PING with short payload category */
        0x10,  /* Length: 16 bytes */
        0x00, 0x01, 0x00, 0x00,
        0xE8, 0x03, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };

    /* First fragment: header only */
    cwnet_client_on_data(&client, ping_full, 2);
    TEST_ASSERT_EQUAL(0, mock_tx_len);  /* No response yet */

    /* Second fragment: payload */
    mock_time_ms = 1050;
    cwnet_client_on_data(&client, ping_full + 2, CWNET_PING_PAYLOAD_SIZE);

    /* Now should have responded */
    TEST_ASSERT_GREATER_THAN(0, mock_tx_len);
}

/*===========================================================================*/
/* Test Runner                                                               */
/*===========================================================================*/

void run_cwnet_client_tests(void) {
    /* Initialization */
    RUN_TEST(test_client_init_basic);
    RUN_TEST(test_client_init_null_client);
    RUN_TEST(test_client_init_null_config);
    RUN_TEST(test_client_init_null_callbacks);
    RUN_TEST(test_client_init_empty_host);
    RUN_TEST(test_client_init_empty_username);

    /* State Transitions */
    RUN_TEST(test_client_connect_transitions_to_connecting);
    RUN_TEST(test_client_disconnect_from_any_state);

    /* Protocol Handshake */
    RUN_TEST(test_client_sends_ident_on_connect);
    RUN_TEST(test_client_reaches_ready_on_connect_echo);

    /* PING Handling */
    RUN_TEST(test_client_responds_to_ping_request);
    RUN_TEST(test_client_syncs_timer_on_ping_request);
    RUN_TEST(test_client_updates_latency_on_ping_response2);

    /* CW Events */
    RUN_TEST(test_client_tx_first_transition_of_an_over_waits_zero);
    RUN_TEST(test_client_tx_wait_is_measured_from_previous_transition);
    RUN_TEST(test_client_tx_first_over_matches_reference_capture);
    RUN_TEST(test_client_tx_advances_by_encoded_not_measured_ms);
    RUN_TEST(test_client_tx_splits_wait_above_1165_ms);
    RUN_TEST(test_client_tx_end_of_over_after_14_dot_times);
    RUN_TEST(test_client_tx_end_of_over_splits_at_slow_speed);
    RUN_TEST(test_client_tx_ignores_repeated_key_state);
    RUN_TEST(test_client_rejects_events_when_not_ready);

    /* Error Handling */
    RUN_TEST(test_client_handles_invalid_frame);
    RUN_TEST(test_client_handles_disconnect_during_operation);

    /* Fragmentation */
    RUN_TEST(test_client_handles_fragmented_frame);
    RUN_TEST(test_client_handles_ping_in_fragments);
}
