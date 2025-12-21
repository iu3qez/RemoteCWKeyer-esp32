/**
 * @file log_stream.c
 * @brief Lock-free log stream implementation
 */

#include "rt_log.h"
#include <string.h>

/* Global log stream instances */
log_stream_t g_rt_log_stream = LOG_STREAM_INIT;
log_stream_t g_bg_log_stream = LOG_STREAM_INIT;

void log_stream_init(log_stream_t *stream) {
    atomic_init(&stream->write_idx, 0);
    atomic_init(&stream->read_idx, 0);
    atomic_init(&stream->dropped, 0);
    memset(stream->entries, 0, sizeof(stream->entries));
}

bool log_stream_push(log_stream_t *stream, int64_t timestamp_us,
                     log_level_t level, const char *msg, size_t len) {
    /* Check if buffer is full */
    uint32_t write = atomic_load_explicit(&stream->write_idx, memory_order_relaxed);
    uint32_t read = atomic_load_explicit(&stream->read_idx, memory_order_acquire);

    if (write - read >= LOG_BUFFER_SIZE) {
        /* Buffer full - drop message */
        atomic_fetch_add_explicit(&stream->dropped, 1, memory_order_relaxed);
        return false;
    }

    /* Get slot */
    uint32_t slot = write & (LOG_BUFFER_SIZE - 1);
    log_entry_t *entry = &stream->entries[slot];

    /* Fill entry */
    entry->timestamp_us = timestamp_us;
    entry->level = level;

    /* Copy message (truncate if needed) */
    size_t copy_len = (len > LOG_MAX_MSG_LEN) ? LOG_MAX_MSG_LEN : len;
    memcpy(entry->msg, msg, copy_len);
    entry->len = (uint8_t)copy_len;

    /* Publish entry */
    atomic_store_explicit(&stream->write_idx, write + 1, memory_order_release);

    return true;
}

bool log_stream_drain(log_stream_t *stream, log_entry_t *out) {
    uint32_t read = atomic_load_explicit(&stream->read_idx, memory_order_relaxed);
    uint32_t write = atomic_load_explicit(&stream->write_idx, memory_order_acquire);

    if (read == write) {
        /* Empty */
        return false;
    }

    /* Get slot */
    uint32_t slot = read & (LOG_BUFFER_SIZE - 1);

    /* Copy entry */
    *out = stream->entries[slot];

    /* Advance read index */
    atomic_store_explicit(&stream->read_idx, read + 1, memory_order_release);

    return true;
}

uint32_t log_stream_dropped(const log_stream_t *stream) {
    return atomic_load_explicit(&stream->dropped, memory_order_relaxed);
}

bool log_stream_has_entries(const log_stream_t *stream) {
    uint32_t read = atomic_load_explicit(&stream->read_idx, memory_order_relaxed);
    uint32_t write = atomic_load_explicit(&stream->write_idx, memory_order_acquire);
    return read != write;
}

void log_stream_reset_dropped(log_stream_t *stream) {
    atomic_store_explicit(&stream->dropped, 0, memory_order_relaxed);
}

const char *log_level_str(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_TRACE: return "TRACE";
        default:              return "?????";
    }
}
