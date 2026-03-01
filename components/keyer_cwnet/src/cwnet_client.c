/**
 * @file cwnet_client.c
 * @brief CWNet TCP client state machine implementation
 */

#include "cwnet_client.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

/* Diagnostic logging - uses RT-safe logging from bg task context */
#include "rt_log.h"
#include "esp_timer.h"
extern log_stream_t g_bg_log_stream;

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
 * @brief Build command byte from category and command
 */
static inline uint8_t make_cmd_byte(cwnet_frame_category_t cat, cwnet_cmd_t cmd) {
    return (uint8_t)((cat << 6) | (cmd & 0x3F));
}

/**
 * @brief Build and send CONNECT frame
 *
 * CONNECT frame format (94 bytes total):
 *   - cmd byte: 0x41 (short block, CONNECT command)
 *   - length: 92 (0x5C)
 *   - payload[0-43]: username (44 bytes, null-padded)
 *   - payload[44-87]: callsign (44 bytes, null-padded)
 *   - payload[88-91]: permissions (4 bytes, uint32 LE)
 */
static cwnet_client_err_t send_connect(cwnet_client_t *client) {
    /* Frame: cmd(1) + len(1) + payload(92) = 94 bytes */
    uint8_t frame[2 + CWNET_CONNECT_PAYLOAD_LEN];
    memset(frame, 0, sizeof(frame));

    /* Command byte: CONNECT with short payload */
    frame[0] = make_cmd_byte(CWNET_FRAME_CAT_SHORT_PAYLOAD, CWNET_CMD_CONNECT);
    frame[1] = CWNET_CONNECT_PAYLOAD_LEN;  /* 92 */

    /* Username field (44 bytes, null-terminated, zero-padded) */
    _Static_assert(sizeof(((cwnet_client_t *)0)->username) <= CWNET_CONNECT_USERNAME_LEN,
                   "username buffer must fit in connect frame field");
    size_t username_len = strlen(client->username);
    memcpy(&frame[2], client->username, username_len);

    /* Callsign field (44 bytes) - use same as username for now */
    memcpy(&frame[2 + CWNET_CONNECT_USERNAME_LEN], client->username, username_len);

    /* Permissions field (4 bytes) - leave as zero */

    int64_t now_us = esp_timer_get_time();
    RT_DEBUG(&g_bg_log_stream, now_us,
             "CONNECT: cmd=0x%02X user=\"%s\"",
             frame[0], client->username);

    int sent = send_frame(client, frame, sizeof(frame));
    if (sent < 0 || (size_t)sent != sizeof(frame)) {
        RT_ERROR(&g_bg_log_stream, now_us, "CONNECT send failed: %d", sent);
        return CWNET_CLIENT_ERR_SEND_FAILED;
    }

    RT_DEBUG(&g_bg_log_stream, now_us, "CONNECT sent: %d bytes", sent);
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

    /* Frame: cmd(1) + len(1) + payload(16) - short block */
    uint8_t frame[2 + CWNET_PING_PAYLOAD_SIZE];
    frame[0] = make_cmd_byte(CWNET_FRAME_CAT_SHORT_PAYLOAD, CWNET_CMD_PING);
    frame[1] = CWNET_PING_PAYLOAD_SIZE;
    memcpy(&frame[2], payload, CWNET_PING_PAYLOAD_SIZE);

    int sent = send_frame(client, frame, sizeof(frame));
    if (sent < 0 || (size_t)sent != sizeof(frame)) {
        int64_t now_us = esp_timer_get_time();
        RT_ERROR(&g_bg_log_stream, now_us, "PING RSP1 send failed: %d", sent);
        return CWNET_CLIENT_ERR_SEND_FAILED;
    }

    int64_t now_us = esp_timer_get_time();
    RT_DEBUG(&g_bg_log_stream, now_us,
             "PING RSP1 sent: id=%u t0=%" PRId32 " t1=%" PRId32,
             request->id, request->t0_ms, our_time);

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
    frame[0] = make_cmd_byte(CWNET_FRAME_CAT_SHORT_PAYLOAD, cmd);
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
        int64_t now_us = esp_timer_get_time();
        RT_WARN(&g_bg_log_stream, now_us, "CWNet PING: parse failed (len=%zu)", len);
        return;  /* Invalid PING, ignore */
    }

    int64_t now_us = esp_timer_get_time();

    switch (ping.type) {
        case CWNET_PING_REQUEST:
            /* Sync timer to server time BEFORE responding */
            {
                int32_t local_time = get_local_time(client);
                int64_t old_offset = client->timer.offset_ms;

                cwnet_timer_sync_to_server(&client->timer, ping.t0_ms, local_time);

                int64_t new_offset = client->timer.offset_ms;
                int32_t synced_time = cwnet_timer_read_synced_ms(&client->timer, local_time);

                RT_DEBUG(&g_bg_log_stream, now_us,
                         "PING sync: offset=%" PRId64 " delta=%" PRId64 " synced=%" PRId32,
                         new_offset, new_offset - old_offset, synced_time);
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
                    RT_DEBUG(&g_bg_log_stream, now_us, "RTT=%" PRId32 "ms", latency);
                }
            }
            break;

        case CWNET_PING_RESPONSE_1:
            /* Client shouldn't receive RESPONSE_1, ignore */
            RT_DEBUG(&g_bg_log_stream, now_us, "PING RSP1: unexpected (id=%u)", ping.id);
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

    /* Send CONNECT frame */
    send_connect(client);
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

    int64_t now_us = esp_timer_get_time();
    (void)now_us;  /* Used by RT_* macros below */

    /* Process all frames in buffer */
    size_t offset = 0;
    while (offset < len) {
        /* Feed remaining data to parser */
        cwnet_parse_result_t result = cwnet_frame_parse(&client->parser,
                                                         data + offset,
                                                         len - offset);

        switch (result.status) {
            case CWNET_PARSE_OK:
                /* Frame complete, process it */
                RT_DEBUG(&g_bg_log_stream, now_us,
                         "FRAME: cmd=0x%02X len=%u", result.command, result.payload_len);
                process_frame(client, &result);
                cwnet_frame_parser_reset(&client->parser);
                offset += result.bytes_consumed;
                break;

            case CWNET_PARSE_NEED_MORE:
                /* Waiting for more data, exit loop */
                return;

            case CWNET_PARSE_ERROR:
                /* Parse error - skip one byte to try to resync */
                cwnet_frame_parser_reset(&client->parser);
                offset++;  /* Skip one byte and try again */
                break;
        }
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
