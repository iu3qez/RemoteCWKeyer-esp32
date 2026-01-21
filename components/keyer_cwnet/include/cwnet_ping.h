/**
 * @file cwnet_ping.h
 * @brief CWNet PING handling and timer synchronization
 *
 * Timer synchronization is CRITICAL for CWNet protocol:
 *   - Client MUST sync to server time on EVERY PING REQUEST
 *   - Failure to sync causes timestamp drift and packet rejection
 *
 * PING payload (16 bytes, little-endian):
 *   - type (1 byte): 0=REQUEST, 1=RESPONSE_1, 2=RESPONSE_2
 *   - id (1 byte): sequence ID
 *   - reserved (2 bytes): alignment padding
 *   - t0 (4 bytes): requester timestamp
 *   - t1 (4 bytes): responder 1 timestamp
 *   - t2 (4 bytes): responder 2 timestamp
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief PING payload size (fixed)
 */
#define CWNET_PING_PAYLOAD_SIZE 16

/**
 * @brief PING types
 */
typedef enum {
    CWNET_PING_REQUEST = 0,     /**< Server -> Client: sync request */
    CWNET_PING_RESPONSE_1 = 1,  /**< Client -> Server: first response */
    CWNET_PING_RESPONSE_2 = 2   /**< Server -> Client: latency measurement */
} cwnet_ping_type_t;

/**
 * @brief Parsed PING payload
 */
typedef struct {
    cwnet_ping_type_t type;     /**< PING type */
    uint8_t id;                 /**< Sequence ID */
    int32_t t0_ms;              /**< Requester timestamp */
    int32_t t1_ms;              /**< Responder 1 timestamp */
    int32_t t2_ms;              /**< Responder 2 timestamp */
} cwnet_ping_t;

/**
 * @brief Timer synchronization context
 *
 * Maintains offset between local clock and server clock.
 * The offset is adjusted on every PING REQUEST to prevent drift.
 */
typedef struct {
    int64_t offset_ms;          /**< Offset: synced = local + offset */
} cwnet_timer_t;

/*===========================================================================*/
/* Timer Synchronization                                                     */
/*===========================================================================*/

/**
 * @brief Initialize timer context
 *
 * @param timer Timer context (may be NULL - no-op)
 */
void cwnet_timer_init(cwnet_timer_t *timer);

/**
 * @brief Sync local timer to server time
 *
 * MUST be called on every PING REQUEST received from server.
 * Adjusts the offset to align local time with server time.
 *
 * @param timer Timer context (may be NULL - no-op)
 * @param server_time_ms Server's timestamp from PING t0
 * @param local_time_ms Current local time (e.g., from esp_timer)
 */
void cwnet_timer_sync_to_server(cwnet_timer_t *timer,
                                 int32_t server_time_ms,
                                 int32_t local_time_ms);

/**
 * @brief Read synchronized time
 *
 * Returns local time adjusted by the sync offset.
 *
 * @param timer Timer context (NULL returns 0)
 * @param local_time_ms Current local time
 * @return int32_t Synchronized time (server-relative)
 */
int32_t cwnet_timer_read_synced_ms(const cwnet_timer_t *timer,
                                    int32_t local_time_ms);

/*===========================================================================*/
/* PING Parsing and Building                                                 */
/*===========================================================================*/

/**
 * @brief Parse PING payload
 *
 * @param ping Output parsed PING (NULL returns false)
 * @param payload Input payload bytes (NULL returns false)
 * @param len Payload length (must be >= 16)
 * @return true if parsed successfully
 */
bool cwnet_ping_parse(cwnet_ping_t *ping,
                      const uint8_t *payload,
                      size_t len);

/**
 * @brief Build PING RESPONSE_1 from REQUEST
 *
 * Builds a response payload with:
 *   - type = RESPONSE_1
 *   - id = preserved from request
 *   - t0 = preserved from request
 *   - t1 = our_time_ms
 *   - t2 = 0
 *
 * @param request Input REQUEST ping
 * @param buffer Output buffer (must be >= 16 bytes)
 * @param buf_len Buffer size
 * @param our_time_ms Our synchronized timestamp for t1
 * @return true if built successfully
 */
bool cwnet_ping_build_response(const cwnet_ping_t *request,
                                uint8_t *buffer,
                                size_t buf_len,
                                int32_t our_time_ms);

/**
 * @brief Calculate round-trip latency from RESPONSE_2
 *
 * @param response RESPONSE_2 ping (NULL or wrong type returns -1)
 * @return int32_t RTT in ms (t2 - t0), or -1 on error
 */
int32_t cwnet_ping_calc_latency(const cwnet_ping_t *response);
