/**
 * @file cwnet_server.h
 * @brief The station server's core: who is connected, who has the key
 *
 * This is the state machine behind the station daemon. It takes a client
 * from its CONNECT to READY, announces who holds the key, arbitrates the
 * key between clients, answers the PINGs and the rig-control strings, and
 * hands the key holder's MORSE bytes to the playback engine (cwnet_play).
 *
 * HOST ONLY. Deliberately absent from the ESP-IDF component SRCS, like
 * cwnet_play.c: the box is never the server (KTD2).
 *
 * It owns no socket, no clock and no log (KTD1, KTD10):
 *   - the caller accepts the TCP connection and calls cwnet_server_on_connected()
 *   - it feeds received bytes to cwnet_server_on_data()
 *   - it calls cwnet_server_poll() when a deadline comes due
 *   - it calls cwnet_server_on_disconnected() when a socket dies
 *   - sending goes out through one callback, addressed by client index
 * Every entry point takes the current instant; what happened comes back in
 * a result structure, never in a log line. Printing it is the daemon's job.
 *
 * Client indices are the reference's: 1 and up for remote clients
 * (CwNet.h, CWNET_FIRST_REMOTE_CLIENT_INDEX), and they are what goes in
 * the TX_INFO index byte and in the PING id. Index 0, the server's own
 * operator ("The Sysop"), does not exist here: there is no local operator
 * on this server (Scope Boundaries).
 *
 * Permissions
 * -----------
 * Everybody who connects may talk, transmit and control the rig: the echo
 * always carries 0x07 and there is no user list (KTD6). What matters is
 * knowing WHO is transmitting, and that is the TX_INFO.
 *
 * The key
 * -------
 * The first MORSE byte that arrives while nobody holds the key takes it,
 * as in the reference (CwNet.c:2875-2903); every other client's MORSE is
 * dropped without a word. Unlike the reference we keep it for the whole
 * over: its 1 s stopwatch, which makes the announcement oscillate during
 * an over, is not copied (KTD4). The key comes back when the end-of-over
 * marker has been played and the PTT has dropped, or when a safety net
 * fires: the holder's TCP closes, nothing arrives from it for the idle
 * timeout, or the over outlasts its ceiling. Each of those forces the key
 * up at once and drops the PTT at the tail at the latest.
 *
 * Link eligibility
 * ----------------
 * The buffer B of an over is the holder's peak-hold latency, floored at
 * cfg.buffer_floor_ms, fixed when it takes the key. Above
 * cfg.buffer_ceiling_ms the link is not fit to transmit: the client does
 * not take the key, nothing of its is played, and it gets a PRINT saying
 * so with the measured value. Two seconds of buffer means the far end is
 * already answering when the PTT drops, and slow turnaround after a
 * transmission is the main complaint about the original program.
 *
 * A client that has never had a PING answered has no measurement (peak
 * -1): it is treated as fit and plays with B at the floor. Refusing it
 * would refuse every client for the first two seconds of its session.
 *
 * A client that HAS answered and still has no peak is the opposite case.
 * The peak-hold only takes a sample the gate lets through (cwnet_ping.h,
 * cwnet_ping_peak_hold_update): a link slow enough that every answer misses
 * that window never records one. Left together with the client above it
 * would look identical — fit, and playing at the floor — which is exactly
 * the link the ceiling exists to keep off the air. So the two are kept
 * apart: never answered is fit, answered but never measurable is not.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cwnet_frame.h"
#include "cwnet_ping.h"
#include "cwnet_play.h"

/*===========================================================================*/
/* Constants                                                                 */
/*===========================================================================*/

/** Clients the table can hold; cfg.max_clients selects how many are used */
#define CWNET_SERVER_MAX_CLIENTS 8

/** First remote client index, as CWNET_FIRST_REMOTE_CLIENT_INDEX in CwNet.h */
#define CWNET_SERVER_FIRST_CLIENT 1

/** Key holder index meaning "nobody"; 0xFF on the wire */
#define CWNET_SERVER_NOBODY (-1)

/** cwnet_server_on_connected() when every slot is taken: accept, then close */
#define CWNET_SERVER_NO_SLOT (-1)

/** Longest announced name, NUL included ("NoCall #8", a callsign, "-- nobody --") */
#define CWNET_SERVER_NAME_LEN 48

/**
 * @brief "Text the receiver should print somewhere", CWNET_CMD_PRINT in CwNet.h:65
 *
 * Not in cwnet_client.h's command enum: the box never sends one, and the
 * server is the only side here that does.
 */
#define CWNET_SERVER_CMD_PRINT 0x04u

/** Events one call can report before the caller must call again */
#define CWNET_SERVER_MAX_EVENTS 32

/* Defaults: the reference's, the box's, or the decisions of the plan */
#define CWNET_SERVER_DEFAULT_MAX_CLIENTS 4u
#define CWNET_SERVER_DEFAULT_PING_INTERVAL_MS 2000u   /**< CwNet.c:2251 */
#define CWNET_SERVER_DEFAULT_HANDSHAKE_MS 5000u
#define CWNET_SERVER_DEFAULT_IDLE_MS 5000u
#define CWNET_SERVER_DEFAULT_OVER_MAX_MS 120000u
#define CWNET_SERVER_DEFAULT_BUFFER_FLOOR_MS 100u
#define CWNET_SERVER_DEFAULT_BUFFER_CEILING_MS 1000u  /**< The eligibility ceiling */

/** Unanswered PING requests in a row that close a client (R16) */
#define CWNET_SERVER_PING_MISSES 3u

/*===========================================================================*/
/* Rig-control result codes                                                  */
/*===========================================================================*/

/**
 * @brief What a 0x06 rig-control string is answered with
 *
 * The values are the reference's (HamlibResultCodes.h, as CwNet.c uses
 * them). Nothing is ever applied to a rig here: "set_ptt" is acknowledged
 * and dropped, because the PTT follows the keying that is played, not the
 * strings that arrive (KTD5, R10).
 */
#define CWNET_SERVER_RPRT_OK 0            /**< HAMLIB_RESULT_NO_ERROR */
#define CWNET_SERVER_RPRT_BAD_ARG (-17)   /**< HAMLIB_RESULT_ARG_OUT_OF_DOM, CwNet.c:4102 */
#define CWNET_SERVER_RPRT_NO_FUNC (-11)   /**< HAMLIB_RESULT_NOT_AVAILABLE, CwNet.c:4114 */

/*===========================================================================*/
/* Events                                                                    */
/*===========================================================================*/

/** Why a client's connection ended */
typedef enum {
    CWNET_SERVER_CLOSE_NO_SLOT = 0,     /**< Accepted past the client limit */
    CWNET_SERVER_CLOSE_BAD_CONNECT,     /**< CONNECT of the wrong length */
    CWNET_SERVER_CLOSE_PARSE_ERROR,     /**< Frame parse error (R15) */
    CWNET_SERVER_CLOSE_BAD_STRING,      /**< 0x06 string with no NUL in its payload */
    CWNET_SERVER_CLOSE_HANDSHAKE,       /**< No CONNECT within the handshake timeout */
    CWNET_SERVER_CLOSE_PING_TIMEOUT,    /**< Three PING requests unanswered */
    CWNET_SERVER_CLOSE_SEND_FAILED,     /**< The send callback failed */
    CWNET_SERVER_CLOSE_PEER,            /**< The socket died: on_disconnected() */
} cwnet_server_close_reason_t;

/** What went wrong badly enough to write a line about */
typedef enum {
    /**
     * The grace ran out with the key down: the key was forced up.
     *
     * Not an empty FIFO. An empty FIFO at a deadline is the normal state of
     * a live over (R8): every byte arrives about B ms before its own
     * deadline, so any element longer than B empties the queue and the
     * applied state simply holds. What this fault says is that the holding
     * lasted longer than cfg.play.key_grace_ms with a carrier up, and the
     * engine lifted the key rather than leave it there.
     */
    CWNET_SERVER_FAULT_GRACE_EXPIRED = 0,
    CWNET_SERVER_FAULT_HOLDER_GONE,     /**< The key holder's TCP closed mid-over */
    CWNET_SERVER_FAULT_IDLE,            /**< Nothing from the holder for the idle timeout */
    CWNET_SERVER_FAULT_OVER_TOO_LONG,   /**< The over outlasted its ceiling */
} cwnet_server_fault_t;

/** What the core did, for the daemon to print and to put on the key output */
typedef enum {
    CWNET_SERVER_EV_CLIENT_READY = 0, /**< CONNECT confirmed; client_idx, name available */
    CWNET_SERVER_EV_CLIENT_CLOSED,    /**< Connection over; value = cwnet_server_close_reason_t */
    CWNET_SERVER_EV_KEY_HOLDER,       /**< The key changed hands; client_idx or CWNET_SERVER_NOBODY */
    CWNET_SERVER_EV_LATENCY,          /**< A PING closed; value = RTT ms, peak = peak-hold ms */
    /**
     * Refused the key. Reported once per refusal, as often as the PRINT
     * that goes with it and no more: a refused client keeps sending, and
     * one event per rejected byte would fill this array with one line.
     * value = the peak-hold that refused it, or -1 when the client has
     * answered PINGs but never inside the measurement window.
     */
    CWNET_SERVER_EV_LINK_UNFIT,
    CWNET_SERVER_EV_OVER_BUFFER,      /**< An over started; value = B in ms */
    CWNET_SERVER_EV_KEY_DOWN,         /**< Key output: carrier on */
    CWNET_SERVER_EV_KEY_UP,           /**< Key output: carrier off */
    CWNET_SERVER_EV_PTT_ON,           /**< PTT output: on */
    CWNET_SERVER_EV_PTT_OFF,          /**< PTT output: off */
    /**
     * A byte arrived after its own deadline: the element on the air came
     * out longer by the delay (R8). The link is slipping, and this is how
     * the operator learns it before the grace expires and it becomes a
     * fault. value = bytes applied late since start, peak_ms = the
     * milliseconds they added, in total (cwnet_play_late_bytes(),
     * cwnet_play_late_ms()).
     */
    CWNET_SERVER_EV_LATE_BYTE,
    CWNET_SERVER_EV_FAULT,            /**< value = cwnet_server_fault_t */
} cwnet_server_event_type_t;

/**
 * @brief One event with the instant it belongs to
 *
 * @note at_ms is the instant the thing happened, which for a keying edge
 *       is the instant it was scheduled for, not the instant of the call.
 */
typedef struct {
    cwnet_server_event_type_t type;
    int client_idx;   /**< 1..max_clients, CWNET_SERVER_NOBODY, or 0 when it fits no client */
    int32_t value;    /**< Per type: reason, RTT, peak, B */
    int32_t peak_ms;  /**< LATENCY: the peak-hold after the sample. LATE_BYTE: total late ms */
    int64_t at_ms;
} cwnet_server_event_t;

/**
 * @brief Events of one call, oldest first
 *
 * If it fills, the rest is lost rather than the state machine stalling:
 * events are diagnostics, the state is the truth. The count of dropped
 * events is kept so the daemon can say so.
 */
typedef struct {
    cwnet_server_event_t ev[CWNET_SERVER_MAX_EVENTS];
    size_t count;
    size_t dropped;
} cwnet_server_result_t;

/*===========================================================================*/
/* Configuration                                                             */
/*===========================================================================*/

/**
 * @brief Send data to one client
 *
 * @param client_idx Client index, 1 and up
 * @param data       Bytes to send
 * @param len        Length
 * @param user_data  Caller's context
 * @return Bytes sent, or negative on error. Anything but a full write
 *         closes that client (a partial write would break the framing).
 */
typedef int (*cwnet_server_send_cb_t)(int client_idx, const uint8_t *data,
                                       size_t len, void *user_data);

/**
 * @brief The daemon's knobs. cwnet_server_cfg_defaults() fills the defaults.
 */
typedef struct {
    uint16_t max_clients;          /**< Clients served at once (default 4) */
    uint32_t ping_interval_ms;     /**< PING cadence per client (default 2000) */
    uint32_t handshake_timeout_ms; /**< Accept to CONNECT (default 5000) */
    uint32_t idle_timeout_ms;      /**< Silence from the holder mid-over (default 5000) */
    uint32_t over_max_ms;          /**< Ceiling on one over (default 120000) */
    uint32_t buffer_floor_ms;      /**< Floor under B (default 100) */
    uint32_t buffer_ceiling_ms;    /**< Link eligibility ceiling (default 1000) */
    cwnet_play_cfg_t play;         /**< PTT lead and tail for the playback engine */

    cwnet_server_send_cb_t send_cb; /**< Required */
    void *user_data;
} cwnet_server_cfg_t;

/*===========================================================================*/
/* Context                                                                   */
/*===========================================================================*/

/** One connection's slot. Read through the accessors, not by hand. */
typedef struct {
    bool in_use;
    bool confirmed;                 /**< CONNECT accepted: READY */
    bool unfit_notified;            /**< Already told this link is not fit */
    char name[CWNET_SERVER_NAME_LEN]; /**< Announced name: callsign, or "NoCall #n" */
    char username[CWNET_SERVER_NAME_LEN]; /**< From the CONNECT, for the welcome only */
    cwnet_frame_parser_t parser;

    int64_t accepted_at_ms;
    int64_t next_ping_at_ms;
    bool ping_pending;
    uint8_t ping_id;
    int32_t ping_t0_ms;             /**< t0 of the pending request, 31-bit */
    uint32_t ping_misses;           /**< Requests unanswered in a row */

    int32_t latency_ms;             /**< Last gated RTT, -1 until one lands */
    int32_t latency_peak_ms;        /**< Peak-hold of it, -1 until one lands */
    bool ping_answered;             /**< A PING came back, gate or no gate */
} cwnet_server_client_t;

/**
 * @brief The server. Allocated by the caller, never by us.
 */
typedef struct {
    cwnet_server_cfg_t cfg;
    cwnet_server_client_t client[CWNET_SERVER_MAX_CLIENTS];

    cwnet_play_t play;              /**< The key holder's bytes become edges here */

    int key_holder;                 /**< 1.. , or CWNET_SERVER_NOBODY */
    int64_t over_started_at_ms;     /**< When the key was taken */
    int64_t holder_last_byte_ms;    /**< Last MORSE byte from the holder */

    uint32_t morse_ignored;         /**< Bytes dropped because the sender had no key */
} cwnet_server_t;

/*===========================================================================*/
/* API                                                                       */
/*===========================================================================*/

/**
 * @brief Fill a configuration with the defaults
 *
 * send_cb and user_data are left alone: they are the caller's.
 */
void cwnet_server_cfg_defaults(cwnet_server_cfg_t *cfg);

/**
 * @brief Set up a server. No client is connected and the key is free.
 *
 * @param srv Server, not NULL
 * @param cfg Configuration; NULL takes the defaults, but then nothing can
 *            be sent. max_clients is clamped to CWNET_SERVER_MAX_CLIENTS,
 *            and a zero timeout or interval takes its default.
 * @return false if srv is NULL
 */
bool cwnet_server_init(cwnet_server_t *srv, const cwnet_server_cfg_t *cfg);

/**
 * @brief A TCP connection was accepted
 *
 * @param srv    Server
 * @param now_ms Now
 * @param out    Events, cleared first; may be NULL
 * @return The client index to use from here on, or CWNET_SERVER_NO_SLOT
 *         when every slot is taken. In that case an event says so and the
 *         caller closes the socket at once: the accept never blocks (R1).
 */
int cwnet_server_on_connected(cwnet_server_t *srv, int64_t now_ms,
                               cwnet_server_result_t *out);

/**
 * @brief Bytes arrived from one client
 *
 * Handles fragmentation; an oversized fragmented frame is skipped without
 * losing the framing (R14). A parse error closes the client (R15).
 *
 * @param srv        Server
 * @param client_idx Index from cwnet_server_on_connected()
 * @param data       Received bytes
 * @param len        How many
 * @param now_ms     Now: it timestamps the MORSE bytes for the engine
 * @param out        Events, cleared first; may be NULL
 */
void cwnet_server_on_data(cwnet_server_t *srv, int client_idx,
                           const uint8_t *data, size_t len,
                           int64_t now_ms, cwnet_server_result_t *out);

/**
 * @brief A client's socket died
 *
 * Frees the slot, and releases the key if that client held it. Calling it
 * for a slot the core has already closed itself is a no-op.
 */
void cwnet_server_on_disconnected(cwnet_server_t *srv, int client_idx,
                                   int64_t now_ms, cwnet_server_result_t *out);

/**
 * @brief Everything that is due at or before now_ms
 *
 * The PINGs, the timeouts, the safety nets, and the playback engine's key
 * and PTT edges.
 *
 * @param out Events, cleared first; may be NULL
 */
void cwnet_server_poll(cwnet_server_t *srv, int64_t now_ms,
                        cwnet_server_result_t *out);

/**
 * @brief Instant of the next thing the server has to do
 *
 * The daemon waits on its sockets until then. Covers the playback
 * deadlines, the PING cadence, the handshake timeout and the safety nets.
 *
 * @return false when nothing is scheduled.
 */
bool cwnet_server_next_deadline(const cwnet_server_t *srv, int64_t *out_ms);

/*===========================================================================*/
/* Reading the state                                                         */
/*===========================================================================*/

/** @return the client index holding the key, or CWNET_SERVER_NOBODY */
int cwnet_server_key_holder(const cwnet_server_t *srv);

/** @return the name announced for a client, "" if the slot is free or unconfirmed */
const char *cwnet_server_client_name(const cwnet_server_t *srv, int client_idx);

/** @return true when the slot holds a client that has completed its CONNECT */
bool cwnet_server_client_ready(const cwnet_server_t *srv, int client_idx);

/** @return connected clients, confirmed or not */
size_t cwnet_server_client_count(const cwnet_server_t *srv);

/** @return last gated RTT in ms for a client, -1 if unknown */
int32_t cwnet_server_client_latency_ms(const cwnet_server_t *srv, int client_idx);

/** @return peak-hold RTT in ms for a client, -1 if unknown */
int32_t cwnet_server_client_peak_ms(const cwnet_server_t *srv, int client_idx);

/** @return B of the over in progress, 0 when the key is free */
uint32_t cwnet_server_buffer_ms(const cwnet_server_t *srv);

/** @return key state on the output */
bool cwnet_server_key_down(const cwnet_server_t *srv);

/** @return PTT state on the output */
bool cwnet_server_ptt_on(const cwnet_server_t *srv);

/** @return MORSE bytes the playback engine could not hold */
uint32_t cwnet_server_play_dropped(const cwnet_server_t *srv);

/** @return MORSE bytes dropped because their sender did not hold the key */
uint32_t cwnet_server_morse_ignored(const cwnet_server_t *srv);
