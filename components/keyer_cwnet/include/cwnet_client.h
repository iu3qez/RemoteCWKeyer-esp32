/**
 * @file cwnet_client.h
 * @brief CWNet TCP client state machine
 *
 * This module implements the CWNet protocol client as a state machine.
 * Socket operations are abstracted through callbacks to allow:
 *   - Testing without actual network
 *   - Integration with different TCP stacks (lwIP, POSIX, etc.)
 *
 * IMPORTANT: This client does NOT manage the TCP socket itself.
 * The caller must:
 *   1. Create and connect the TCP socket
 *   2. Call cwnet_client_on_connected() when connected
 *   3. Call cwnet_client_on_data() when data arrives
 *   4. Call cwnet_client_on_disconnected() on disconnect/error
 *   5. Implement send_cb to write to socket
 *
 * Protocol flow:
 *   DISCONNECTED -> on_connected() -> CONNECTING (sends IDENT)
 *   CONNECTING -> recv CONNECT echo -> READY
 *   READY -> recv PING_REQUEST -> send RESPONSE_1, sync timer
 *   READY -> recv PING_RESPONSE_2 -> update latency
 *   READY -> send_key_event() -> send MORSE (7-bit keying stream)
 *   READY -> poll() after 14 dot-times of key-up -> send the end of the over
 *   any state -> on_disconnected() -> DISCONNECTED
 *
 * Keying on the wire (reference: DL4YHF Remote CW Keyer, CwStreamEnc.c and
 * KeyerThread.c, sw_MorseTxFifo):
 *   Each key transition becomes one byte in a MORSE 0x10 frame: bit 7 is the
 *   new key state, bits 6..0 the 7-bit encoded time to wait before applying
 *   it, measured from the previous transition. The first transition of an
 *   over waits 0. After each byte the sender's reference instant advances by
 *   the *encoded* milliseconds, not the measured ones, so quantisation error
 *   does not accumulate. A wait above CWSTREAM_MAX_WAIT_MS is split over
 *   several bytes with the same key state. Once the key has been up for more
 *   than 14 dot-times the sender emits a second key-up: that is how the
 *   receiver learns the over has ended.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "cwnet_frame.h"
#include "cwnet_ping.h"

/*===========================================================================*/
/* Constants                                                                 */
/*===========================================================================*/

/** Maximum username length (case sensitive) */
#define CWNET_MAX_USERNAME_LEN 32

/** Maximum server host length */
#define CWNET_MAX_HOST_LEN 64

/*===========================================================================*/
/* CWNet Protocol Commands                                                   */
/*===========================================================================*/

/**
 * @brief CWNet command codes (bits 5-0 of command byte)
 *
 * Command byte format: [CAT:2][CMD:6]
 *   - Bits 7-6: Block type (category)
 *   - Bits 5-0: Command code
 *
 * To build command byte: (category << 6) | command
 */
typedef enum {
    CWNET_CMD_CONNECT = 0x01,   /**< Client -> Server request; echoed back by the server to confirm */
    CWNET_CMD_DISCONNECT = 0x02,/**< Bidirectional: disconnect */
    CWNET_CMD_PING = 0x03,      /**< Bidirectional: time sync */
    CWNET_CMD_MORSE = 0x10,     /**< Keying: 7-bit stream, bit 7 = key state, bits 6..0 = wait */
    CWNET_CMD_CI_V = 0x14,      /**< A single CI-V packet (Icom rig control). Not keying. */
    CWNET_CMD_SPECTRUM = 0x15,  /**< Spectrum data for the waterfall display. Not keying. */
} cwnet_cmd_t;

/** Longest MORSE payload this client sends in one frame (bytes = events) */
#define CWNET_MORSE_MAX_EVENTS 8

/** CONNECT payload field sizes */
#define CWNET_CONNECT_USERNAME_LEN  44
#define CWNET_CONNECT_CALLSIGN_LEN  44
#define CWNET_CONNECT_PAYLOAD_LEN   92  /**< 44 + 44 + 4 */
#define CWNET_CONNECT_PERMISSIONS_OFFSET 88 /**< Permissions bitmask, uint32 LE */

/** Permission bits the server grants in its CONNECT echo */
#define CWNET_PERMISSION_TALK      0x01u  /**< May talk to other users */
#define CWNET_PERMISSION_TRANSMIT  0x02u  /**< May transmit (CW) */
#define CWNET_PERMISSION_CTRL_RIG  0x04u  /**< May control the remote rig */
#define CWNET_PERMISSION_ADMIN     0x08u  /**< Is the server administrator */

/*===========================================================================*/
/* Client State                                                              */
/*===========================================================================*/

/**
 * @brief Client connection state
 */
typedef enum {
    CWNET_STATE_DISCONNECTED = 0,  /**< Not connected */
    CWNET_STATE_CONNECTING,        /**< TCP connected, waiting for the CONNECT echo */
    CWNET_STATE_READY,             /**< Connection confirmed, can send/receive CW */
} cwnet_client_state_t;

/**
 * @brief Client error codes
 */
typedef enum {
    CWNET_CLIENT_OK = 0,           /**< Success */
    CWNET_CLIENT_ERR_INVALID_ARG,  /**< Invalid argument */
    CWNET_CLIENT_ERR_NOT_READY,    /**< Not in READY state */
    CWNET_CLIENT_ERR_SEND_FAILED,  /**< Send callback failed */
    CWNET_CLIENT_ERR_PROTOCOL,     /**< Protocol error */
    CWNET_CLIENT_ERR_NOT_PERMITTED,/**< Server did not grant TRANSMIT */
} cwnet_client_err_t;

/*===========================================================================*/
/* Callbacks                                                                 */
/*===========================================================================*/

/**
 * @brief Send data callback
 *
 * Called when the client needs to send data to the server.
 *
 * @param data Data to send
 * @param len Data length
 * @param user_data User context pointer
 * @return Number of bytes sent, or negative on error
 */
typedef int (*cwnet_send_cb_t)(const uint8_t *data, size_t len, void *user_data);

/**
 * @brief Get current time callback
 *
 * Called to get current time in milliseconds for timestamps.
 *
 * @param user_data User context pointer
 * @return Current time in milliseconds
 */
typedef int32_t (*cwnet_get_time_ms_cb_t)(void *user_data);

/**
 * @brief State change callback (optional)
 *
 * Called when the client state changes.
 *
 * @param old_state Previous state
 * @param new_state New state
 * @param user_data User context pointer
 */
typedef void (*cwnet_state_change_cb_t)(cwnet_client_state_t old_state,
                                         cwnet_client_state_t new_state,
                                         void *user_data);

/*===========================================================================*/
/* Configuration                                                             */
/*===========================================================================*/

/**
 * @brief Client configuration
 */
typedef struct {
    const char *server_host;            /**< Server hostname/IP (required) */
    uint16_t server_port;               /**< Server port (required) */
    const char *username;               /**< Username for logging (case sensitive) */

    /* Required callbacks */
    cwnet_send_cb_t send_cb;            /**< Send data callback (required) */
    cwnet_get_time_ms_cb_t get_time_ms_cb; /**< Get time callback (required) */

    /* Optional callbacks */
    cwnet_state_change_cb_t state_change_cb;  /**< State change notification */

    void *user_data;                    /**< User context for callbacks */
} cwnet_client_config_t;

/*===========================================================================*/
/* Client Context                                                            */
/*===========================================================================*/

/**
 * @brief Client context structure
 *
 * Contains all state for a single CWNet connection.
 */
typedef struct {
    /* Configuration (copied) */
    char server_host[CWNET_MAX_HOST_LEN];
    uint16_t server_port;
    char username[CWNET_MAX_USERNAME_LEN];

    /* Callbacks */
    cwnet_send_cb_t send_cb;
    cwnet_get_time_ms_cb_t get_time_ms_cb;
    cwnet_state_change_cb_t state_change_cb;
    void *user_data;

    /* State */
    cwnet_client_state_t state;

    /* Timer synchronization */
    cwnet_timer_t timer;

    /* Latency measurement */
    int32_t latency_ms;  /**< Last measured RTT, -1 if unknown */

    /* Permissions granted by the server in its CONNECT echo */
    uint32_t permissions;

    /* MORSE TX stopwatch: the reference's sw_MorseTxFifo, fFillingTxFifo and
     * fMorseOutput_sent (KeyerThread.c). */
    int32_t tx_ref_ms;   /**< Instant the next wait is measured from; advances by the encoded ms */
    bool tx_filling;     /**< Inside an over: waits are measured, not forced to 0 */
    bool tx_key_down;    /**< Last key state put on the wire */

    /* Frame parser for incoming data */
    cwnet_frame_parser_t parser;
} cwnet_client_t;

/*===========================================================================*/
/* API Functions                                                             */
/*===========================================================================*/

/**
 * @brief Initialize client
 *
 * @param client Client context (must not be NULL)
 * @param config Configuration (must not be NULL)
 * @return CWNET_CLIENT_OK on success
 */
cwnet_client_err_t cwnet_client_init(cwnet_client_t *client,
                                      const cwnet_client_config_t *config);

/**
 * @brief Get current state
 *
 * @param client Client context
 * @return Current state, or DISCONNECTED if client is NULL
 */
cwnet_client_state_t cwnet_client_get_state(const cwnet_client_t *client);

/**
 * @brief Get synced time
 *
 * Returns local time adjusted to server time using the sync offset.
 * Call this to get timestamps for CW events.
 *
 * @param client Client context
 * @return Synced time in ms, or 0 if client is NULL
 */
int32_t cwnet_client_get_synced_time(const cwnet_client_t *client);

/**
 * @brief Get last measured latency
 *
 * @param client Client context
 * @return RTT in ms, or -1 if unknown/not measured
 */
int32_t cwnet_client_get_latency_ms(const cwnet_client_t *client);

/*===========================================================================*/
/* Connection Events (called by socket layer)                                */
/*===========================================================================*/

/**
 * @brief Notify client that TCP connection was established
 *
 * Call this when the TCP socket successfully connects.
 * Client will transition to CONNECTING and send IDENT.
 *
 * @param client Client context
 */
void cwnet_client_on_connected(cwnet_client_t *client);

/**
 * @brief Notify client that TCP connection was closed
 *
 * Call this when the TCP socket disconnects or errors.
 * Client will transition to DISCONNECTED.
 *
 * @param client Client context
 */
void cwnet_client_on_disconnected(cwnet_client_t *client);

/**
 * @brief Feed received data to the client
 *
 * Call this when data arrives on the TCP socket.
 * Handles fragmentation internally using the frame parser.
 *
 * @param client Client context
 * @param data Received data
 * @param len Data length
 */
/**
 * @brief Permissions the server granted, from its CONNECT echo
 *
 * Zero until the connection is confirmed. Bits: TALK 1, TRANSMIT 2,
 * CTRL_RIG 4, ADMIN 8.
 *
 * @param client Client context
 * @return uint32_t Granted permission bitmask, 0 if client is NULL
 */
uint32_t cwnet_client_get_permissions(const cwnet_client_t *client);

void cwnet_client_on_data(cwnet_client_t *client,
                           const uint8_t *data,
                           size_t len);

/*===========================================================================*/
/* Outgoing Events                                                           */
/*===========================================================================*/

/**
 * @brief Send a key transition as a MORSE frame
 *
 * Emits one MORSE 0x10 frame carrying the new key state and the 7-bit wait
 * since the previous transition, as the reference client does (see the file
 * header). A call that repeats the current key state sends nothing: the
 * reference encodes only on a change of its keying output.
 *
 * Only valid in READY state, and only once the server granted TRANSMIT.
 *
 * @param client Client context
 * @param key_down true for key down, false for key up
 * @param at_ms Instant of the transition, in milliseconds on a clock the
 *              caller keeps monotonic across calls (only differences are
 *              used; the first transition of an over is sent with wait 0).
 *              Round to the millisecond, do not truncate: the reference
 *              rounds its microsecond stopwatch.
 * @return CWNET_CLIENT_OK on success (also when nothing had to be sent),
 *         CWNET_CLIENT_ERR_NOT_READY if not in READY state,
 *         CWNET_CLIENT_ERR_NOT_PERMITTED if the server did not grant TRANSMIT,
 *         CWNET_CLIENT_ERR_SEND_FAILED if the send callback failed
 */
cwnet_client_err_t cwnet_client_send_key_event(cwnet_client_t *client,
                                                bool key_down,
                                                int32_t at_ms);

/**
 * @brief Close an over that has gone quiet
 *
 * Call periodically. When the key has been up for more than 14 dot-times
 * since the last transition, sends the second key-up the reference uses to
 * mark the end of an over, and stops measuring: the next transition starts
 * a new over with wait 0.
 *
 * @param client Client context
 * @param now_ms Current instant on the same clock as send_key_event()'s at_ms
 * @param dot_ms Local dot time in milliseconds (1200 / WPM); the threshold
 *               is 14 * dot_ms, as the reference's KeyerThread.c
 * @return true if the end-of-over byte was sent by this call
 */
bool cwnet_client_poll(cwnet_client_t *client, int32_t now_ms, int32_t dot_ms);
