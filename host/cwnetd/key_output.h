/**
 * @file key_output.h
 * @brief Where the played keying actually goes: key and PTT behind one seam.
 *
 * R11: "Tasto e PTT passano da un'interfaccia di uscita con un backend
 * virtuale che scrive ogni fronte come riga con l'istante in ms su un
 * descrittore proprio (file o stderr) che non scarta mai, separato dalle
 * righe di stato."
 *
 * KTD9 puts the physical transport behind its own Decision, which is not
 * open yet, so the only backend here is the virtual one: every edge becomes
 * a line on the descriptor the daemon chose with --edges. A serial-line or
 * GPIO backend later fills in the same two function pointers and nothing
 * above this file changes.
 *
 * The instant carried by an edge is the instant it was *scheduled* for, not
 * the instant the daemon noticed it: cwnet_play computes deadlines from the
 * waits the sender encoded, and that is what the line has to show for the
 * jitter measurement of the Success Criteria to mean anything.
 *
 * Two rules a backend must keep, both of them the FAULT philosophy applied
 * to a transmitter:
 *   - the rest state is key up and PTT off, and key_output_release() must
 *     reach it from any state;
 *   - an edge that does not change the state is not an edge: it is dropped
 *     here, so a backend never sees a redundant write and the trace is a
 *     clean list of transitions.
 */
#ifndef HOST_CWNETD_KEY_OUTPUT_H
#define HOST_CWNETD_KEY_OUTPUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A backend's own text output. The daemon owns the descriptor, so a backend
 * never writes to one directly: it hands a finished line to this. The line
 * carries no newline.
 *
 * Unlike the status lines, this sink does not drop: the jitter measurement
 * reads these lines, and a measurement that silently loses the edges it
 * measures is worse than no measurement (R11, KTD9). What that costs, and
 * what the daemon does about it, is main.c's edge_write().
 */
typedef void (*key_output_line_fn)(void *ctx, const char *line);

typedef struct key_output {
    const char *name;         /**< Backend name, as given to key_output_open() */
    bool key_down;            /**< Key state as this interface last set it */
    bool ptt_on;              /**< PTT state as this interface last set it */

    key_output_line_fn line;  /**< Where a backend's lines go; never NULL */
    void *line_ctx;

    /** Backend hooks. Called only on an actual transition. */
    void (*apply_key)(struct key_output *out, bool down, int64_t at_ms);
    void (*apply_ptt)(struct key_output *out, bool on, int64_t at_ms);
} key_output_t;

/** Comma-separated list of the backends key_output_open() accepts, for --help. */
const char *key_output_backends(void);

/**
 * @brief Wire up an output. Starts at rest: key up, PTT off.
 *
 * @param out      Interface, not NULL
 * @param backend  Backend name; see key_output_backends()
 * @param line     Line sink, not NULL
 * @param line_ctx Passed back to @p line
 * @return false on an unknown backend or a NULL argument; nothing is written.
 */
bool key_output_open(key_output_t *out, const char *backend,
                     key_output_line_fn line, void *line_ctx);

/** @brief Key edge at the instant it was scheduled for. No-op if unchanged. */
void key_output_set_key(key_output_t *out, bool down, int64_t at_ms);

/** @brief PTT edge at the instant it was scheduled for. No-op if unchanged. */
void key_output_set_ptt(key_output_t *out, bool on, int64_t at_ms);

/**
 * @brief Back to rest: key up first, then PTT off.
 *
 * The order is the one a transmitter wants — never leave the key down while
 * dropping the PTT. Idempotent, so a signal handler's path and the normal
 * path can both call it.
 */
void key_output_release(key_output_t *out, int64_t at_ms);

/** @brief Release and let the backend go. The interface is unusable after. */
void key_output_close(key_output_t *out, int64_t at_ms);

#ifdef __cplusplus
}
#endif

#endif /* HOST_CWNETD_KEY_OUTPUT_H */
