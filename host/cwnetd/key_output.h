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
#include <stddef.h>
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

/*---------------------------------------------------------------------------*/
/* Configuration (plan 2026-09-24-2143, KTD7)                                */
/*---------------------------------------------------------------------------*/

/** Default line of each function: DTR = key, RTS = PTT (N1MM, DL4YHF). */
#define KEY_OUTPUT_DEFAULT_KEY_LINE "dtr"
#define KEY_OUTPUT_DEFAULT_PTT_LINE "rts"

/** Room for any message key_output_check() writes. */
#define KEY_OUTPUT_ERR_LEN 160

/**
 * What the operator asked for, as given on the command line. The strings
 * are not copied and must outlive the output. A NULL line takes its default,
 * so main.c can pass a flag that was never given as it is. The serial fields
 * are read only when the backend is "serial".
 */
typedef struct key_output_cfg {
    const char *backend;   /**< "virtual" or "serial" */
    const char *device;    /**< --serial: the port, e.g. /dev/cu.usbserial-X */
    const char *key_line;  /**< --key-line: dtr, rts, dtr-inv, rts-inv */
    const char *ptt_line;  /**< --ptt-line: the same, or none */
} key_output_cfg_t;

/**
 * Which modem-control line carries each function, as TIOCM_DTR / TIOCM_RTS
 * bits. A zero bit is a function on no line. An inverted line is low when
 * its function is active.
 */
typedef struct key_output_map {
    unsigned key_bit;
    bool key_inverted;
    unsigned ptt_bit;
    bool ptt_inverted;
} key_output_map_t;

/**
 * @brief Validate a configuration and resolve its line mapping.
 *
 * Touches no device. Refuses: an unknown backend; a serial backend without
 * a device; an unknown line value; the key on no line; key and PTT on the
 * same line, inverted or not.
 *
 * @param cfg     Configuration, not NULL
 * @param map     Filled on success; all zero for the virtual backend
 * @param err     On failure, a message that names the flag and the value
 * @param err_len Size of @p err; KEY_OUTPUT_ERR_LEN holds any message
 * @return true when the configuration can be opened
 */
bool key_output_check(const key_output_cfg_t *cfg, key_output_map_t *map,
                      char *err, size_t err_len);

/**
 * @brief The modem-control lines to hold high for a function state.
 *
 * Pure. Rest is (false, false). A line no function uses is never in the
 * result, so it stays low.
 *
 * @return a TIOCM_DTR / TIOCM_RTS mask of the lines that are high
 */
unsigned key_output_levels(const key_output_map_t *map, bool key_down, bool ptt_on);

/**
 * @brief The start-up warning for a serial output run as root, or NULL.
 *
 * The exclusive lock on the port does not stop root: Linux lets
 * CAP_SYS_ADMIN past TIOCEXCL, XNU lets the superuser past it, and flock is
 * advisory. A second cwnetd started as root then opens the port under a
 * running one, and the open raises DTR and RTS. The maintainer chose a
 * warning over a refusal (plan 2026-09-24-2143, Key Decisions).
 *
 * @param backend The --output value
 * @param euid    The effective uid, as geteuid() returns it
 */
const char *key_output_root_warning(const char *backend, unsigned long euid);

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
