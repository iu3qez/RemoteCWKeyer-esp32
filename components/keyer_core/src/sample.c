/**
 * @file sample.c
 * @brief Stream sample implementation
 */

#include "sample.h"

stream_sample_t sample_with_edges_from(stream_sample_t current,
                                       const stream_sample_t *previous) {
    uint8_t flags = current.flags;

    /* Check for GPIO edge */
    if (current.gpio.bits != previous->gpio.bits) {
        flags |= FLAG_GPIO_EDGE;
    }

    /* Check for local key edge */
    if (current.local_key != previous->local_key) {
        flags |= FLAG_LOCAL_EDGE;
    }

    current.flags = flags;
    return current;
}
