/**
 * @file cwnet_client.c
 * @brief CWNet TCP client state machine implementation
 */

#include "cwnet_client.h"
#include "cwnet_timestamp.h"
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
 * @brief Build and send one MORSE frame for a key transition
 *
 * MORSE frame format (short payload):
 *   - cmd byte: 0x50 = (CAT_SHORT << 6) | MORSE
 *   - length: number of keying bytes
 *   - payload: keying bytes, bit 7 = key state, bits 6..0 = 7-bit wait
 *
 * A wait above CWSTREAM_MAX_WAIT_MS becomes several bytes with the same key
 * state, exactly as CwStream_EncodeKeyUpDownEvent() splits it: full bytes of
 * 1165 ms, then the remainder. The reference queues them in its FIFO and the
 * network poll drains the whole FIFO into one frame, so they travel together.
 *
 * @param encoded_ms Out: sum of the decoded waits actually put on the wire.
 *                   This is what the sender's stopwatch advances by.
 */
static cwnet_client_err_t send_morse(cwnet_client_t *client,
                                     bool key_down,
                                     int32_t wait_ms,
                                     int32_t *encoded_ms) {
    uint8_t frame[2 + CWNET_MORSE_MAX_EVENTS];
    size_t n = 0;
    int32_t remaining = wait_ms > 0 ? wait_ms : 0;
    int32_t total = 0;

    /* The reference's do-while: always at least one byte, even for wait 0 */
    do {
        int32_t chunk = remaining > CWSTREAM_MAX_WAIT_MS ? CWSTREAM_MAX_WAIT_MS : remaining;
        uint8_t b = cwstream_encode_timestamp((int)chunk);
        if (key_down) {
            b |= 0x80u;
        }
        frame[2 + n] = b;
        n++;
        total += cwstream_decode_timestamp(b);
        remaining -= chunk;
    } while (remaining > 0 && n < CWNET_MORSE_MAX_EVENTS);

    frame[0] = make_cmd_byte(CWNET_FRAME_CAT_SHORT_PAYLOAD, CWNET_CMD_MORSE);
    frame[1] = (uint8_t)n;

    size_t frame_len = 2 + n;
    int sent = send_frame(client, frame, frame_len);
    if (sent < 0 || (size_t)sent != frame_len) {
        return CWNET_CLIENT_ERR_SEND_FAILED;
    }

    *encoded_ms = total;
    return CWNET_CLIENT_OK;
}

/*===========================================================================*/
/* Frame Handlers                                                            */
/*===========================================================================*/

/**
 * @brief Handle the server's CONNECT echo, which confirms the connection
 */
static void handle_connect_echo(cwnet_client_t *client,
                                const uint8_t *payload,
                                size_t len) {
    /* The server returns the record we sent with the permissions field filled
     * in; that field is how we learn what this connection is allowed to do. */
    if (len >= CWNET_CONNECT_PAYLOAD_LEN) {
        const uint8_t *p = &payload[CWNET_CONNECT_PERMISSIONS_OFFSET];
        client->permissions = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                              ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }

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
                    /* CwNet.c:1442-1447: jump to a new peak, otherwise drop
                     * by a tenth of the gap. Integer division: a gap under
                     * 10 ms no longer descends, and that is what the
                     * reference shows. */
                    if (client->latency_peak_ms < 0 || latency >= client->latency_peak_ms) {
                        client->latency_peak_ms = latency;
                    } else {
                        client->latency_peak_ms -= (client->latency_peak_ms - latency) / 10;
                    }
                    RT_DEBUG(&g_bg_log_stream, now_us, "RTT=%" PRId32 "ms pk=%" PRId32 "ms",
                             latency, client->latency_peak_ms);
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
 * @brief Send a rig-control string in a 0x06 frame, trailing NUL included
 *
 * CwNet.c:1000-1007: the payload is the C string with its terminator.
 */
static cwnet_client_err_t send_rig_string(cwnet_client_t *client, const char *text) {
    size_t len = strlen(text) + 1;
    uint8_t frame[2 + 32];
    if (len > sizeof(frame) - 2) {
        return CWNET_CLIENT_ERR_INVALID_ARG;
    }
    frame[0] = make_cmd_byte(CWNET_FRAME_CAT_SHORT_PAYLOAD, CWNET_CMD_RIG_STRING);
    frame[1] = (uint8_t)len;
    memcpy(&frame[2], text, len);
    int sent = send_frame(client, frame, 2 + len);
    if (sent < 0 || (size_t)sent != 2 + len) {
        return CWNET_CLIENT_ERR_SEND_FAILED;
    }
    return CWNET_CLIENT_OK;
}

/**
 * @brief The server's "RPRT n" answer to a rig-control string
 */
static void handle_rig_string(cwnet_client_t *client, const uint8_t *payload, size_t len) {
    /* "RPRT " then a signed decimal, then "\n\0" */
    if (len < 6 || memcmp(payload, "RPRT ", 5) != 0) {
        return;
    }
    size_t i = 5;
    bool negative = false;
    if (i < len && payload[i] == '-') {
        negative = true;
        i++;
    }
    if (i >= len || payload[i] < '0' || payload[i] > '9') {
        return;
    }
    int32_t value = 0;
    while (i < len && payload[i] >= '0' && payload[i] <= '9') {
        value = value * 10 + (payload[i] - '0');
        i++;
    }
    client->rig_result = negative ? -value : value;
}

/**
 * @brief Queue every byte of a MORSE frame as a received keying event
 *
 * The reference does this per byte as it parses (CwNet.c:2875-2902,
 * MorseRxFifo), stamping each with its time of reception for the latency
 * control that plays it back. A full FIFO drops the byte and counts it;
 * the reference overwrites silently.
 */
static void handle_morse(cwnet_client_t *client, const uint8_t *payload, size_t len) {
    int32_t now_ms = get_local_time(client);
    for (size_t i = 0; i < len; i++) {
        if (client->rx.count >= CWNET_RX_FIFO_SIZE) {
            client->rx_dropped++;
            continue;
        }
        uint16_t head = (uint16_t)((client->rx.tail + client->rx.count) % CWNET_RX_FIFO_SIZE);
        client->rx.cmd[head] = payload[i];
        client->rx.received_at_ms[head] = now_ms;
        client->rx.count++;
    }
}

/**
 * @brief Process a complete frame from parse result
 *
 * CI_V 0x14 and SPECTRUM 0x15 are rig control and display data, never keying.
 */
static void process_frame(cwnet_client_t *client, const cwnet_parse_result_t *result) {
    uint8_t cmd = result->command;
    const uint8_t *payload = result->payload;
    size_t payload_len = result->payload_len;

    switch (cmd) {
        case CWNET_CMD_CONNECT:
            handle_connect_echo(client, payload, payload_len);
            break;

        case CWNET_CMD_PING:
            handle_ping(client, payload, payload_len);
            break;

        case CWNET_CMD_MORSE:
            handle_morse(client, payload, payload_len);
            break;

        case CWNET_CMD_RIG_STRING:
            handle_rig_string(client, payload, payload_len);
            break;

        default:
            /* Not handled, ignore */
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
    client->user_data = config->user_data;

    /* Initialize state */
    client->state = CWNET_STATE_DISCONNECTED;
    client->latency_ms = -1;
    client->latency_peak_ms = -1;
    client->rig_result = CWNET_RIG_RESULT_NONE;

    /* Initialize timer */
    cwnet_timer_init(&client->timer);

    /* Initialize frame parser */
    cwnet_frame_parser_init(&client->parser);

    return CWNET_CLIENT_OK;
}

uint32_t cwnet_client_get_permissions(const cwnet_client_t *client) {
    if (client == NULL) {
        return 0;
    }
    return client->permissions;
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

    /* A new session starts a new over: first transition waits 0, PTT off */
    client->tx_ref_ms = 0;
    client->tx_filling = false;
    client->tx_key_down = false;
    client->tx_last_edge_ms = 0;
    client->ptt_on = false;

    /* Nothing received yet, and no latency known: the reference keeps both
     * across a reconnect, which is stale data with a fresh server. */
    memset(&client->rx, 0, sizeof(client->rx));
    client->rx_dropped = 0;
    client->latency_ms = -1;
    client->latency_peak_ms = -1;
    client->rig_result = CWNET_RIG_RESULT_NONE;

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
                                                bool key_down,
                                                int32_t at_ms) {
    if (client == NULL) {
        return CWNET_CLIENT_ERR_INVALID_ARG;
    }

    if (client->state != CWNET_STATE_READY) {
        return CWNET_CLIENT_ERR_NOT_READY;
    }

    /* A server that did not grant TRANSMIT discards our keying without saying
     * so, which would leave the operator listening to their own sidetone and
     * believing they were on the air. Refuse instead. */
    if ((client->permissions & CWNET_PERMISSION_TRANSMIT) == 0) {
        return CWNET_CLIENT_ERR_NOT_PERMITTED;
    }

    /* KeyerThread.c encodes only when fMorseOutput differs from what was sent */
    if (key_down == client->tx_key_down) {
        return CWNET_CLIENT_OK;
    }

    /* Wait since the previous transition. At the start of an over there is
     * no previous transition: the wait is 0 and the stopwatch starts here. */
    bool opening = !client->tx_filling;
    int32_t wait_ms = opening ? 0 : (int32_t)((uint32_t)at_ms - client->tx_ref_ms);

    int32_t encoded_ms = 0;
    cwnet_client_err_t err = send_morse(client, key_down, wait_ms, &encoded_ms);
    if (err != CWNET_CLIENT_OK) {
        return err;
    }

    /* Advance by what the receiver will actually wait, not by what we
     * measured: the quantisation residual carries into the next interval
     * (TIM_AdjustStopwatch_ms in KeyerThread.c). The reference never resets
     * this state on reconnect; we do, in on_connected(), so a session cannot
     * open with a stale wait. */
    if (opening) {
        client->tx_ref_ms = (uint32_t)at_ms;
        client->tx_filling = true;
    }
    if (wait_ms > CWNET_MORSE_MAX_EVENTS * CWSTREAM_MAX_WAIT_MS) {
        /* One frame could not carry the whole wait: that wait is short on
         * the wire. Re-base on this edge so the next one is not late by
         * the part that was lost. */
        client->tx_ref_ms = (uint32_t)at_ms;
    } else {
        client->tx_ref_ms += (uint32_t)encoded_ms;
    }
    client->tx_key_down = key_down;
    client->tx_last_edge_ms = (uint32_t)at_ms;

    /* PTT rises with a key-down, right after its MORSE byte, as the
     * reference's network poll orders them (CwNet.c:2616 then :2648). */
    if (key_down && client->ptt_tail_ms > 0 && !client->ptt_on) {
        client->ptt_on = true;
        return send_rig_string(client, "set_ptt 1\n");
    }
    return CWNET_CLIENT_OK;
}

bool cwnet_client_abort_over(cwnet_client_t *client) {
    if (client == NULL) {
        return false;
    }

    if (client->state == CWNET_STATE_READY) {
        int32_t encoded_ms = 0;
        if (client->tx_key_down) {
            /* Key up, now. If it does not go out the key is still down on
             * the wire: say so, the caller comes back. */
            if (send_morse(client, false, 0, &encoded_ms) != CWNET_CLIENT_OK) {
                return false;
            }
            client->tx_key_down = false;
        }
        if (client->tx_filling) {
            /* Second key-up: the end of the over */
            if (send_morse(client, false, 0, &encoded_ms) != CWNET_CLIENT_OK) {
                return false;
            }
            client->tx_filling = false;
        }
        if (client->ptt_on) {
            if (send_rig_string(client, "set_ptt 0\n") != CWNET_CLIENT_OK) {
                return false;
            }
            client->ptt_on = false;
        }
    }
    /* Not READY: there is no session, so nothing is on the wire */

    client->tx_ref_ms = 0;
    client->tx_filling = false;
    client->tx_key_down = false;
    client->ptt_on = false;
    return true;
}

void cwnet_client_set_ptt_tail_ms(cwnet_client_t *client, int32_t tail_ms) {
    if (client != NULL) {
        client->ptt_tail_ms = tail_ms > 0 ? tail_ms : 0;
    }
}

bool cwnet_client_ptt_on_wire(const cwnet_client_t *client) {
    return client != NULL && client->ptt_on;
}

int32_t cwnet_client_get_rig_result(const cwnet_client_t *client) {
    return client == NULL ? CWNET_RIG_RESULT_NONE : client->rig_result;
}

bool cwnet_client_key_on_wire(const cwnet_client_t *client) {
    return client != NULL && client->tx_key_down;
}

int32_t cwnet_client_get_latency_peak_ms(const cwnet_client_t *client) {
    if (client == NULL) {
        return -1;
    }
    return client->latency_peak_ms;
}

/*===========================================================================*/
/* Received keying                                                           */
/*===========================================================================*/

bool cwnet_client_rx_pop(cwnet_client_t *client, cwnet_rx_event_t *out) {
    if (client == NULL || out == NULL || client->rx.count == 0) {
        return false;
    }
    uint8_t b = client->rx.cmd[client->rx.tail];
    out->key_down = (b & 0x80u) != 0;
    out->wait_ms = cwstream_decode_timestamp(b);
    out->received_at_ms = client->rx.received_at_ms[client->rx.tail];
    client->rx.tail = (uint16_t)((client->rx.tail + 1) % CWNET_RX_FIFO_SIZE);
    client->rx.count--;
    return true;
}

size_t cwnet_client_rx_count(const cwnet_client_t *client) {
    return client == NULL ? 0 : client->rx.count;
}

int32_t cwnet_client_rx_buffered_ms(const cwnet_client_t *client) {
    if (client == NULL) {
        return 0;
    }
    int32_t total = 0;
    for (uint16_t i = 0; i < client->rx.count; i++) {
        uint16_t idx = (uint16_t)((client->rx.tail + i) % CWNET_RX_FIFO_SIZE);
        total += cwstream_decode_timestamp(client->rx.cmd[idx]);
    }
    return total;
}

bool cwnet_client_rx_has_end_of_over(const cwnet_client_t *client) {
    if (client == NULL) {
        return false;
    }
    /* CwStreamEnc.c: two consecutive key-up commands, regardless of the
     * seven-bit time, indicate END-OF-TRANSMISSION */
    for (uint16_t i = 1; i < client->rx.count; i++) {
        uint16_t prev = (uint16_t)((client->rx.tail + i - 1) % CWNET_RX_FIFO_SIZE);
        uint16_t cur = (uint16_t)((client->rx.tail + i) % CWNET_RX_FIFO_SIZE);
        if ((client->rx.cmd[prev] & 0x80u) == 0 && (client->rx.cmd[cur] & 0x80u) == 0) {
            return true;
        }
    }
    return false;
}

uint32_t cwnet_client_rx_dropped(const cwnet_client_t *client) {
    return client == NULL ? 0 : client->rx_dropped;
}

bool cwnet_client_over_open(const cwnet_client_t *client) {
    return client != NULL && client->tx_filling;
}

bool cwnet_client_poll(cwnet_client_t *client, int32_t now_ms, int32_t dot_ms) {
    if (client == NULL || client->state != CWNET_STATE_READY) {
        return false;
    }
    if (client->tx_key_down) {
        return false;
    }

    /* PTT drops once the key has been up for the box's tail, whether or
     * not the over is still open: below about 33 WPM it comes before the
     * end of the over, above it after, as in the reference. */
    if (client->ptt_on &&
        (int32_t)((uint32_t)now_ms - client->tx_last_edge_ms) >= client->ptt_tail_ms) {
        if (send_rig_string(client, "set_ptt 0\n") != CWNET_CLIENT_OK) {
            return false;
        }
        client->ptt_on = false;
    }

    if (!client->tx_filling) {
        return false;
    }

    /* KeyerThread.c: t_us > 14000 * iDotTime_ms. Which 16 ms bucket the
     * elapsed time falls in depends on the poll instant there too. */
    int32_t elapsed_ms = (int32_t)((uint32_t)now_ms - client->tx_ref_ms);
    if (elapsed_ms <= 14 * dot_ms) {
        return false;
    }

    int32_t encoded_ms = 0;
    if (send_morse(client, false, elapsed_ms, &encoded_ms) != CWNET_CLIENT_OK) {
        return false;
    }

    client->tx_ref_ms += (uint32_t)encoded_ms;
    client->tx_filling = false;  /* the next transition opens a new over with wait 0 */
    return true;
}
