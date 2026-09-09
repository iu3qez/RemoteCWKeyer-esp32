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

/**
 * @brief Write a 31-bit wire timestamp little-endian, as the reference does
 */
static void put_le32(uint8_t *dst, int32_t value) {
    uint32_t v = (uint32_t)value;
    dst[0] = (uint8_t)(v & 0xFFu);
    dst[1] = (uint8_t)((v >> 8) & 0xFFu);
    dst[2] = (uint8_t)((v >> 16) & 0xFFu);
    dst[3] = (uint8_t)((v >> 24) & 0xFFu);
}

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
    put_le32(&buffer[4], request->t0_ms);

    /* t1 = our time (little-endian) */
    put_le32(&buffer[8], our_time_ms);

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

/*===========================================================================*/
/* PING Building: initiator side                                            */
/*===========================================================================*/

bool cwnet_ping_build_request(uint8_t id,
                               int32_t t0_ms,
                               uint8_t *buffer,
                               size_t buf_len) {
    if (buffer == NULL || buf_len < CWNET_PING_PAYLOAD_SIZE) {
        return false;
    }

    memset(buffer, 0, CWNET_PING_PAYLOAD_SIZE);

    /* Type = REQUEST */
    buffer[0] = (uint8_t)CWNET_PING_REQUEST;

    /* id (CwNet.c:2256: the client's index) */
    buffer[1] = id;

    /* Reserved bytes 2-3 stay zero */

    /* t0 (little-endian) */
    put_le32(&buffer[4], t0_ms);

    /* t1, t2 slots (bytes 8-15) stay zero, already zeroed by memset */

    return true;
}

bool cwnet_ping_build_response2(const cwnet_ping_t *response_1,
                                 uint8_t *buffer,
                                 size_t buf_len,
                                 int32_t our_time_ms) {
    if (response_1 == NULL || buffer == NULL || buf_len < CWNET_PING_PAYLOAD_SIZE) {
        return false;
    }

    /* Can only build a RESPONSE_2 from a RESPONSE_1 */
    if (response_1->type != CWNET_PING_RESPONSE_1) {
        return false;
    }

    memset(buffer, 0, CWNET_PING_PAYLOAD_SIZE);

    /* Type = RESPONSE_2 */
    buffer[0] = (uint8_t)CWNET_PING_RESPONSE_2;

    /* Preserve ID */
    buffer[1] = response_1->id;

    /* Reserved bytes 2-3 stay zero */

    /* t0 = preserve from the RESPONSE_1 (little-endian) */
    put_le32(&buffer[4], response_1->t0_ms);

    /* t1 = preserve from the RESPONSE_1 (little-endian) */
    put_le32(&buffer[8], response_1->t1_ms);

    /* t2 = our time (little-endian) */
    put_le32(&buffer[12], our_time_ms);

    return true;
}

/*===========================================================================*/
/* Peak-hold                                                                 */
/*===========================================================================*/

/* CwNet.c:1437: "realistic" window, in microseconds there (0 <= i32 <
 * 2000000); here it gates the wire-computed millisecond latency directly. */
#define CWNET_PING_LATENCY_GATE_MIN_MS 0
#define CWNET_PING_LATENCY_GATE_MAX_MS 2000

bool cwnet_ping_peak_hold_update(int32_t *peak_ms, int32_t latency_ms) {
    if (peak_ms == NULL) {
        return false;
    }

    if (latency_ms < CWNET_PING_LATENCY_GATE_MIN_MS ||
        latency_ms >= CWNET_PING_LATENCY_GATE_MAX_MS) {
        return false;
    }

    /* CwNet.c:1442-1447: jump to a new peak, otherwise drop by a tenth of
     * the gap. Integer division: a gap under 10 ms no longer descends. */
    if (*peak_ms < 0 || latency_ms >= *peak_ms) {
        *peak_ms = latency_ms;
    } else {
        *peak_ms -= (*peak_ms - latency_ms) / 10;
    }

    return true;
}
