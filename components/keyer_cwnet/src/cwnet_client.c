/**
 * @file cwnet_client.c
 * @brief CWNet TCP client state machine implementation
 */

#include "cwnet_client.h"
#include <string.h>

/*===========================================================================*/
/* Internal Helpers                                                          */
/*===========================================================================*/

/**
 * @brief Set state and notify callback
 */
static void set_state(cwnet_client_t *client, cwnet_client_state_t new_state) {
    if (client == NULL || client->state == new_state) {
        return;
    }

    cwnet_client_state_t old_state = client->state;
    client->state = new_state;

    if (client->state_change_cb != NULL) {
        client->state_change_cb(old_state, new_state, client->user_data);
    }
}

/**
 * @brief Send a frame through the callback
 */
static int send_frame(cwnet_client_t *client, const uint8_t *data, size_t len) {
    if (client == NULL || client->send_cb == NULL) {
        return -1;
    }
    return client->send_cb(data, len, client->user_data);
}

/**
 * @brief Get current local time
 */
static int32_t get_local_time(cwnet_client_t *client) {
    if (client == NULL || client->get_time_ms_cb == NULL) {
        return 0;
    }
    return client->get_time_ms_cb(client->user_data);
}

/*===========================================================================*/
/* Protocol Frame Builders                                                   */
/*===========================================================================*/

/**
 * @brief Build and send IDENT frame
 *
 * IDENT frame format:
 *   - cmd byte: (IDENT << 2) | CAT_SHORT (0x61)
 *   - length: username length (1 byte)
 *   - payload: username bytes
 */
static cwnet_client_err_t send_ident(cwnet_client_t *client) {
    size_t username_len = strlen(client->username);

    /* Frame: cmd(1) + len(1) + username(n) */
    uint8_t frame[2 + CWNET_MAX_USERNAME_LEN];
    size_t frame_len = 2 + username_len;

    /* Command byte: IDENT with short payload category */
    frame[0] = (uint8_t)((CWNET_CMD_IDENT << 2) | CWNET_FRAME_CAT_SHORT_PAYLOAD);
    frame[1] = (uint8_t)username_len;

    if (username_len > 0) {
        memcpy(&frame[2], client->username, username_len);
    }

    int sent = send_frame(client, frame, frame_len);
    if (sent < 0 || (size_t)sent != frame_len) {
        return CWNET_CLIENT_ERR_SEND_FAILED;
    }

    return CWNET_CLIENT_OK;
}

/**
 * @brief Build and send PING RESPONSE_1
 */
static cwnet_client_err_t send_ping_response(cwnet_client_t *client,
                                              const cwnet_ping_t *request) {
    /* Get our synced time for t1 */
    int32_t local_time = get_local_time(client);
    int32_t our_time = cwnet_timer_read_synced_ms(&client->timer, local_time);

    /* Build response payload */
    uint8_t payload[CWNET_PING_PAYLOAD_SIZE];
    if (!cwnet_ping_build_response(request, payload, sizeof(payload), our_time)) {
        return CWNET_CLIENT_ERR_PROTOCOL;
    }

    /* Frame: cmd(1) + len_hi(1) + len_lo(1) + payload(16) */
    uint8_t frame[3 + CWNET_PING_PAYLOAD_SIZE];
    frame[0] = (uint8_t)((CWNET_CMD_PING << 2) | CWNET_FRAME_CAT_LONG_PAYLOAD);
    frame[1] = 0x00;  /* Length high byte */
    frame[2] = CWNET_PING_PAYLOAD_SIZE;  /* Length low byte */
    memcpy(&frame[3], payload, CWNET_PING_PAYLOAD_SIZE);

    int sent = send_frame(client, frame, sizeof(frame));
    if (sent < 0 || (size_t)sent != sizeof(frame)) {
        return CWNET_CLIENT_ERR_SEND_FAILED;
    }

    return CWNET_CLIENT_OK;
}

/**
 * @brief Build and send CW event frame
 *
 * CW event frame format (short payload with timestamp):
 *   - cmd byte: (CW_DOWN/CW_UP << 2) | CAT_SHORT
 *   - length: 4 (32-bit timestamp)
 *   - payload: 4-byte little-endian synced timestamp
 */
static cwnet_client_err_t send_cw_event(cwnet_client_t *client, bool key_down) {
    /* Get synced timestamp */
    int32_t local_time = get_local_time(client);
    int32_t timestamp = cwnet_timer_read_synced_ms(&client->timer, local_time);

    /* Frame: cmd(1) + len(1) + timestamp(4) */
    uint8_t frame[6];
    cwnet_cmd_t cmd = key_down ? CWNET_CMD_CW_DOWN : CWNET_CMD_CW_UP;
    frame[0] = (uint8_t)((cmd << 2) | CWNET_FRAME_CAT_SHORT_PAYLOAD);
    frame[1] = 4;  /* 4-byte timestamp */

    /* Little-endian timestamp */
    uint32_t ts = (uint32_t)timestamp;
    frame[2] = (uint8_t)(ts & 0xFF);
    frame[3] = (uint8_t)((ts >> 8) & 0xFF);
    frame[4] = (uint8_t)((ts >> 16) & 0xFF);
    frame[5] = (uint8_t)((ts >> 24) & 0xFF);

    int sent = send_frame(client, frame, sizeof(frame));
    if (sent < 0 || (size_t)sent != sizeof(frame)) {
        return CWNET_CLIENT_ERR_SEND_FAILED;
    }

    return CWNET_CLIENT_OK;
}

/*===========================================================================*/
/* Frame Handlers                                                            */
/*===========================================================================*/

/**
 * @brief Handle WELCOME frame
 */
static void handle_welcome(cwnet_client_t *client) {
    if (client->state == CWNET_STATE_CONNECTING) {
        set_state(client, CWNET_STATE_READY);
    }
}

/**
 * @brief Handle PING frame
 */
static void handle_ping(cwnet_client_t *client,
                        const uint8_t *payload,
                        size_t len) {
    cwnet_ping_t ping;
    if (!cwnet_ping_parse(&ping, payload, len)) {
        return;  /* Invalid PING, ignore */
    }

    switch (ping.type) {
        case CWNET_PING_REQUEST:
            /* Sync timer to server time BEFORE responding */
            {
                int32_t local_time = get_local_time(client);
                cwnet_timer_sync_to_server(&client->timer, ping.t0_ms, local_time);
            }
            /* Send RESPONSE_1 */
            send_ping_response(client, &ping);
            break;

        case CWNET_PING_RESPONSE_2:
            /* Update latency measurement */
            {
                int32_t latency = cwnet_ping_calc_latency(&ping);
                if (latency >= 0) {
                    client->latency_ms = latency;
                }
            }
            break;

        case CWNET_PING_RESPONSE_1:
            /* Client shouldn't receive RESPONSE_1, ignore */
            break;
    }
}

/**
 * @brief Handle CW event frame (received from server/other operators)
 */
static void handle_cw_event(cwnet_client_t *client,
                            bool key_down,
                            const uint8_t *payload,
                            size_t len) {
    if (client->cw_event_cb == NULL) {
        return;  /* No callback registered */
    }

    /* Decode little-endian 32-bit timestamp */
    int32_t timestamp = 0;
    if (len >= 4) {
        timestamp = (int32_t)((uint32_t)payload[0] |
                              ((uint32_t)payload[1] << 8) |
                              ((uint32_t)payload[2] << 16) |
                              ((uint32_t)payload[3] << 24));
    }

    client->cw_event_cb(key_down, timestamp, client->user_data);
}

/**
 * @brief Process a complete frame from parse result
 */
static void process_frame(cwnet_client_t *client, const cwnet_parse_result_t *result) {
    uint8_t cmd = result->command;
    const uint8_t *payload = result->payload;
    size_t payload_len = result->payload_len;

    switch (cmd) {
        case CWNET_CMD_WELCOME:
            handle_welcome(client);
            break;

        case CWNET_CMD_PING:
            handle_ping(client, payload, payload_len);
            break;

        case CWNET_CMD_CW_DOWN:
            handle_cw_event(client, true, payload, payload_len);
            break;

        case CWNET_CMD_CW_UP:
            handle_cw_event(client, false, payload, payload_len);
            break;

        default:
            /* Unknown command, ignore */
            break;
    }
}

/*===========================================================================*/
/* Public API                                                                */
/*===========================================================================*/

cwnet_client_err_t cwnet_client_init(cwnet_client_t *client,
                                      const cwnet_client_config_t *config) {
    if (client == NULL || config == NULL) {
        return CWNET_CLIENT_ERR_INVALID_ARG;
    }

    /* Validate required fields */
    if (config->server_host == NULL || config->server_host[0] == '\0') {
        return CWNET_CLIENT_ERR_INVALID_ARG;
    }
    if (config->send_cb == NULL || config->get_time_ms_cb == NULL) {
        return CWNET_CLIENT_ERR_INVALID_ARG;
    }

    /* Clear structure */
    memset(client, 0, sizeof(*client));

    /* Copy configuration */
    size_t host_len = strlen(config->server_host);
    if (host_len >= CWNET_MAX_HOST_LEN) {
        host_len = CWNET_MAX_HOST_LEN - 1;
    }
    memcpy(client->server_host, config->server_host, host_len);
    client->server_host[host_len] = '\0';

    client->server_port = config->server_port;

    if (config->username != NULL) {
        size_t user_len = strlen(config->username);
        if (user_len >= CWNET_MAX_USERNAME_LEN) {
            user_len = CWNET_MAX_USERNAME_LEN - 1;
        }
        memcpy(client->username, config->username, user_len);
        client->username[user_len] = '\0';
    }

    /* Set callbacks */
    client->send_cb = config->send_cb;
    client->get_time_ms_cb = config->get_time_ms_cb;
    client->state_change_cb = config->state_change_cb;
    client->cw_event_cb = config->cw_event_cb;
    client->user_data = config->user_data;

    /* Initialize state */
    client->state = CWNET_STATE_DISCONNECTED;
    client->latency_ms = -1;

    /* Initialize timer */
    cwnet_timer_init(&client->timer);

    /* Initialize frame parser */
    cwnet_frame_parser_init(&client->parser);

    return CWNET_CLIENT_OK;
}

cwnet_client_state_t cwnet_client_get_state(const cwnet_client_t *client) {
    if (client == NULL) {
        return CWNET_STATE_DISCONNECTED;
    }
    return client->state;
}

int32_t cwnet_client_get_synced_time(const cwnet_client_t *client) {
    if (client == NULL) {
        return 0;
    }
    int32_t local_time = 0;
    if (client->get_time_ms_cb != NULL) {
        /* Need non-const access to call callback, but it doesn't modify client */
        local_time = client->get_time_ms_cb(client->user_data);
    }
    return cwnet_timer_read_synced_ms(&client->timer, local_time);
}

int32_t cwnet_client_get_latency_ms(const cwnet_client_t *client) {
    if (client == NULL) {
        return -1;
    }
    return client->latency_ms;
}

void cwnet_client_on_connected(cwnet_client_t *client) {
    if (client == NULL) {
        return;
    }

    /* Reset parser for new connection */
    cwnet_frame_parser_reset(&client->parser);

    /* Transition to CONNECTING */
    set_state(client, CWNET_STATE_CONNECTING);

    /* Send IDENT frame */
    send_ident(client);
}

void cwnet_client_on_disconnected(cwnet_client_t *client) {
    if (client == NULL) {
        return;
    }

    /* Reset parser */
    cwnet_frame_parser_reset(&client->parser);

    /* Transition to DISCONNECTED */
    set_state(client, CWNET_STATE_DISCONNECTED);
}

void cwnet_client_on_data(cwnet_client_t *client,
                           const uint8_t *data,
                           size_t len) {
    if (client == NULL || data == NULL || len == 0) {
        return;
    }

    /* Feed data to parser */
    cwnet_parse_result_t result = cwnet_frame_parse(&client->parser, data, len);

    switch (result.status) {
        case CWNET_PARSE_OK:
            /* Frame complete, process it */
            process_frame(client, &result);
            /* Reset parser for next frame */
            cwnet_frame_parser_reset(&client->parser);
            break;

        case CWNET_PARSE_NEED_MORE:
            /* Waiting for more data, nothing to do */
            break;

        case CWNET_PARSE_ERROR:
            /* Parse error, reset and try to recover */
            cwnet_frame_parser_reset(&client->parser);
            break;
    }
}

cwnet_client_err_t cwnet_client_send_key_event(cwnet_client_t *client,
                                                bool key_down) {
    if (client == NULL) {
        return CWNET_CLIENT_ERR_INVALID_ARG;
    }

    if (client->state != CWNET_STATE_READY) {
        return CWNET_CLIENT_ERR_NOT_READY;
    }

    return send_cw_event(client, key_down);
}
