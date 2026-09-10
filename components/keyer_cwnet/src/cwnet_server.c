/**
 * @file cwnet_server.c
 * @brief The station server's core. Rules and reasons in cwnet_server.h.
 *
 * Nothing here reads a clock, touches a socket or writes a log (KTD1,
 * KTD10): the instant comes in as an argument, the bytes go out through
 * the send callback, and what happened comes back in the result.
 *
 * The peer's bytes are never C strings. The CONNECT's two fields are
 * copied for their fixed 44 bytes and terminated here; a rig-control
 * string must carry its NUL inside its payload or the client is closed;
 * a PING goes through cwnet_ping_parse(), which refuses a short payload.
 */

#include "cwnet_server.h"

#include "cwnet_client.h"   /* command codes, CONNECT layout, permission bits */

#include <stdio.h>
#include <string.h>

/** Frame the server builds: the CONNECT echo is the longest at 2 + 92 */
#define TX_FRAME_MAX (2u + 128u)

/** Longest rig-control string we look at; the rest cannot match a command */
#define RIG_STRING_MAX 128u

/** What the reference announces when nobody has the key (CwNet.c:449) */
#define NOBODY_NAME "-- nobody --"

/** The index byte of a TX_INFO when nobody has the key: -1 as a byte */
#define NOBODY_INDEX_BYTE 0xFFu

/** The 31-bit range the reference puts on the wire (CwNet.c:2254) */
#define WIRE_CLOCK_MASK 0x7FFFFFFF

/** Bound on how many times one call drives the playback engine */
#define PLAY_GUARD 64

/*===========================================================================*/
/* Events                                                                    */
/*===========================================================================*/

static void emit(cwnet_server_result_t *out, cwnet_server_event_type_t type,
                 int client_idx, int32_t value, int32_t peak_ms, int64_t at_ms) {
    if (out == NULL) {
        return;
    }
    if (out->count >= CWNET_SERVER_MAX_EVENTS) {
        out->dropped++;
        return;
    }
    cwnet_server_event_t *ev = &out->ev[out->count++];
    ev->type = type;
    ev->client_idx = client_idx;
    ev->value = value;
    ev->peak_ms = peak_ms;
    ev->at_ms = at_ms;
}

static void result_clear(cwnet_server_result_t *out) {
    if (out != NULL) {
        out->count = 0;
        out->dropped = 0;
    }
}

/*===========================================================================*/
/* Slots                                                                     */
/*===========================================================================*/

static bool index_in_range(const cwnet_server_t *srv, int client_idx) {
    return client_idx >= CWNET_SERVER_FIRST_CLIENT &&
           client_idx < CWNET_SERVER_FIRST_CLIENT + (int)srv->cfg.max_clients;
}

static cwnet_server_client_t *slot(cwnet_server_t *srv, int client_idx) {
    if (srv == NULL || !index_in_range(srv, client_idx)) {
        return NULL;
    }
    cwnet_server_client_t *c = &srv->client[client_idx - CWNET_SERVER_FIRST_CLIENT];
    return c->in_use ? c : NULL;
}

static const cwnet_server_client_t *slot_const(const cwnet_server_t *srv, int client_idx) {
    if (srv == NULL || !index_in_range(srv, client_idx)) {
        return NULL;
    }
    const cwnet_server_client_t *c = &srv->client[client_idx - CWNET_SERVER_FIRST_CLIENT];
    return c->in_use ? c : NULL;
}

/*===========================================================================*/
/* Sending                                                                   */
/*===========================================================================*/

static void close_client(cwnet_server_t *srv, int client_idx,
                         cwnet_server_close_reason_t reason,
                         int64_t now_ms, cwnet_server_result_t *out);

/**
 * @brief Put one frame on a client's wire
 *
 * A short write breaks the framing, so it counts as a failure and the
 * caller closes that client.
 *
 * @return false when the send callback did not take the whole frame
 */
static bool send_frame(cwnet_server_t *srv, int client_idx, uint8_t cmd,
                       const uint8_t *payload, size_t payload_len) {
    uint8_t frame[TX_FRAME_MAX];
    size_t frame_len = 0;
    if (!cwnet_frame_build(cmd, payload, payload_len, frame, sizeof(frame), &frame_len)) {
        return false;
    }
    if (srv->cfg.send_cb == NULL) {
        return false;
    }
    int sent = srv->cfg.send_cb(client_idx, frame, frame_len, srv->cfg.user_data);
    return sent >= 0 && (size_t)sent == frame_len;
}

/**
 * @brief A frame, and the client closed if it could not go out
 */
static bool send_or_close(cwnet_server_t *srv, int client_idx, uint8_t cmd,
                          const uint8_t *payload, size_t payload_len,
                          int64_t now_ms, cwnet_server_result_t *out) {
    if (send_frame(srv, client_idx, cmd, payload, payload_len)) {
        return true;
    }
    close_client(srv, client_idx, CWNET_SERVER_CLOSE_SEND_FAILED, now_ms, out);
    return false;
}

/**
 * @brief "Text the receiver should print somewhere", trailing NUL included
 *
 * CwNet.c:749-756: CwNet_SendTextToPrint() sends strlen + 1.
 */
static void send_print(cwnet_server_t *srv, int client_idx, const char *text,
                       int64_t now_ms, cwnet_server_result_t *out) {
    size_t len = strlen(text) + 1u;
    if (len > TX_FRAME_MAX - 2u) {
        return;
    }
    (void)send_or_close(srv, client_idx, (uint8_t)CWNET_SERVER_CMD_PRINT,
                        (const uint8_t *)text, len, now_ms, out);
}

/*===========================================================================*/
/* Who has the key: the TX_INFO                                              */
/*===========================================================================*/

/**
 * @brief The name a client is announced under
 *
 * The callsign it sent, or "NoCall #n" when it sent none: the reference's
 * CwNet_GetClientCallOrInfo() (CwNet.c:432-452), which never reveals a
 * user name to the other clients.
 */
static const char *announced_name(const cwnet_server_t *srv, int client_idx) {
    const cwnet_server_client_t *c = slot_const(srv, client_idx);
    return (c != NULL && c->name[0] != '\0') ? c->name : NOBODY_NAME;
}

/**
 * @brief The TX_INFO payload for the current holder: index byte, name, NUL
 */
static size_t build_tx_info(const cwnet_server_t *srv, uint8_t *payload, size_t size) {
    const char *name = NOBODY_NAME;
    uint8_t index_byte = (uint8_t)NOBODY_INDEX_BYTE;
    if (srv->key_holder != CWNET_SERVER_NOBODY) {
        index_byte = (uint8_t)srv->key_holder;
        name = announced_name(srv, srv->key_holder);
    }
    size_t name_len = strlen(name) + 1u;   /* the NUL goes on the wire */
    if (name_len + 1u > size) {
        name_len = size - 1u;
    }
    payload[0] = index_byte;
    memcpy(payload + 1, name, name_len);
    payload[name_len] = 0u;   /* a truncated name still ends where it says */
    return name_len + 1u;
}

/**
 * @brief Tell every confirmed client who has the key now
 *
 * The payload is built inside the loop, once per client, because the loop
 * can change what it has to say. A send that fails closes the client it was
 * writing to, and take_key() sets the holder BEFORE announcing it, so that
 * client may well be the holder: closing it releases the key and announces
 * the release from in here. One payload built ahead of the loop would then
 * go on being sent, after the fact, to every client the loop had not
 * reached yet — and each of those would keep a holder the server no longer
 * has, with nothing further coming to correct it. Each client is told the
 * state as it stands when its own frame leaves.
 */
static void announce_key(cwnet_server_t *srv, int64_t now_ms, cwnet_server_result_t *out) {
    emit(out, CWNET_SERVER_EV_KEY_HOLDER, srv->key_holder, 0, 0, now_ms);

    for (int i = 0; i < (int)srv->cfg.max_clients; i++) {
        int idx = CWNET_SERVER_FIRST_CLIENT + i;
        cwnet_server_client_t *c = slot(srv, idx);
        if (c == NULL || !c->confirmed) {
            continue;
        }
        uint8_t payload[CWNET_SERVER_NAME_LEN + 1];
        size_t len = build_tx_info(srv, payload, sizeof(payload));
        (void)send_or_close(srv, idx, (uint8_t)CWNET_CMD_TX_INFO, payload, len, now_ms, out);
    }
}

/*===========================================================================*/
/* The playback engine                                                       */
/*===========================================================================*/

static void release_key(cwnet_server_t *srv, int64_t now_ms, cwnet_server_result_t *out) {
    if (srv->key_holder == CWNET_SERVER_NOBODY) {
        return;
    }
    srv->key_holder = CWNET_SERVER_NOBODY;
    announce_key(srv, now_ms, out);
}

static void map_play_events(cwnet_server_t *srv, const cwnet_play_result_t *pr,
                            cwnet_server_result_t *out) {
    for (size_t i = 0; i < pr->count; i++) {
        const cwnet_play_event_t *ev = &pr->ev[i];
        switch (ev->type) {
            case CWNET_PLAY_EV_KEY_DOWN:
                emit(out, CWNET_SERVER_EV_KEY_DOWN, srv->key_holder, 0, 0, ev->at_ms);
                break;
            case CWNET_PLAY_EV_KEY_UP:
                emit(out, CWNET_SERVER_EV_KEY_UP, srv->key_holder, 0, 0, ev->at_ms);
                break;
            case CWNET_PLAY_EV_PTT_ON:
                emit(out, CWNET_SERVER_EV_PTT_ON, srv->key_holder, 0, 0, ev->at_ms);
                break;
            case CWNET_PLAY_EV_PTT_OFF:
                emit(out, CWNET_SERVER_EV_PTT_OFF, srv->key_holder, 0, 0, ev->at_ms);
                break;
            case CWNET_PLAY_EV_LATE_BYTE:
                /* The engine counts; the caller is the only one that can
                 * say it out loud (KTD10). The numbers are the running
                 * totals, so two lines apart tell how fast the link is
                 * slipping, not just that it is. */
                emit(out, CWNET_SERVER_EV_LATE_BYTE, srv->key_holder,
                     (int32_t)cwnet_play_late_bytes(&srv->play),
                     (int32_t)cwnet_play_late_ms(&srv->play), ev->at_ms);
                break;
            case CWNET_PLAY_EV_OVER_FINISHED:
                /* The marker was played and the PTT is down: the key is free
                 * again (R6). Announcing is the last thing the over does. */
                release_key(srv, ev->at_ms, out);
                break;
            case CWNET_PLAY_EV_END_OF_OVER:
            default:
                break;
        }
    }
}

/**
 * @brief Let the engine do everything due at or before now_ms
 */
static void run_play(cwnet_server_t *srv, int64_t now_ms, cwnet_server_result_t *out) {
    for (int guard = 0; guard < PLAY_GUARD; guard++) {
        cwnet_play_result_t pr;
        cwnet_play_tick(&srv->play, now_ms, &pr);
        if (pr.count == 0u) {
            return;
        }
        map_play_events(srv, &pr, out);
    }
}

/**
 * @brief A safety net fired: key up now, PTT down at the tail, key free
 */
static void force_release(cwnet_server_t *srv, cwnet_server_fault_t why,
                          int64_t now_ms, cwnet_server_result_t *out) {
    /* Taken before the engine runs: a release with no PTT raised finishes
     * the over inside that call, which frees the key. The fault has to name
     * the client that lost it, not the emptiness left behind. */
    int holder = srv->key_holder;

    cwnet_play_result_t pr;
    cwnet_play_force_release(&srv->play, now_ms, &pr);
    map_play_events(srv, &pr, out);
    emit(out, CWNET_SERVER_EV_FAULT, holder, (int32_t)why, 0, now_ms);
    release_key(srv, now_ms, out);
}

/*===========================================================================*/
/* Closing a client                                                          */
/*===========================================================================*/

static void close_client(cwnet_server_t *srv, int client_idx,
                         cwnet_server_close_reason_t reason,
                         int64_t now_ms, cwnet_server_result_t *out) {
    cwnet_server_client_t *c = slot(srv, client_idx);
    if (c == NULL) {
        return;
    }
    bool had_key = (srv->key_holder == client_idx);

    memset(c, 0, sizeof(*c));
    c->in_use = false;

    emit(out, CWNET_SERVER_EV_CLIENT_CLOSED, client_idx, (int32_t)reason, 0, now_ms);

    if (had_key) {
        /* Its over dies with it: key up at once, PTT down at the tail (R6) */
        force_release(srv, CWNET_SERVER_FAULT_HOLDER_GONE, now_ms, out);
    }
}

/*===========================================================================*/
/* CONNECT                                                                   */
/*===========================================================================*/

/**
 * @brief Copy one fixed-width CONNECT field and terminate it here
 *
 * The peer's 44 bytes may carry no NUL at all: reading them as a C string
 * would run off the payload.
 */
static void copy_field(char *dst, size_t dst_size, const uint8_t *src, size_t field_len) {
    size_t n = (field_len < dst_size - 1u) ? field_len : dst_size - 1u;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void handle_connect(cwnet_server_t *srv, int client_idx,
                           const uint8_t *payload, size_t payload_len,
                           int64_t now_ms, cwnet_server_result_t *out) {
    cwnet_server_client_t *c = slot(srv, client_idx);
    if (c == NULL) {
        return;
    }
    if (payload_len != CWNET_CONNECT_PAYLOAD_LEN) {
        /* "Un CONNECT di lunghezza diversa chiude la connessione" (R2) */
        close_client(srv, client_idx, CWNET_SERVER_CLOSE_BAD_CONNECT, now_ms, out);
        return;
    }
    if (c->confirmed) {
        /* One log-in per connection. A second one would move a callsign
         * under an over in progress; the reference has no such case. */
        return;
    }

    copy_field(c->username, sizeof(c->username), payload, CWNET_CONNECT_USERNAME_LEN);
    copy_field(c->name, sizeof(c->name), payload + CWNET_CONNECT_USERNAME_LEN,
               CWNET_CONNECT_CALLSIGN_LEN);
    if (c->name[0] <= ' ') {
        /* No callsign: announced as the reference does, never as the user
         * name (CwNet.c:441-444) */
        (void)snprintf(c->name, sizeof(c->name), "NoCall #%d", client_idx);
    }

    /* The echo: the record back with the permissions filled in. Everybody
     * may talk, transmit and control the rig (KTD6). */
    uint8_t echo[CWNET_CONNECT_PAYLOAD_LEN];
    memcpy(echo, payload, CWNET_CONNECT_PAYLOAD_LEN);
    uint32_t permissions = CWNET_PERMISSION_TALK | CWNET_PERMISSION_TRANSMIT |
                           CWNET_PERMISSION_CTRL_RIG;
    echo[CWNET_CONNECT_PERMISSIONS_OFFSET + 0] = (uint8_t)(permissions & 0xFFu);
    echo[CWNET_CONNECT_PERMISSIONS_OFFSET + 1] = (uint8_t)((permissions >> 8) & 0xFFu);
    echo[CWNET_CONNECT_PERMISSIONS_OFFSET + 2] = (uint8_t)((permissions >> 16) & 0xFFu);
    echo[CWNET_CONNECT_PERMISSIONS_OFFSET + 3] = (uint8_t)((permissions >> 24) & 0xFFu);

    c->confirmed = true;
    c->next_ping_at_ms = now_ms + (int64_t)srv->cfg.ping_interval_ms;

    if (!send_or_close(srv, client_idx, (uint8_t)CWNET_CMD_CONNECT, echo, sizeof(echo),
                       now_ms, out)) {
        return;
    }

    char welcome[CWNET_SERVER_NAME_LEN + 64];
    (void)snprintf(welcome, sizeof(welcome),
                   "Welcome %s. You may talk, transmit and control the rig.",
                   c->username);
    send_print(srv, client_idx, welcome, now_ms, out);
    if (slot(srv, client_idx) == NULL) {
        return;
    }

    /* Then the state of the key as it stands (R2) */
    uint8_t tx_info[CWNET_SERVER_NAME_LEN + 1];
    size_t tx_info_len = build_tx_info(srv, tx_info, sizeof(tx_info));
    if (!send_or_close(srv, client_idx, (uint8_t)CWNET_CMD_TX_INFO, tx_info, tx_info_len,
                       now_ms, out)) {
        return;
    }

    emit(out, CWNET_SERVER_EV_CLIENT_READY, client_idx, 0, 0, now_ms);
}

/*===========================================================================*/
/* PING                                                                      */
/*===========================================================================*/

static int32_t wire_clock(int64_t now_ms) {
    return (int32_t)(uint32_t)((uint64_t)now_ms & (uint64_t)WIRE_CLOCK_MASK);
}

static void handle_ping(cwnet_server_t *srv, int client_idx,
                        const uint8_t *payload, size_t payload_len,
                        int64_t now_ms, cwnet_server_result_t *out) {
    cwnet_server_client_t *c = slot(srv, client_idx);
    cwnet_ping_t ping;
    if (c == NULL || !cwnet_ping_parse(&ping, payload, payload_len)) {
        return;   /* a wrong length is not a framing error: ignore it */
    }

    uint8_t buf[CWNET_PING_PAYLOAD_SIZE];

    if (ping.type == CWNET_PING_REQUEST) {
        /* The other end may ping us too; answer as the reference does */
        if (cwnet_ping_build_response(&ping, buf, sizeof(buf), wire_clock(now_ms))) {
            (void)send_or_close(srv, client_idx, (uint8_t)CWNET_CMD_PING, buf, sizeof(buf),
                                now_ms, out);
        }
        return;
    }
    if (ping.type != CWNET_PING_RESPONSE_1) {
        return;   /* RESPONSE_2 belongs to the initiator, and that is us */
    }

    /* Only the answer to THIS connection's pending request counts: the
     * peak-hold belongs to the connection, not to the id. */
    if (!c->ping_pending || ping.id != c->ping_id || ping.t0_ms != c->ping_t0_ms) {
        return;
    }

    int32_t t2 = wire_clock(now_ms);
    if (!cwnet_ping_build_response2(&ping, buf, sizeof(buf), t2)) {
        return;
    }
    if (!send_or_close(srv, client_idx, (uint8_t)CWNET_CMD_PING, buf, sizeof(buf),
                       now_ms, out)) {
        return;
    }

    c->ping_pending = false;
    c->ping_misses = 0u;
    /* An answer came back. Whether the sample survives the gate below is a
     * different question, and take_key() needs both answers apart. */
    c->ping_answered = true;

    int32_t latency = t2 - ping.t0_ms;
    if (cwnet_ping_peak_hold_update(&c->latency_peak_ms, latency)) {
        c->latency_ms = latency;
        if ((uint32_t)c->latency_peak_ms <= srv->cfg.buffer_ceiling_ms) {
            c->unfit_notified = false;   /* the link came back: say so again if it goes */
        }
        emit(out, CWNET_SERVER_EV_LATENCY, client_idx, latency, c->latency_peak_ms, now_ms);
    }
}

/*===========================================================================*/
/* Rig-control strings                                                       */
/*===========================================================================*/

static const char *skip_spaces(const char *p) {
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

/**
 * @brief The code a rig-control string is answered with
 *
 * Nothing is applied (R10): "set_ptt" is acknowledged and dropped, because
 * the PTT follows the keying that is played (KTD5). Everything else is a
 * function this daemon does not have.
 */
static int rig_result_for(const char *cmd) {
    /* The reference's parser does not need the "long command" backslash
     * either (CwNet.c:2015) */
    const char *p = skip_spaces(cmd);
    if (*p == '\\') {
        p++;
    }
    static const char set_ptt[] = "set_ptt";
    size_t n = sizeof(set_ptt) - 1u;
    if (strncmp(p, set_ptt, n) != 0) {
        return CWNET_SERVER_RPRT_NO_FUNC;
    }
    p += n;
    if (*p == '\0' || *p == '\n' || *p == '\r') {
        return CWNET_SERVER_RPRT_BAD_ARG;   /* the command, with no argument */
    }
    if (*p != ' ' && *p != '\t') {
        return CWNET_SERVER_RPRT_NO_FUNC;   /* "set_pttx", a different word */
    }
    p = skip_spaces(p);
    if (*p != '0' && *p != '1') {
        return CWNET_SERVER_RPRT_BAD_ARG;
    }
    p++;
    if (*p != '\0' && *p != '\n' && *p != '\r' && *p != ' ') {
        return CWNET_SERVER_RPRT_BAD_ARG;
    }
    return CWNET_SERVER_RPRT_OK;
}

static void handle_rig_string(cwnet_server_t *srv, int client_idx,
                              const uint8_t *payload, size_t payload_len,
                              int64_t now_ms, cwnet_server_result_t *out) {
    /* The NUL has to be inside the payload; without it there is no string
     * here, only bytes, and reading on would leave the buffer. */
    if (payload_len == 0u || memchr(payload, 0, payload_len) == NULL) {
        close_client(srv, client_idx, CWNET_SERVER_CLOSE_BAD_STRING, now_ms, out);
        return;
    }

    char cmd[RIG_STRING_MAX];
    size_t n = (payload_len < sizeof(cmd)) ? payload_len : sizeof(cmd) - 1u;
    memcpy(cmd, payload, n);
    cmd[n] = '\0';

    char resp[24];
    int len = snprintf(resp, sizeof(resp), "RPRT %d\n", rig_result_for(cmd));
    if (len <= 0) {
        return;
    }
    (void)send_or_close(srv, client_idx, (uint8_t)CWNET_CMD_RIG_STRING,
                        (const uint8_t *)resp, (size_t)len + 1u, now_ms, out);
}

/*===========================================================================*/
/* MORSE and the key                                                         */
/*===========================================================================*/

/**
 * @brief Try to give the free key to a client
 *
 * @return true when it took it. False means the link is not fit: it gets a
 *         PRINT saying so and nothing of its is played (R7).
 */
static bool take_key(cwnet_server_t *srv, int client_idx, int64_t now_ms,
                     cwnet_server_result_t *out) {
    cwnet_server_client_t *c = slot(srv, client_idx);
    if (c == NULL) {
        return false;
    }
    int32_t peak = c->latency_peak_ms;

    /* A peak of -1 has two meanings and only one of them is innocent. The
     * peak-hold takes a sample only if it lands inside the gate's window
     * (cwnet_ping.h); a link whose every answer is slower than that never
     * records one, so it would present itself exactly like a client that
     * has only just connected and play at the floor. That is the link the
     * ceiling exists for. Answered but never measurable is not fit. */
    bool never_measurable = (peak < 0) && c->ping_answered;

    if (never_measurable || (peak >= 0 && (uint32_t)peak > srv->cfg.buffer_ceiling_ms)) {
        /* Once per refusal, exactly as often as the PRINT. A refused client
         * does not stop sending, and one event per rejected byte fills the
         * result's array with the same line and costs the daemon the events
         * it needs. The flag is re-armed when a sample brings the peak back
         * under the ceiling, in handle_ping(). */
        if (!c->unfit_notified) {
            c->unfit_notified = true;
            emit(out, CWNET_SERVER_EV_LINK_UNFIT, client_idx, peak, peak, now_ms);
            char measured[96];
            const char *msg = "Link not fit to transmit: no PING answered inside "
                              "the measurement window";
            if (!never_measurable) {
                (void)snprintf(measured, sizeof(measured),
                               "Link not fit to transmit: %d ms, ceiling %u ms",
                               (int)peak, (unsigned)srv->cfg.buffer_ceiling_ms);
                msg = measured;
            }
            send_print(srv, client_idx, msg, now_ms, out);
        }
        return false;
    }

    /* B: the peak-hold over the floor, fixed for the over. A client with no
     * measurement yet plays at the floor rather than not at all. */
    uint32_t buffer_ms = srv->cfg.buffer_floor_ms;
    if (peak > 0 && (uint32_t)peak > buffer_ms) {
        buffer_ms = (uint32_t)peak;
    }

    srv->key_holder = client_idx;
    srv->over_started_at_ms = now_ms;
    srv->holder_last_byte_ms = now_ms;
    cwnet_play_start_over(&srv->play, buffer_ms);

    emit(out, CWNET_SERVER_EV_OVER_BUFFER, client_idx, (int32_t)buffer_ms, 0, now_ms);
    announce_key(srv, now_ms, out);
    return true;
}

static void handle_morse(cwnet_server_t *srv, int client_idx,
                         const uint8_t *payload, size_t payload_len,
                         int64_t now_ms, cwnet_server_result_t *out) {
    if (payload_len == 0u) {
        return;
    }
    if (srv->key_holder == CWNET_SERVER_NOBODY) {
        (void)take_key(srv, client_idx, now_ms, out);
    }
    if (srv->key_holder != client_idx) {
        /* Somebody else is on the key, or this link is not fit: dropped
         * without a word, as the reference does (CwNet.c:2895) */
        srv->morse_ignored += (uint32_t)payload_len;
        return;
    }

    srv->holder_last_byte_ms = now_ms;
    for (size_t i = 0; i < payload_len; i++) {
        /* A byte the engine cannot hold is dropped and counted there */
        (void)cwnet_play_push(&srv->play, payload[i], now_ms);
    }
}

/*===========================================================================*/
/* Frame dispatch                                                            */
/*===========================================================================*/

static void dispatch(cwnet_server_t *srv, int client_idx,
                     const cwnet_parse_result_t *frame,
                     int64_t now_ms, cwnet_server_result_t *out) {
    const cwnet_server_client_t *c = slot_const(srv, client_idx);
    if (c == NULL) {
        return;
    }
    const uint8_t *payload = frame->payload;
    size_t payload_len = frame->payload_len;

    if (frame->command == CWNET_CMD_CONNECT) {
        handle_connect(srv, client_idx, payload, payload_len, now_ms, out);
        return;
    }
    if (!c->confirmed) {
        /* Nothing but the CONNECT counts before the log-in */
        return;
    }

    switch (frame->command) {
        case CWNET_CMD_PING:
            handle_ping(srv, client_idx, payload, payload_len, now_ms, out);
            break;
        case CWNET_CMD_RIG_STRING:
            handle_rig_string(srv, client_idx, payload, payload_len, now_ms, out);
            break;
        case CWNET_CMD_MORSE:
            handle_morse(srv, client_idx, payload, payload_len, now_ms, out);
            break;
        default:
            /* CI-V, spectrum, audio, anything else: ignored (R15) */
            break;
    }
}

/*===========================================================================*/
/* API                                                                       */
/*===========================================================================*/

void cwnet_server_cfg_defaults(cwnet_server_cfg_t *cfg) {
    if (cfg == NULL) {
        return;
    }
    cfg->max_clients = (uint16_t)CWNET_SERVER_DEFAULT_MAX_CLIENTS;
    cfg->ping_interval_ms = CWNET_SERVER_DEFAULT_PING_INTERVAL_MS;
    cfg->handshake_timeout_ms = CWNET_SERVER_DEFAULT_HANDSHAKE_MS;
    cfg->idle_timeout_ms = CWNET_SERVER_DEFAULT_IDLE_MS;
    cfg->over_max_ms = CWNET_SERVER_DEFAULT_OVER_MAX_MS;
    cfg->buffer_floor_ms = CWNET_SERVER_DEFAULT_BUFFER_FLOOR_MS;
    cfg->buffer_ceiling_ms = CWNET_SERVER_DEFAULT_BUFFER_CEILING_MS;
    cfg->play.ptt_lead_ms = 0u;
    cfg->play.ptt_tail_ms = CWNET_PLAY_DEFAULT_PTT_TAIL_MS;
    cfg->send_cb = NULL;
    cfg->user_data = NULL;
}

bool cwnet_server_init(cwnet_server_t *srv, const cwnet_server_cfg_t *cfg) {
    if (srv == NULL) {
        return false;
    }
    memset(srv, 0, sizeof(*srv));
    cwnet_server_cfg_defaults(&srv->cfg);
    if (cfg != NULL) {
        srv->cfg = *cfg;
    }
    if (srv->cfg.max_clients == 0u || srv->cfg.max_clients > CWNET_SERVER_MAX_CLIENTS) {
        srv->cfg.max_clients = (uint16_t)CWNET_SERVER_MAX_CLIENTS;
    }
    if (srv->cfg.ping_interval_ms == 0u) {
        srv->cfg.ping_interval_ms = CWNET_SERVER_DEFAULT_PING_INTERVAL_MS;
    }
    if (srv->cfg.handshake_timeout_ms == 0u) {
        srv->cfg.handshake_timeout_ms = CWNET_SERVER_DEFAULT_HANDSHAKE_MS;
    }
    if (srv->cfg.idle_timeout_ms == 0u) {
        srv->cfg.idle_timeout_ms = CWNET_SERVER_DEFAULT_IDLE_MS;
    }
    if (srv->cfg.over_max_ms == 0u) {
        srv->cfg.over_max_ms = CWNET_SERVER_DEFAULT_OVER_MAX_MS;
    }
    cwnet_play_init(&srv->play, &srv->cfg.play);
    srv->key_holder = CWNET_SERVER_NOBODY;
    return true;
}

int cwnet_server_on_connected(cwnet_server_t *srv, int64_t now_ms,
                               cwnet_server_result_t *out) {
    result_clear(out);
    if (srv == NULL) {
        return CWNET_SERVER_NO_SLOT;
    }
    for (int i = 0; i < (int)srv->cfg.max_clients; i++) {
        cwnet_server_client_t *c = &srv->client[i];
        if (c->in_use) {
            continue;
        }
        int idx = CWNET_SERVER_FIRST_CLIENT + i;
        memset(c, 0, sizeof(*c));
        c->in_use = true;
        c->accepted_at_ms = now_ms;
        c->latency_ms = -1;
        c->latency_peak_ms = -1;
        c->ping_id = (uint8_t)idx;
        cwnet_frame_parser_init(&c->parser);
        return idx;
    }
    /* Past the limit: the caller closes it at once, and the accept never
     * blocks on our account (R1) */
    emit(out, CWNET_SERVER_EV_CLIENT_CLOSED, 0, (int32_t)CWNET_SERVER_CLOSE_NO_SLOT,
         0, now_ms);
    return CWNET_SERVER_NO_SLOT;
}

void cwnet_server_on_data(cwnet_server_t *srv, int client_idx,
                           const uint8_t *data, size_t len,
                           int64_t now_ms, cwnet_server_result_t *out) {
    result_clear(out);
    if (srv == NULL || data == NULL || len == 0u || slot(srv, client_idx) == NULL) {
        return;
    }

    size_t offset = 0;
    while (offset < len) {
        cwnet_server_client_t *c = slot(srv, client_idx);
        if (c == NULL) {
            return;   /* a frame closed this client */
        }
        cwnet_parse_result_t frame = cwnet_frame_parse(&c->parser, data + offset,
                                                        len - offset);
        switch (frame.status) {
            case CWNET_PARSE_OK:
                offset += frame.bytes_consumed;
                dispatch(srv, client_idx, &frame, now_ms, out);
                c = slot(srv, client_idx);
                if (c != NULL) {
                    cwnet_frame_parser_reset(&c->parser);
                }
                break;

            case CWNET_PARSE_SKIPPED:
                /* Too long to buffer and fragmented: consumed, framing kept (R14) */
                offset += frame.bytes_consumed;
                cwnet_frame_parser_reset(&c->parser);
                break;

            case CWNET_PARSE_NEED_MORE:
                offset = len;
                break;

            case CWNET_PARSE_ERROR:
            default:
                /* "un errore di parse chiude la connessione del client" (R15) */
                close_client(srv, client_idx, CWNET_SERVER_CLOSE_PARSE_ERROR, now_ms, out);
                return;
        }
    }

    run_play(srv, now_ms, out);
}

void cwnet_server_on_disconnected(cwnet_server_t *srv, int client_idx,
                                   int64_t now_ms, cwnet_server_result_t *out) {
    result_clear(out);
    if (srv == NULL) {
        return;
    }
    close_client(srv, client_idx, CWNET_SERVER_CLOSE_PEER, now_ms, out);
}

void cwnet_server_poll(cwnet_server_t *srv, int64_t now_ms,
                        cwnet_server_result_t *out) {
    result_clear(out);
    if (srv == NULL) {
        return;
    }

    for (int i = 0; i < (int)srv->cfg.max_clients; i++) {
        int idx = CWNET_SERVER_FIRST_CLIENT + i;
        cwnet_server_client_t *c = slot(srv, idx);
        if (c == NULL) {
            continue;
        }

        if (!c->confirmed) {
            if (now_ms - c->accepted_at_ms >= (int64_t)srv->cfg.handshake_timeout_ms) {
                close_client(srv, idx, CWNET_SERVER_CLOSE_HANDSHAKE, now_ms, out);
            }
            continue;
        }

        if (now_ms < c->next_ping_at_ms) {
            continue;
        }
        if (c->ping_pending) {
            c->ping_misses++;
            if (c->ping_misses >= CWNET_SERVER_PING_MISSES) {
                close_client(srv, idx, CWNET_SERVER_CLOSE_PING_TIMEOUT, now_ms, out);
                continue;
            }
        }
        int32_t t0 = wire_clock(now_ms);
        uint8_t buf[CWNET_PING_PAYLOAD_SIZE];
        if (!cwnet_ping_build_request((uint8_t)idx, t0, buf, sizeof(buf))) {
            continue;
        }
        if (!send_or_close(srv, idx, (uint8_t)CWNET_CMD_PING, buf, sizeof(buf), now_ms, out)) {
            continue;
        }
        c->ping_pending = true;
        c->ping_id = (uint8_t)idx;
        c->ping_t0_ms = t0;
        c->next_ping_at_ms = now_ms + (int64_t)srv->cfg.ping_interval_ms;
    }

    /* Safety nets on the over in progress (R6). The third one, a holder the
     * PINGs declared dead, fired in the loop above through close_client().
     *
     * The idle net speaks only with the key UP: between elements and
     * between overs it frees a key nobody is using, but under a key that is
     * down it would cut an operator's tune-up short at five seconds. There
     * the PING is the judge, because the program answers it and the hand
     * does not (R6, R8). */
    if (srv->key_holder != CWNET_SERVER_NOBODY) {
        if (now_ms - srv->over_started_at_ms >= (int64_t)srv->cfg.over_max_ms) {
            force_release(srv, CWNET_SERVER_FAULT_OVER_TOO_LONG, now_ms, out);
        } else if (!cwnet_play_key_down(&srv->play) &&
                   now_ms - srv->holder_last_byte_ms >= (int64_t)srv->cfg.idle_timeout_ms) {
            force_release(srv, CWNET_SERVER_FAULT_IDLE, now_ms, out);
        }
    }

    run_play(srv, now_ms, out);
}

static void deadline_min(int64_t candidate, bool *have, int64_t *best) {
    if (!*have || candidate < *best) {
        *have = true;
        *best = candidate;
    }
}

bool cwnet_server_next_deadline(const cwnet_server_t *srv, int64_t *out_ms) {
    if (srv == NULL || out_ms == NULL) {
        return false;
    }
    bool have = false;
    int64_t best = 0;

    int64_t play_at = 0;
    if (cwnet_play_next_deadline(&srv->play, &play_at)) {
        deadline_min(play_at, &have, &best);
    }

    for (int i = 0; i < (int)srv->cfg.max_clients; i++) {
        const cwnet_server_client_t *c = &srv->client[i];
        if (!c->in_use) {
            continue;
        }
        if (!c->confirmed) {
            deadline_min(c->accepted_at_ms + (int64_t)srv->cfg.handshake_timeout_ms,
                         &have, &best);
        } else {
            deadline_min(c->next_ping_at_ms, &have, &best);
        }
    }

    if (srv->key_holder != CWNET_SERVER_NOBODY) {
        deadline_min(srv->over_started_at_ms + (int64_t)srv->cfg.over_max_ms, &have, &best);
        /* Not while the key is down: nothing is due then, and putting an
         * instant here would wake the loop only to decide to do nothing.
         * The key comes up on a deadline of the engine's, which is already
         * a candidate, and the poll after it sees the idle net again. */
        if (!cwnet_play_key_down(&srv->play)) {
            deadline_min(srv->holder_last_byte_ms + (int64_t)srv->cfg.idle_timeout_ms,
                         &have, &best);
        }
    }

    if (have) {
        *out_ms = best;
    }
    return have;
}

/*===========================================================================*/
/* Reading the state                                                         */
/*===========================================================================*/

int cwnet_server_key_holder(const cwnet_server_t *srv) {
    return (srv == NULL) ? CWNET_SERVER_NOBODY : srv->key_holder;
}

const char *cwnet_server_client_name(const cwnet_server_t *srv, int client_idx) {
    const cwnet_server_client_t *c = slot_const(srv, client_idx);
    return (c == NULL) ? "" : c->name;
}

bool cwnet_server_client_ready(const cwnet_server_t *srv, int client_idx) {
    const cwnet_server_client_t *c = slot_const(srv, client_idx);
    return c != NULL && c->confirmed;
}

size_t cwnet_server_client_count(const cwnet_server_t *srv) {
    if (srv == NULL) {
        return 0u;
    }
    size_t n = 0;
    for (int i = 0; i < (int)srv->cfg.max_clients; i++) {
        if (srv->client[i].in_use) {
            n++;
        }
    }
    return n;
}

int32_t cwnet_server_client_latency_ms(const cwnet_server_t *srv, int client_idx) {
    const cwnet_server_client_t *c = slot_const(srv, client_idx);
    return (c == NULL) ? -1 : c->latency_ms;
}

int32_t cwnet_server_client_peak_ms(const cwnet_server_t *srv, int client_idx) {
    const cwnet_server_client_t *c = slot_const(srv, client_idx);
    return (c == NULL) ? -1 : c->latency_peak_ms;
}

uint32_t cwnet_server_buffer_ms(const cwnet_server_t *srv) {
    /* The playback engine owns B; asking it beats keeping a second copy in
     * step with it. */
    return (srv == NULL) ? 0u : cwnet_play_buffer_ms(&srv->play);
}

bool cwnet_server_key_down(const cwnet_server_t *srv) {
    return (srv != NULL) && cwnet_play_key_down(&srv->play);
}

bool cwnet_server_ptt_on(const cwnet_server_t *srv) {
    return (srv != NULL) && cwnet_play_ptt_on(&srv->play);
}

uint32_t cwnet_server_play_dropped(const cwnet_server_t *srv) {
    return (srv == NULL) ? 0u : cwnet_play_dropped(&srv->play);
}

uint32_t cwnet_server_morse_ignored(const cwnet_server_t *srv) {
    return (srv == NULL) ? 0u : srv->morse_ignored;
}
