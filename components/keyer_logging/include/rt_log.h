/**
 * @file rt_log.h
 * @brief RT-safe non-blocking logging system
 *
 * Lock-free log stream with ~100-200ns push latency.
 * UART drain task runs on Core 1.
 *
 * ARCHITECTURE.md compliance:
 * - RULE 3.1.4: No operation shall block
 * - Uses lock-free ring buffer for log entries
 */

#ifndef KEYER_RT_LOG_H
#define KEYER_RT_LOG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration
 * ============================================================================ */

/** Maximum message length (truncated if exceeded) */
#define LOG_MAX_MSG_LEN 120

/** Log buffer size (number of entries, must be power of 2) */
#define LOG_BUFFER_SIZE 256

/* ============================================================================
 * Types
 * ============================================================================ */

/**
 * @brief Log level
 */
typedef enum {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN = 1,
    LOG_LEVEL_INFO = 2,
    LOG_LEVEL_DEBUG = 3,
    LOG_LEVEL_TRACE = 4,
} log_level_t;

/**
 * @brief Log entry
 */
typedef struct {
    int64_t timestamp_us;              /**< Timestamp in microseconds */
    log_level_t level;                 /**< Log level */
    uint8_t len;                       /**< Message length */
    char msg[LOG_MAX_MSG_LEN];         /**< Message buffer */
} log_entry_t;

/**
 * @brief Lock-free log stream
 */
typedef struct {
    log_entry_t entries[LOG_BUFFER_SIZE]; /**< Entry ring buffer */
    atomic_uint write_idx;                 /**< Producer write index */
    atomic_uint read_idx;                  /**< Consumer read index */
    atomic_uint dropped;                   /**< Dropped message counter */
} log_stream_t;

/**
 * @brief Static initializer for log_stream_t
 */
#define LOG_STREAM_INIT { \
    .write_idx = ATOMIC_VAR_INIT(0), \
    .read_idx = ATOMIC_VAR_INIT(0), \
    .dropped = ATOMIC_VAR_INIT(0) \
}

/* ============================================================================
 * Global Instances
 * ============================================================================ */

/** RT log stream (Core 0, high priority) */
extern log_stream_t g_rt_log_stream;

/** Background log stream (Core 1, low priority) */
extern log_stream_t g_bg_log_stream;

/* ============================================================================
 * Functions
 * ============================================================================ */

/**
 * @brief Initialize log stream
 * @param stream Stream to initialize
 */
void log_stream_init(log_stream_t *stream);

/**
 * @brief Push log entry (RT-safe, non-blocking)
 *
 * Timing: ~100-200ns. Never blocks. If buffer full, increments dropped counter.
 *
 * @param stream Stream to push to
 * @param timestamp_us Timestamp in microseconds
 * @param level Log level
 * @param msg Message string
 * @param len Message length (max LOG_MAX_MSG_LEN)
 * @return true if pushed, false if dropped
 */
bool log_stream_push(log_stream_t *stream, int64_t timestamp_us,
                     log_level_t level, const char *msg, size_t len);

/**
 * @brief Drain log entry (consumer side)
 *
 * @param stream Stream to drain from
 * @param out Output entry (written on success)
 * @return true if entry available
 */
bool log_stream_drain(log_stream_t *stream, log_entry_t *out);

/**
 * @brief Get dropped message count
 * @param stream Stream
 * @return Number of dropped messages
 */
uint32_t log_stream_dropped(const log_stream_t *stream);

/**
 * @brief Check if stream has entries
 * @param stream Stream
 * @return true if entries available
 */
bool log_stream_has_entries(const log_stream_t *stream);

/**
 * @brief Reset dropped counter
 * @param stream Stream
 */
void log_stream_reset_dropped(log_stream_t *stream);

/**
 * @brief Get log level name
 * @param level Log level
 * @return Level name string
 */
const char *log_level_str(log_level_t level);

/* ============================================================================
 * RT-Safe Logging Macros
 * ============================================================================ */

/**
 * @brief RT-safe log macro (internal)
 *
 * Uses snprintf to format message, then pushes to stream.
 */
#define RT_LOG(stream, level, ts, fmt, ...) do { \
    char _rt_log_buf[LOG_MAX_MSG_LEN]; \
    int _rt_log_len = snprintf(_rt_log_buf, sizeof(_rt_log_buf), fmt, ##__VA_ARGS__); \
    if (_rt_log_len > 0) { \
        log_stream_push((stream), (ts), (level), _rt_log_buf, \
            (_rt_log_len > LOG_MAX_MSG_LEN) ? LOG_MAX_MSG_LEN : (size_t)_rt_log_len); \
    } \
} while(0)

/** Log error (critical) */
#define RT_ERROR(stream, ts, fmt, ...) \
    RT_LOG(stream, LOG_LEVEL_ERROR, ts, fmt, ##__VA_ARGS__)

/** Log warning */
#define RT_WARN(stream, ts, fmt, ...) \
    RT_LOG(stream, LOG_LEVEL_WARN, ts, fmt, ##__VA_ARGS__)

/** Log info (normal events) */
#define RT_INFO(stream, ts, fmt, ...) \
    RT_LOG(stream, LOG_LEVEL_INFO, ts, fmt, ##__VA_ARGS__)

/** Log debug */
#define RT_DEBUG(stream, ts, fmt, ...) \
    RT_LOG(stream, LOG_LEVEL_DEBUG, ts, fmt, ##__VA_ARGS__)

/** Log trace (verbose) */
#define RT_TRACE(stream, ts, fmt, ...) \
    RT_LOG(stream, LOG_LEVEL_TRACE, ts, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* KEYER_RT_LOG_H */
