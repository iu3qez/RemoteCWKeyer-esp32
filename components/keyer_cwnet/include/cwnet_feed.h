/**
 * @file cwnet_feed.h
 * @brief Feeds the CWNet client from the keying stream
 *
 * A best-effort consumer on the keying stream that turns local_key edges
 * into cwnet_client_send_key_event() calls stamped with *stream time*: the
 * tick the edge happened on Core 0, reconstructed by counting one tick per
 * sample and the tick count of every silence marker. The 7-bit waits on the
 * wire are then tick distances, whatever the background loop's jitter and
 * however many samples one drain finds waiting.
 *
 * The stream carries no entry while nothing changes, so stream time stands
 * still on an idle key. The end of an over is judged on stream time aged by
 * the caller's clock since the last tick consumed. With the clock sampled
 * right before the call, the entries consumed were produced before it, so
 * the aged value never runs ahead of stream time and the end of the over
 * fires at most one drain period late, inside the 16 ms bucket the
 * reference itself encodes it in.
 *
 * When the ticks are no longer trustworthy (a stream overrun) or the wire
 * may disagree with the client (a send failed), corrupted timing is worse
 * than silence (ARCHITECTURE.md 8.1): the over on the wire is closed at
 * once and the next edge opens a fresh one with wait 0.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "stream.h"
#include "consumer.h"
#include "cwnet_client.h"

/** Edges sent in one pass at most; the rest of a backlog waits for the next */
#define CWNET_FEED_MAX_EDGES_PER_PASS 64

typedef struct {
    best_effort_consumer_t consumer;
    uint32_t stream_ms;     /**< Ticks consumed: stream time of the last entry seen */
    int64_t aged_at_us;     /**< Caller clock when stream_ms last advanced */
    size_t dropped_seen;    /**< consumer.dropped at the last check */
    uint8_t key;            /**< local_key of the last sample seen */
    bool wire_stuck;        /**< The over on the wire could not be closed yet */
} cwnet_feed_t;

/** What one cwnet_feed_process() call put on the wire, for logging */
typedef struct {
    uint16_t edges;         /**< Key transitions sent */
    bool end_of_over;       /**< The quiet-over second key-up was sent */
    bool aborted;           /**< The over on the wire was closed by force */
    bool stuck;             /**< It had to be and could not: retried next pass */
} cwnet_feed_result_t;

/**
 * @brief Start consuming at the stream's current position
 *
 * @param feed Feed context
 * @param stream Keying stream (producer: rt_task on Core 0)
 * @param now_us Caller clock, same one later passed to process()
 */
void cwnet_feed_init(cwnet_feed_t *feed, const keying_stream_t *stream, int64_t now_us);

/**
 * @brief Drain the stream into the client, then close a quiet over
 *
 * Call periodically from the background loop, in every socket state: the
 * stream is drained whether or not the client can send, so a client that
 * becomes READY does not replay stale edges. When it cannot send, edges are
 * consumed and dropped.
 *
 * @param feed Feed context
 * @param client CWNet client
 * @param now_us Caller clock (esp_timer on target), sampled immediately
 *               before this call; used only to age stream time while the
 *               stream is idle
 * @param dot_ms Local dot time; the end of an over is 14 dot-times of key-up.
 *               0 or less disables the end-of-over check.
 * @return What was sent
 */
cwnet_feed_result_t cwnet_feed_process(cwnet_feed_t *feed,
                                       cwnet_client_t *client,
                                       int64_t now_us,
                                       int32_t dot_ms);

/**
 * @brief Stream time of the last entry consumed, in ticks (ms)
 */
static inline uint32_t cwnet_feed_stream_ms(const cwnet_feed_t *feed) {
    return feed->stream_ms;
}
