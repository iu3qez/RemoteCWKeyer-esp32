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
 *   CONNECTING -> recv WELCOME -> READY
 *   READY -> recv PING_REQUEST -> send RESPONSE_1, sync timer
 *   READY -> recv PING_RESPONSE_2 -> update latency
 *   READY -> send_key_event() -> send CW_DOWN/CW_UP
 *   any state -> on_disconnected() -> DISCONNECTED
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
    CWNET_CMD_WELCOME = 0x00,   /**< Server -> Client: connection accepted */
    CWNET_CMD_CONNECT = 0x01,   /**< Client -> Server: connection request */
    CWNET_CMD_DISCONNECT = 0x02,/**< Bidirectional: disconnect */
    CWNET_CMD_PING = 0x03,      /**< Bidirectional: time sync */
    CWNET_CMD_CW_UP = 0x14,     /**< Key up event */
    CWNET_CMD_CW_DOWN = 0x15,   /**< Key down event */
} cwnet_cmd_t;

/** CONNECT payload field sizes */
#define CWNET_CONNECT_USERNAME_LEN  44
#define CWNET_CONNECT_CALLSIGN_LEN  44
#define CWNET_CONNECT_PAYLOAD_LEN   92  /**< 44 + 44 + 4 */

/*===========================================================================*/
/* Client State                                                              */
/*===========================================================================*/

/**
 * @brief Client connection state
 */
typedef enum {
    CWNET_STATE_DISCONNECTED = 0,  /**< Not connected */
    CWNET_STATE_CONNECTING,        /**< TCP connected, waiting for WELCOME */
    CWNET_STATE_READY,             /**< WELCOME received, can send/receive CW */
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

/**
 * @brief CW event received callback (optional)
 *
 * Called when a CW event is received from the server (another operator).
 *
 * @param key_down true if key down, false if key up
 * @param timestamp_ms Event timestamp (server time)
 * @param user_data User context pointer
 */
typedef void (*cwnet_cw_event_cb_t)(bool key_down,
                                     int32_t timestamp_ms,
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
    cwnet_cw_event_cb_t cw_event_cb;          /**< Received CW event */

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
    cwnet_cw_event_cb_t cw_event_cb;
    void *user_data;

    /* State */
    cwnet_client_state_t state;

    /* Timer synchronization */
    cwnet_timer_t timer;

    /* Latency measurement */
    int32_t latency_ms;  /**< Last measured RTT, -1 if unknown */

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
void cwnet_client_on_data(cwnet_client_t *client,
                           const uint8_t *data,
                           size_t len);

/*===========================================================================*/
/* Outgoing Events                                                           */
/*===========================================================================*/

/**
 * @brief Send CW key event
 *
 * Sends a key down or key up event to the server.
 * Only valid in READY state.
 *
 * @param client Client context
 * @param key_down true for key down, false for key up
 * @return CWNET_CLIENT_OK on success,
 *         CWNET_CLIENT_ERR_NOT_READY if not in READY state
 */
cwnet_client_err_t cwnet_client_send_key_event(cwnet_client_t *client,
                                                bool key_down);
