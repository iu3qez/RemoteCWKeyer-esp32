/**
 * @file cwnet_ping.c
 * @brief CWNet PING handling and timer synchronization implementation
 */

#include "cwnet_ping.h"
#include <string.h>

/*===========================================================================*/
/* Timer Synchronization                                                     */
/*===========================================================================*/

void cwnet_timer_init(cwnet_timer_t *timer) {
    if (timer == NULL) {
        return;
    }
    timer->offset_ms = 0;
}

void cwnet_timer_sync_to_server(cwnet_timer_t *timer,
                                 int32_t server_time_ms,
                                 int32_t local_time_ms) {
    if (timer == NULL) {
        return;
    }

    /* Calculate current synced time with existing offset */
    int64_t current_synced = (int64_t)local_time_ms + timer->offset_ms;

    /* Calculate delta from server time */
    int64_t delta = current_synced - (int64_t)server_time_ms;

    /* Adjust offset to eliminate delta */
    timer->offset_ms -= delta;
}

int32_t cwnet_timer_read_synced_ms(const cwnet_timer_t *timer,
                                    int32_t local_time_ms) {
    if (timer == NULL) {
        return 0;
    }

    int64_t synced = (int64_t)local_time_ms + timer->offset_ms;

    /* Wrap to positive 31-bit range (protocol uses modulo 2^31) */
    /* This matches the official implementation */
    return (int32_t)(synced % 2147483647LL);
}

/*===========================================================================*/
/* PING Parsing                                                              */
/*===========================================================================*/

bool cwnet_ping_parse(cwnet_ping_t *ping,
                      const uint8_t *payload,
                      size_t len) {
    if (ping == NULL || payload == NULL || len < CWNET_PING_PAYLOAD_SIZE) {
        return false;
    }

    ping->type = (cwnet_ping_type_t)payload[0];
    ping->id = payload[1];
    /* bytes 2-3 are reserved */

    /* Parse timestamps (little-endian, signed 32-bit) */
    ping->t0_ms = (int32_t)((uint32_t)payload[4] |
                            ((uint32_t)payload[5] << 8) |
                            ((uint32_t)payload[6] << 16) |
                            ((uint32_t)payload[7] << 24));

    ping->t1_ms = (int32_t)((uint32_t)payload[8] |
                            ((uint32_t)payload[9] << 8) |
                            ((uint32_t)payload[10] << 16) |
                            ((uint32_t)payload[11] << 24));

    ping->t2_ms = (int32_t)((uint32_t)payload[12] |
                            ((uint32_t)payload[13] << 8) |
                            ((uint32_t)payload[14] << 16) |
                            ((uint32_t)payload[15] << 24));

    return true;
}

/*===========================================================================*/
/* PING Building                                                             */
/*===========================================================================*/

bool cwnet_ping_build_response(const cwnet_ping_t *request,
                                uint8_t *buffer,
                                size_t buf_len,
                                int32_t our_time_ms) {
    if (request == NULL || buffer == NULL || buf_len < CWNET_PING_PAYLOAD_SIZE) {
        return false;
    }

    memset(buffer, 0, CWNET_PING_PAYLOAD_SIZE);

    /* Type = RESPONSE_1 */
    buffer[0] = (uint8_t)CWNET_PING_RESPONSE_1;

    /* Preserve ID */
    buffer[1] = request->id;

    /* Reserved bytes 2-3 stay zero */

    /* t0 = preserve from request (little-endian) */
    uint32_t t0 = (uint32_t)request->t0_ms;
    buffer[4] = (uint8_t)(t0 & 0xFF);
    buffer[5] = (uint8_t)((t0 >> 8) & 0xFF);
    buffer[6] = (uint8_t)((t0 >> 16) & 0xFF);
    buffer[7] = (uint8_t)((t0 >> 24) & 0xFF);

    /* t1 = our time (little-endian) */
    uint32_t t1 = (uint32_t)our_time_ms;
    buffer[8] = (uint8_t)(t1 & 0xFF);
    buffer[9] = (uint8_t)((t1 >> 8) & 0xFF);
    buffer[10] = (uint8_t)((t1 >> 16) & 0xFF);
    buffer[11] = (uint8_t)((t1 >> 24) & 0xFF);

    /* t2 = 0 (will be filled by server) */
    /* Already zeroed by memset */

    return true;
}

/*===========================================================================*/
/* Latency Calculation                                                       */
/*===========================================================================*/

int32_t cwnet_ping_calc_latency(const cwnet_ping_t *response) {
    if (response == NULL) {
        return -1;
    }

    /* Can only calculate from RESPONSE_2 */
    if (response->type != CWNET_PING_RESPONSE_2) {
        return -1;
    }

    /* RTT = t2 - t0 */
    return response->t2_ms - response->t0_ms;
}
