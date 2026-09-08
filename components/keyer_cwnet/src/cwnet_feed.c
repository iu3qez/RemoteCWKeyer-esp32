/**
 * @file cwnet_feed.c
 * @brief Feeds the CWNet client from the keying stream
 */

#include "cwnet_feed.h"
#include <string.h>

void cwnet_feed_init(cwnet_feed_t *feed, const keying_stream_t *stream, int64_t now_us) {
    if (feed == NULL) {
        return;
    }
    memset(feed, 0, sizeof(*feed));
    if (stream == NULL) {
        return;  /* consumer.stream stays NULL: process() does nothing */
    }
    /* skip_threshold 0: skip only on a real overrun, never on mere lag */
    best_effort_consumer_init(&feed->consumer, stream, 0);
    feed->aged_at_us = now_us;
}

/**
 * @brief Close whatever is on the wire, now
 *
 * If the client cannot (send failed) the wire stays marked stuck and every
 * later pass retries before anything else goes out.
 */
static void close_wire(cwnet_feed_t *feed, cwnet_client_t *client, cwnet_feed_result_t *result) {
    bool open = cwnet_client_key_on_wire(client) || cwnet_client_over_open(client) ||
                cwnet_client_ptt_on_wire(client);
    if (cwnet_client_abort_over(client)) {
        feed->wire_stuck = false;
        if (open) {
            result->aborted = true;
        }
    } else {
        feed->wire_stuck = true;
        result->stuck = true;
    }
}

/**
 * @brief React to the consumer having skipped entries
 *
 * The ticks in between are lost: the wait of anything sent from here would
 * be wrong, so the over is closed on the wire. The wire is key-up after
 * that; if the key is still down, the next sample that carries state opens
 * a new over with wait 0.
 */
static void note_skip(cwnet_feed_t *feed, cwnet_client_t *client, cwnet_feed_result_t *result) {
    if (feed->consumer.dropped != feed->dropped_seen) {
        feed->dropped_seen = feed->consumer.dropped;
        close_wire(feed, client, result);
        feed->key = 0;
    }
}

static void send_edge(cwnet_feed_t *feed, cwnet_client_t *client, uint8_t key,
                      cwnet_feed_result_t *result) {
    /* The client answers OK also when the wire already shows this state (a
     * key-up at the start of a session): count only what changed. */
    bool before = cwnet_client_key_on_wire(client);
    cwnet_client_err_t err = cwnet_client_send_key_event(client, key != 0,
                                                         (int32_t)feed->stream_ms);
    if (err == CWNET_CLIENT_OK) {
        if (cwnet_client_key_on_wire(client) != before) {
            result->edges++;
        }
    } else if (err == CWNET_CLIENT_ERR_SEND_FAILED) {
        /* The wire may hold a key-down that will never get its key-up:
         * close the over rather than key on from a state the server does
         * not share. */
        close_wire(feed, client, result);
    }
    /* NOT_READY / NOT_PERMITTED: no session to key; the edge is consumed,
     * not queued, so nothing stale is replayed later. */
}

cwnet_feed_result_t cwnet_feed_process(cwnet_feed_t *feed,
                                       cwnet_client_t *client,
                                       int64_t now_us,
                                       int32_t dot_ms) {
    cwnet_feed_result_t result = {0};
    if (feed == NULL || client == NULL || feed->consumer.stream == NULL) {
        return result;
    }
    bool ptt_before = cwnet_client_ptt_on_wire(client);

    /* A previous pass could not close the over on the wire: nothing else
     * goes out before it is closed. */
    if (feed->wire_stuck) {
        close_wire(feed, client, &result);
    }

    stream_sample_t sample;
    bool advanced = false;

    /* Bounded per pass: after a stall, the backlog goes out over several
     * passes rather than as one burst that fills the socket. Stream time is
     * exact either way. */
    while (result.edges < CWNET_FEED_MAX_EDGES_PER_PASS &&
           best_effort_consumer_tick(&feed->consumer, &sample)) {
        note_skip(feed, client, &result);

        if (sample_is_silence(&sample)) {
            feed->stream_ms += sample_silence_ticks(&sample);
        } else {
            feed->stream_ms += 1;
            if (sample.local_key != feed->key) {
                feed->key = sample.local_key;
                if (!feed->wire_stuck) {
                    send_edge(feed, client, sample.local_key, &result);
                }
            }
        }
        advanced = true;
    }
    /* The consumer also skips, without returning an entry, when the slot it
     * was about to read has been overwritten. */
    note_skip(feed, client, &result);

    if (advanced) {
        feed->aged_at_us = now_us;
    }

    if (dot_ms > 0 && !feed->wire_stuck) {
        /* Stream time stands still on an idle key; age it by the caller's
         * clock since the last tick consumed. */
        int64_t idle_us = now_us - feed->aged_at_us;
        uint32_t idle_ms = idle_us > 0 ? (uint32_t)(idle_us / 1000) : 0u;
        int32_t now_ms = (int32_t)(feed->stream_ms + idle_ms);
        if (cwnet_client_poll(client, now_ms, dot_ms)) {
            result.end_of_over = true;
        }
    }

    bool ptt_after = cwnet_client_ptt_on_wire(client);
    result.ptt_on = ptt_after && !ptt_before;
    result.ptt_off = !ptt_after && ptt_before;
    return result;
}
