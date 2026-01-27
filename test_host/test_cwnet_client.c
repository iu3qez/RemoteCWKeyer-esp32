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

void test_client_receives_welcome_transitions_to_ready(void) {
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

    /* Simulate receiving WELCOME from server */
    /* CMD_WELCOME = 0x00 (no payload category) */
    uint8_t welcome[] = {0x00};
    cwnet_client_on_data(&client, welcome, sizeof(welcome));

    TEST_ASSERT_EQUAL(CWNET_STATE_READY, cwnet_client_get_state(&client));
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
    uint8_t welcome[] = {0x00};
    cwnet_client_on_data(&client, welcome, sizeof(welcome));
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

    uint8_t welcome[] = {0x00};
    cwnet_client_on_data(&client, welcome, sizeof(welcome));

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

    uint8_t welcome[] = {0x00};
    cwnet_client_on_data(&client, welcome, sizeof(welcome));

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

void test_client_sends_key_down_event(void) {
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

    uint8_t welcome[] = {0x00};
    cwnet_client_on_data(&client, welcome, sizeof(welcome));
    mock_tx_len = 0;

    /* Send key down event */
    mock_time_ms = 2000;
    cwnet_client_err_t err = cwnet_client_send_key_event(&client, true);
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, err);
    TEST_ASSERT_GREATER_THAN(0, mock_tx_len);

    /* Verify it's a CW_DOWN command with timestamp */
    /* CMD_CW_DOWN = 0x15 with category 1: (1 << 6) | 0x15 = 0x55 */
    TEST_ASSERT_EQUAL(0x55, mock_tx_buffer[0]);
}

void test_client_sends_key_up_event(void) {
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

    uint8_t welcome[] = {0x00};
    cwnet_client_on_data(&client, welcome, sizeof(welcome));
    mock_tx_len = 0;

    /* Send key up event */
    mock_time_ms = 2100;
    cwnet_client_err_t err = cwnet_client_send_key_event(&client, false);
    TEST_ASSERT_EQUAL(CWNET_CLIENT_OK, err);
    TEST_ASSERT_GREATER_THAN(0, mock_tx_len);

    /* CMD_CW_UP = 0x14 with category 1: (1 << 6) | 0x14 = 0x54 */
    TEST_ASSERT_EQUAL(0x54, mock_tx_buffer[0]);
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

    cwnet_client_err_t err = cwnet_client_send_key_event(&client, true);
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

    uint8_t welcome[] = {0x00};
    cwnet_client_on_data(&client, welcome, sizeof(welcome));

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

    uint8_t welcome[] = {0x00};
    cwnet_client_on_data(&client, welcome, sizeof(welcome));
    TEST_ASSERT_EQUAL(CWNET_STATE_READY, cwnet_client_get_state(&client));

    cwnet_client_on_disconnected(&client);
    TEST_ASSERT_EQUAL(CWNET_STATE_DISCONNECTED, cwnet_client_get_state(&client));

    /* Verify can't send events after disconnect */
    cwnet_client_err_t err = cwnet_client_send_key_event(&client, true);
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

    /* Send WELCOME in two fragments */
    uint8_t frag1[] = {0x00};  /* Just the command byte */
    cwnet_client_on_data(&client, frag1, sizeof(frag1));

    /* WELCOME has no payload, so single byte should complete it */
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

    uint8_t welcome[] = {0x00};
    cwnet_client_on_data(&client, welcome, sizeof(welcome));
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
    RUN_TEST(test_client_receives_welcome_transitions_to_ready);

    /* PING Handling */
    RUN_TEST(test_client_responds_to_ping_request);
    RUN_TEST(test_client_syncs_timer_on_ping_request);
    RUN_TEST(test_client_updates_latency_on_ping_response2);

    /* CW Events */
    RUN_TEST(test_client_sends_key_down_event);
    RUN_TEST(test_client_sends_key_up_event);
    RUN_TEST(test_client_rejects_events_when_not_ready);

    /* Error Handling */
    RUN_TEST(test_client_handles_invalid_frame);
    RUN_TEST(test_client_handles_disconnect_during_operation);

    /* Fragmentation */
    RUN_TEST(test_client_handles_fragmented_frame);
    RUN_TEST(test_client_handles_ping_in_fragments);
}
