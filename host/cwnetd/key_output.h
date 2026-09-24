/**
 * @file key_output.h
 * @brief Where the played keying actually goes: key and PTT behind one seam.
 *
 * R11: "Tasto e PTT passano da un'interfaccia di uscita con un backend
 * virtuale che scrive ogni fronte come riga con l'istante in ms su un
 * descrittore proprio (file o stderr) che non scarta mai, separato dalle
 * righe di stato."
 *
 * Two backends fill in the same two function pointers. "virtual" writes
 * every edge as a line on the descriptor the daemon chose with --edges.
 * "serial" (key_output_serial.c, decision #98) drives the key and PTT on a
 * serial port's DTR and RTS, then writes the same line, so the trace does
 * not depend on the backend.
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
 * An output failure (KTD4). Once `set`, the backend makes no further line
 * call, and the lines are released only by closing the descriptor. The
 * daemon reads it after each batch of edges and stops with a FAULT.
 */
typedef enum key_output_fault_kind {
    KEY_OUTPUT_FAULT_CALL,    /**< An OS call returned an error */
    KEY_OUTPUT_FAULT_SLOW,    /**< An OS call succeeded, over the time allowed */
    KEY_OUTPUT_FAULT_HANGUP,  /**< The poll reported the device hung up */
    KEY_OUTPUT_FAULT_READ,    /**< Reading the device hit end of file or an error */
} key_output_fault_kind_t;

typedef struct key_output_fault {
    bool set;
    key_output_fault_kind_t kind;
    const char *call;      /**< The OS call, for the message, e.g. "TIOCMSET" */
    int err;               /**< Its errno; 0 for SLOW, HANGUP, and READ at end of file */
    int64_t duration_us;   /**< How long it took */
} key_output_fault_t;

/** Durations of the line changes, for the snapshot. All zero for virtual. */
typedef struct key_output_timing {
    unsigned long count;   /**< Line changes made */
    int64_t max_us;        /**< The slowest */
    unsigned long slow;    /**< Over KEY_OUTPUT_SERIAL_SLOW_US */
} key_output_timing_t;

struct key_output_os;

typedef struct key_output {
    const char *name;         /**< Backend name, as given to key_output_open() */
    bool key_down;            /**< Key state as this interface last set it */
    bool ptt_on;              /**< PTT state as this interface last set it */

    key_output_line_fn line;  /**< Where a backend's lines go; never NULL */
    void *line_ctx;

    /** Backend hooks. Called only on an actual transition. */
    void (*apply_key)(struct key_output *out, bool down, int64_t at_ms);
    void (*apply_ptt)(struct key_output *out, bool on, int64_t at_ms);
    /** Backend teardown after the release; NULL when there is none. */
    void (*finish)(struct key_output *out);
    /** key_output_service(); NULL for a backend with nothing to poll. */
    void (*service)(struct key_output *out, bool hangup);

    key_output_fault_t fault;
    key_output_timing_t timing;

    /* Serial backend only. */
    int fd;                            /**< -1 when closed */
    key_output_map_t map;
    const struct key_output_os *os;
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
#define KEY_OUTPUT_ERR_LEN 256

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
 * For "serial" this opens the port and leaves both lines at rest, in the
 * order key_output_serial.h describes.
 *
 * @param out      Interface, not NULL
 * @param cfg      Configuration; refused as key_output_check() refuses it
 * @param line     Line sink, not NULL
 * @param line_ctx Passed back to @p line
 * @param err      On failure, why; not NULL
 * @param err_len  Size of @p err; KEY_OUTPUT_ERR_LEN holds any message
 * @return false on a refused configuration, a NULL argument or a port that
 *         did not open at rest; nothing is written to @p line.
 */
bool key_output_open(key_output_t *out, const key_output_cfg_t *cfg,
                     key_output_line_fn line, void *line_ctx,
                     char *err, size_t err_len);

/** @brief The output's failure record, or NULL when it has none (KTD4). */
const key_output_fault_t *key_output_fault(const key_output_t *out);

/**
 * @brief The descriptor the loop polls for input and hang-up, or -1.
 *
 * The serial port, while it is open. The loop asks for input, not for
 * nothing: XNU's poll() watches only the events requested
 * (sys_generic.c:1750), so with none an unplug would never be reported.
 */
int key_output_poll_fd(const key_output_t *out);

/**
 * @brief The loop's poll found key_output_poll_fd() ready.
 *
 * A hang-up is the device gone, and is recorded as a failure. Otherwise the
 * bytes the device sent are read and dropped, a bounded number per call;
 * an end of file or an error on that read is the device gone too (R7).
 * Makes no line call.
 *
 * @param hangup The poll reported POLLHUP, POLLERR or POLLNVAL
 */
void key_output_service(key_output_t *out, bool hangup);

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
