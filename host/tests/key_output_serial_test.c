/**
 * @file key_output_serial_test.c
 * @brief The serial output of cwnetd: line mapping and configuration (U1),
 *        and the backend's calls to the OS (U2).
 *
 * What a real adapter does with these levels is the bench's job (U6). This
 * file proves what the daemon asks for: which line goes high for which
 * function, which configurations it refuses before a port is touched, and
 * in what order the backend calls the OS. The OS is the table of KTD8,
 * replaced here by one that records every call: a pty cannot stand in,
 * because Linux ptys refuse the modem-line ioctls.
 */
#include "../cwnetd/key_output.h"
#include "../cwnetd/key_output_serial.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <termios.h>

/*===========================================================================*/
/* Helpers                                                                   */
/*===========================================================================*/

static key_output_cfg_t serial_cfg(const char *key_line, const char *ptt_line) {
    key_output_cfg_t cfg = {
        .backend  = "serial",
        .device   = "/dev/ttyUSB0",
        .key_line = key_line,
        .ptt_line = ptt_line,
    };
    return cfg;
}

/* The levels for one function state, or ~0u when the check refuses cfg. */
static unsigned levels_for(const key_output_cfg_t *cfg, bool key_down, bool ptt_on) {
    key_output_map_t map;
    char err[KEY_OUTPUT_ERR_LEN];
    if (!key_output_check(cfg, &map, err, sizeof(err))) {
        fprintf(stderr, "  check refused: %s\n", err);
        return ~0u;
    }
    return key_output_levels(&map, key_down, ptt_on);
}

static bool expect_levels(const char *what, unsigned got, unsigned want) {
    if (got != want) {
        fprintf(stderr, "  %s: levels 0x%x, want 0x%x\n", what, got, want);
        return false;
    }
    return true;
}

/* The check refuses cfg, and its message contains every string in want. */
static bool expect_refused(const key_output_cfg_t *cfg, const char *const *want, size_t n) {
    key_output_map_t map;
    char err[KEY_OUTPUT_ERR_LEN];
    err[0] = '\0';
    if (key_output_check(cfg, &map, err, sizeof(err))) {
        fprintf(stderr, "  accepted, should have been refused\n");
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (strstr(err, want[i]) == NULL) {
            fprintf(stderr, "  message \"%s\" does not name \"%s\"\n", err, want[i]);
            return false;
        }
    }
    return true;
}

/*===========================================================================*/
/* Mapping                                                                   */
/*===========================================================================*/

/* R2: DTR = key, RTS = PTT, asserted = active. */
static bool test_default_mapping(void) {
    key_output_cfg_t cfg = serial_cfg(KEY_OUTPUT_DEFAULT_KEY_LINE, KEY_OUTPUT_DEFAULT_PTT_LINE);
    return expect_levels("rest", levels_for(&cfg, false, false), 0u) &&
           expect_levels("key down", levels_for(&cfg, true, false), (unsigned)TIOCM_DTR) &&
           expect_levels("ptt on", levels_for(&cfg, false, true), (unsigned)TIOCM_RTS) &&
           expect_levels("both", levels_for(&cfg, true, true),
                         (unsigned)(TIOCM_DTR | TIOCM_RTS));
}

static bool test_swapped_lines(void) {
    key_output_cfg_t cfg = serial_cfg("rts", "dtr");
    return expect_levels("rest", levels_for(&cfg, false, false), 0u) &&
           expect_levels("key down", levels_for(&cfg, true, false), (unsigned)TIOCM_RTS) &&
           expect_levels("ptt on", levels_for(&cfg, false, true), (unsigned)TIOCM_DTR);
}

static bool test_inverted_key(void) {
    key_output_cfg_t cfg = serial_cfg("dtr-inv", "rts");
    return expect_levels("rest", levels_for(&cfg, false, false), (unsigned)TIOCM_DTR) &&
           expect_levels("key down", levels_for(&cfg, true, false), 0u) &&
           expect_levels("ptt on", levels_for(&cfg, false, true),
                         (unsigned)(TIOCM_DTR | TIOCM_RTS));
}

static bool test_inverted_ptt(void) {
    key_output_cfg_t cfg = serial_cfg("dtr", "rts-inv");
    return expect_levels("rest", levels_for(&cfg, false, false), (unsigned)TIOCM_RTS) &&
           expect_levels("ptt on", levels_for(&cfg, false, true), 0u);
}

/* R5: a line no function uses is low at rest and never goes high. */
static bool test_ptt_none(void) {
    key_output_cfg_t cfg = serial_cfg("dtr", "none");
    return expect_levels("rest", levels_for(&cfg, false, false), 0u) &&
           expect_levels("ptt on", levels_for(&cfg, false, true), 0u) &&
           expect_levels("key down, ptt on", levels_for(&cfg, true, true),
                         (unsigned)TIOCM_DTR);
}

static bool test_ptt_none_with_inverted_key(void) {
    key_output_cfg_t cfg = serial_cfg("rts-inv", "none");
    return expect_levels("rest", levels_for(&cfg, false, false), (unsigned)TIOCM_RTS) &&
           expect_levels("ptt on", levels_for(&cfg, false, true), (unsigned)TIOCM_RTS) &&
           expect_levels("key down", levels_for(&cfg, true, false), 0u);
}

/* A NULL line takes the default: that is how main.c passes an unset flag. */
static bool test_unset_lines_take_the_defaults(void) {
    key_output_cfg_t cfg = serial_cfg(NULL, NULL);
    return expect_levels("key down", levels_for(&cfg, true, false), (unsigned)TIOCM_DTR) &&
           expect_levels("ptt on", levels_for(&cfg, false, true), (unsigned)TIOCM_RTS);
}

/*===========================================================================*/
/* Refusals                                                                  */
/*===========================================================================*/

static bool test_same_line_refused(void) {
    key_output_cfg_t cfg = serial_cfg("dtr", "dtr-inv");
    const char *want[] = { "--key-line", "--ptt-line" };
    return expect_refused(&cfg, want, 2);
}

static bool test_key_none_refused(void) {
    key_output_cfg_t cfg = serial_cfg("none", "rts");
    const char *want[] = { "--key-line" };
    return expect_refused(&cfg, want, 1);
}

static bool test_unknown_key_line_refused(void) {
    key_output_cfg_t cfg = serial_cfg("cts", "rts");
    const char *want[] = { "--key-line", "cts", "dtr, rts, dtr-inv, rts-inv" };
    return expect_refused(&cfg, want, 3);
}

static bool test_unknown_ptt_line_refused(void) {
    key_output_cfg_t cfg = serial_cfg("dtr", "RTS");
    const char *want[] = { "--ptt-line", "RTS", "dtr, rts, dtr-inv, rts-inv, none" };
    return expect_refused(&cfg, want, 3);
}

static bool test_serial_without_device_refused(void) {
    key_output_cfg_t cfg = serial_cfg("dtr", "rts");
    cfg.device = NULL;
    const char *want[] = { "--serial" };
    if (!expect_refused(&cfg, want, 1)) {
        return false;
    }
    cfg.device = "";
    return expect_refused(&cfg, want, 1);
}

static bool test_unknown_backend_refused(void) {
    key_output_cfg_t cfg = { .backend = "gpio" };
    const char *want[] = { "gpio", "virtual" };
    return expect_refused(&cfg, want, 2);
}

/* The virtual backend has no lines: its check ignores the serial fields. */
static bool test_virtual_accepted(void) {
    key_output_cfg_t cfg = { .backend = "virtual" };
    key_output_map_t map;
    char err[KEY_OUTPUT_ERR_LEN];
    if (!key_output_check(&cfg, &map, err, sizeof(err))) {
        fprintf(stderr, "  virtual refused: %s\n", err);
        return false;
    }
    return expect_levels("virtual", key_output_levels(&map, true, true), 0u);
}

/*===========================================================================*/
/* Root warning                                                              */
/*===========================================================================*/

static bool test_root_warning(void) {
    const char *w = key_output_root_warning("serial", 0ul);
    if (w == NULL || strstr(w, "root") == NULL) {
        fprintf(stderr, "  serial as root: no warning, or one that does not say root\n");
        return false;
    }
    if (key_output_root_warning("serial", 501ul) != NULL) {
        fprintf(stderr, "  serial as uid 501: warned\n");
        return false;
    }
    if (key_output_root_warning("virtual", 0ul) != NULL) {
        fprintf(stderr, "  virtual as root: warned\n");
        return false;
    }
    return true;
}

/*===========================================================================*/
/* Fake OS (KTD8)                                                            */
/*===========================================================================*/

#define FAKE_FD 7
#define FAKE_MAX_CALLS 64
#define FAKE_MAX_EDGES 16
#define FAKE_CALL_US 1000

typedef struct {
    const char *name;   /* "open", "TIOCMSET", "tcsetattr", ... */
    int arg;            /* TIOCMSET: the lines; flock: the operation */
} fake_call_t;

static struct {
    fake_call_t calls[FAKE_MAX_CALLS];
    size_t n_calls;

    int lines;                 /* DTR | RTS as the port holds them */
    bool readback_lies;        /* TIOCMGET reports the opposite of lines */
    struct termios tio;        /* What tcgetattr returns, and tcsetattr stores */

    const char *fail_call;     /* This call fails ... */
    int fail_nth;              /* ... at its nth occurrence, from 1 ... */
    int fail_errno;            /* ... with this errno */

    int64_t clock_us;
    int64_t mset_us[FAKE_MAX_CALLS];  /* How long the kth TIOCMSET takes; 0 = default */
    int n_mset;

    char edges[FAKE_MAX_EDGES][32];
    size_t n_edges;

    int reads_with_data;       /* read() returns bytes this many times ... */
    ssize_t read_end;          /* ... then this: -1 (with read_errno) or 0 */
    int read_errno;
} F;

static void fake_reset(void) {
    memset(&F, 0, sizeof(F));
    F.lines = TIOCM_DTR | TIOCM_RTS;   /* What an open leaves on Linux and macOS */
    (void)cfsetispeed(&F.tio, B9600);
    (void)cfsetospeed(&F.tio, B9600);
    /* What a previous program may have left, and Linux keeps across
     * closes: hardware flow control, and a canonical line with echo. */
    F.tio.c_cflag |= (tcflag_t)(CS8 | CREAD | CRTSCTS);
    F.tio.c_lflag |= (tcflag_t)(ICANON | ECHO | ISIG);
    F.read_end = -1;
    F.read_errno = EAGAIN;
}

/* Records the call; returns true when it is the one set to fail. */
static bool fake_record(const char *name, int arg) {
    if (F.n_calls < FAKE_MAX_CALLS) {
        F.calls[F.n_calls].name = name;
        F.calls[F.n_calls].arg = arg;
        F.n_calls++;
    }
    if (F.fail_call != NULL && strcmp(F.fail_call, name) == 0) {
        F.fail_nth--;
        if (F.fail_nth == 0) {
            errno = F.fail_errno;
            return true;
        }
    }
    return false;
}

static int fake_open(const char *path, int flags) {
    (void)path;
    return fake_record("open", flags) ? -1 : FAKE_FD;
}

static int fake_close(int fd) {
    (void)fd;
    (void)fake_record("close", 0);
    /* HUPCL: the kernel drops both lines on the last close. */
    F.lines = 0;
    return 0;
}

static int fake_ioctl_int(int fd, unsigned long request, int *arg) {
    (void)fd;
    if (request == (unsigned long)TIOCMSET) {
        int64_t us = F.mset_us[F.n_mset];
        F.n_mset++;
        F.clock_us += (us != 0) ? us : FAKE_CALL_US;
        if (fake_record("TIOCMSET", *arg)) {
            return -1;
        }
        F.lines = *arg & (TIOCM_DTR | TIOCM_RTS);
        return 0;
    }
    if (request == (unsigned long)TIOCMGET) {
        if (fake_record("TIOCMGET", 0)) {
            return -1;
        }
        int l = F.readback_lies ? (~F.lines & (TIOCM_DTR | TIOCM_RTS)) : F.lines;
        *arg = l | TIOCM_CTS;   /* An input line, which the read-back must ignore */
        return 0;
    }
    (void)fake_record("ioctl?", 0);
    errno = ENOTTY;
    return -1;
}

static int fake_ioctl_none(int fd, unsigned long request) {
    (void)fd;
    return fake_record((request == (unsigned long)TIOCEXCL) ? "TIOCEXCL" : "ioctl?", 0) ? -1 : 0;
}

static int fake_tcgetattr(int fd, struct termios *t) {
    (void)fd;
    if (fake_record("tcgetattr", 0)) {
        return -1;
    }
    *t = F.tio;
    return 0;
}

static int fake_tcsetattr(int fd, int action, const struct termios *t) {
    (void)fd;
    (void)action;
    if (fake_record("tcsetattr", 0)) {
        return -1;
    }
    F.tio = *t;
    return 0;
}

static int fake_flock(int fd, int operation) {
    (void)fd;
    return fake_record("flock", operation) ? -1 : 0;
}

static ssize_t fake_read(int fd, void *buf, size_t len) {
    (void)fd;
    if (F.reads_with_data > 0) {
        F.reads_with_data--;
        (void)fake_record("read", 1);
        memset(buf, 'x', len);
        return (ssize_t)len;
    }
    (void)fake_record("read", 0);
    if (F.read_end < 0) {
        errno = F.read_errno;
    }
    return F.read_end;
}

static int64_t fake_now_us(void) {
    return F.clock_us;
}

static const key_output_os_t fake_os = {
    .open       = fake_open,
    .close      = fake_close,
    .ioctl_int  = fake_ioctl_int,
    .ioctl_none = fake_ioctl_none,
    .tcgetattr  = fake_tcgetattr,
    .tcsetattr  = fake_tcsetattr,
    .flock      = fake_flock,
    .read       = fake_read,
    .now_us     = fake_now_us,
    .b0_at_rest = true,
};

static void capture_edge(void *ctx, const char *line) {
    (void)ctx;
    if (F.n_edges < FAKE_MAX_EDGES) {
        (void)snprintf(F.edges[F.n_edges], sizeof(F.edges[0]), "%s", line);
        F.n_edges++;
    }
}

/* Opens the backend on the given fake OS with the given lines. */
static bool fake_serial_open_on(const key_output_os_t *os, key_output_t *out,
                                const char *key_line, const char *ptt_line,
                                char *err, size_t err_len) {
    key_output_cfg_t cfg = serial_cfg(key_line, ptt_line);
    key_output_map_t map;
    if (!key_output_check(&cfg, &map, err, err_len)) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->line = capture_edge;
    return key_output_serial_open(out, cfg.device, &map, os, err, err_len);
}

static bool fake_serial_open(key_output_t *out, const char *key_line, const char *ptt_line,
                             char *err, size_t err_len) {
    return fake_serial_open_on(&fake_os, out, key_line, ptt_line, err, err_len);
}

/* The recorded call names, space separated, for one comparison. */
static const char *call_names(size_t from) {
    static char buf[512];
    buf[0] = '\0';
    for (size_t i = from; i < F.n_calls; i++) {
        size_t len = strlen(buf);
        (void)snprintf(buf + len, sizeof(buf) - len, "%s%s", (len > 0u) ? " " : "",
                       F.calls[i].name);
    }
    return buf;
}

static bool expect_calls(const char *what, size_t from, const char *want) {
    const char *got = call_names(from);
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "  %s: calls \"%s\", want \"%s\"\n", what, got, want);
        return false;
    }
    return true;
}

static bool expect_edges(const char *const *want, size_t n) {
    if (F.n_edges != n) {
        fprintf(stderr, "  %zu edge lines, want %zu\n", F.n_edges, n);
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (strcmp(F.edges[i], want[i]) != 0) {
            fprintf(stderr, "  edge %zu \"%s\", want \"%s\"\n", i, F.edges[i], want[i]);
            return false;
        }
    }
    return true;
}

#define OPEN_CALLS "open TIOCMSET tcgetattr tcsetattr TIOCEXCL flock TIOCMGET"

/*===========================================================================*/
/* Open (KTD2)                                                               */
/*===========================================================================*/

/* The safety property: after open, the first call puts both lines at rest,
 * and nothing drives a line active before it. */
static bool test_open_order_plain(void) {
    fake_reset();
    key_output_t out;
    char err[KEY_OUTPUT_ERR_LEN];
    if (!fake_serial_open(&out, "dtr", "rts", err, sizeof(err))) {
        fprintf(stderr, "  open failed: %s\n", err);
        return false;
    }
    if (!expect_calls("open", 0, OPEN_CALLS)) {
        return false;
    }
    if (F.calls[1].arg != 0) {
        fprintf(stderr, "  rest call set 0x%x, want 0\n", (unsigned)F.calls[1].arg);
        return false;
    }
    if ((F.calls[0].arg & O_NONBLOCK) == 0 || (F.calls[0].arg & O_NOCTTY) == 0 ||
        (F.calls[0].arg & O_CLOEXEC) == 0) {
        fprintf(stderr, "  open flags 0x%x lack O_NONBLOCK, O_NOCTTY or O_CLOEXEC\n",
                (unsigned)F.calls[0].arg);
        return false;
    }
    if (F.calls[5].arg != (LOCK_EX | LOCK_NB)) {
        fprintf(stderr, "  flock 0x%x, want LOCK_EX | LOCK_NB\n", (unsigned)F.calls[5].arg);
        return false;
    }
    if ((F.tio.c_cflag & HUPCL) == 0 || (F.tio.c_cflag & CLOCAL) == 0) {
        fprintf(stderr, "  termios lacks HUPCL or CLOCAL\n");
        return false;
    }
    if (cfgetospeed(&F.tio) != B0) {
        fprintf(stderr, "  plain lines: speed not B0\n");
        return false;
    }
    /* Hardware flow control would hand RTS, the PTT, to the driver. */
    if ((F.tio.c_cflag & CRTSCTS) != 0) {
        fprintf(stderr, "  termios keeps CRTSCTS\n");
        return false;
    }
    /* Raw: POLLIN on any byte, and nothing echoed back to the rig. */
    if ((F.tio.c_lflag & (ICANON | ECHO | ISIG)) != 0 || F.tio.c_cc[VMIN] != 1 ||
        F.tio.c_cc[VTIME] != 0) {
        fprintf(stderr, "  termios not raw with VMIN 1, VTIME 0\n");
        return false;
    }
    if (key_output_poll_fd(&out) != FAKE_FD) {
        fprintf(stderr, "  poll fd %d, want %d\n", key_output_poll_fd(&out), FAKE_FD);
        return false;
    }
    if (F.lines != 0 || F.n_edges != 0u || out.fault.set) {
        fprintf(stderr, "  after open: lines 0x%x, %zu edges, fault %d\n",
                (unsigned)F.lines, F.n_edges, out.fault.set);
        return false;
    }
    key_output_close(&out, 0);
    return true;
}

/* B0 drops both lines, which is active on an inverted one: keep the speed. */
static bool test_open_inverted_keeps_speed(void) {
    fake_reset();
    key_output_t out;
    char err[KEY_OUTPUT_ERR_LEN];
    if (!fake_serial_open(&out, "dtr-inv", "rts", err, sizeof(err))) {
        fprintf(stderr, "  open failed: %s\n", err);
        return false;
    }
    bool ok = expect_calls("open", 0, OPEN_CALLS);
    if (F.calls[1].arg != TIOCM_DTR) {
        fprintf(stderr, "  rest call set 0x%x, want DTR high\n", (unsigned)F.calls[1].arg);
        ok = false;
    }
    if (cfgetospeed(&F.tio) != B9600) {
        fprintf(stderr, "  inverted line: speed changed\n");
        ok = false;
    }
    if ((F.tio.c_cflag & HUPCL) == 0) {
        fprintf(stderr, "  termios lacks HUPCL\n");
        ok = false;
    }
    key_output_close(&out, 0);
    return ok;
}

static bool test_open_inverted_ptt_keeps_speed(void) {
    fake_reset();
    key_output_t out;
    char err[KEY_OUTPUT_ERR_LEN];
    if (!fake_serial_open(&out, "dtr", "rts-inv", err, sizeof(err))) {
        fprintf(stderr, "  open failed: %s\n", err);
        return false;
    }
    bool ok = (F.calls[1].arg == TIOCM_RTS) && (cfgetospeed(&F.tio) == B9600);
    if (!ok) {
        fprintf(stderr, "  rest 0x%x or speed changed\n", (unsigned)F.calls[1].arg);
    }
    key_output_close(&out, 0);
    return ok;
}

/* macOS: no B0, the rest is the same. */
static bool test_open_without_b0(void) {
    fake_reset();
    key_output_os_t os = fake_os;
    os.b0_at_rest = false;
    key_output_t out;
    char err[KEY_OUTPUT_ERR_LEN];
    if (!fake_serial_open_on(&os, &out, "dtr", "rts", err, sizeof(err))) {
        fprintf(stderr, "  open failed: %s\n", err);
        return false;
    }
    bool ok = expect_calls("open", 0, OPEN_CALLS) && F.calls[1].arg == 0 &&
              cfgetospeed(&F.tio) == B9600 && (F.tio.c_cflag & HUPCL) != 0;
    if (!ok) {
        fprintf(stderr, "  without B0: speed changed, or rest or HUPCL wrong\n");
    }
    key_output_close(&out, 0);
    return ok;
}

/* Open fails at `call`: the descriptor is closed, no line call follows the
 * rest call, and the message names the device and contains `want`. */
static bool expect_open_fails_at(const char *call, int err_no, const char *want) {
    fake_reset();
    F.fail_call = call;
    F.fail_nth = 1;
    F.fail_errno = err_no;
    key_output_t out;
    char err[KEY_OUTPUT_ERR_LEN];
    err[0] = '\0';
    if (fake_serial_open(&out, "dtr", "rts", err, sizeof(err))) {
        fprintf(stderr, "  %s failing: open succeeded\n", call);
        return false;
    }
    if (strstr(err, "/dev/ttyUSB0") == NULL || strstr(err, want) == NULL) {
        fprintf(stderr, "  %s failing: message \"%s\" lacks the device or \"%s\"\n",
                call, err, want);
        return false;
    }
    int msets = 0;
    for (size_t i = 0; i < F.n_calls; i++) {
        if (strcmp(F.calls[i].name, "TIOCMSET") == 0) {
            msets++;
        }
    }
    if (msets > 1) {
        fprintf(stderr, "  %s failing: %d line calls\n", call, msets);
        return false;
    }
    bool opened = strcmp(call, "open") != 0;
    const char *last = (F.n_calls > 0u) ? F.calls[F.n_calls - 1u].name : "";
    if (opened && strcmp(last, "close") != 0) {
        fprintf(stderr, "  %s failing: last call %s, want close\n", call, last);
        return false;
    }
    if (!opened && F.n_calls != 1u) {
        fprintf(stderr, "  open failing: %zu calls after it\n", F.n_calls - 1u);
        return false;
    }
    /* A second close must not reach the OS. */
    size_t n = F.n_calls;
    key_output_close(&out, 0);
    if (F.n_calls != n) {
        fprintf(stderr, "  %s failing: close after a failed open called the OS\n", call);
        return false;
    }
    return true;
}

static bool test_open_errors_named(void) {
    return expect_open_fails_at("open", ENOENT, "non esiste") &&
           expect_open_fails_at("open", EACCES, "permesso") &&
           expect_open_fails_at("open", EBUSY, "occupata") &&
           expect_open_fails_at("TIOCMSET", ENOTTY, "non e' una porta seriale") &&
           expect_open_fails_at("TIOCMSET", ENODEV, "non e' una porta seriale") &&
           expect_open_fails_at("tcgetattr", EIO, "tcgetattr") &&
           expect_open_fails_at("tcsetattr", EIO, "tcsetattr") &&
           expect_open_fails_at("TIOCEXCL", EBUSY, "occupata") &&
           expect_open_fails_at("flock", EWOULDBLOCK, "occupata") &&
           expect_open_fails_at("TIOCMGET", EIO, "TIOCMGET");
}

static bool test_open_readback_mismatch(void) {
    fake_reset();
    F.readback_lies = true;
    key_output_t out;
    char err[KEY_OUTPUT_ERR_LEN];
    if (fake_serial_open(&out, "dtr", "rts", err, sizeof(err))) {
        fprintf(stderr, "  a read-back that disagrees was accepted\n");
        return false;
    }
    return expect_calls("readback", 0, OPEN_CALLS " close");
}

/*===========================================================================*/
/* Edges and failures (KTD3, KTD4, KTD5)                                     */
/*===========================================================================*/

static bool test_key_edge_one_call_one_line(void) {
    fake_reset();
    key_output_t out;
    char err[KEY_OUTPUT_ERR_LEN];
    if (!fake_serial_open(&out, "dtr", "rts", err, sizeof(err))) {
        return false;
    }
    size_t from = F.n_calls;
    key_output_set_key(&out, true, 1234);
    const char *want[] = { "key 1 1234" };
    bool ok = expect_calls("key edge", from, "TIOCMSET") && expect_edges(want, 1) &&
              F.lines == TIOCM_DTR && out.timing.count == 1u;
    key_output_close(&out, 0);
    return ok;
}

/* R8: the trace does not depend on the backend. */
static bool test_same_edges_as_virtual(void) {
    static const struct { bool key; bool on; int64_t at; } seq[] = {
        { false, true, 100 }, { true, true, 110 }, { true, false, 170 },
        { true, true, 230 }, { true, false, 250 }, { false, false, 350 },
    };
    size_t n = sizeof(seq) / sizeof(seq[0]);

    fake_reset();
    key_output_t v;
    char err[KEY_OUTPUT_ERR_LEN];
    key_output_cfg_t vcfg = { .backend = "virtual" };
    if (!key_output_open(&v, &vcfg, capture_edge, NULL, err, sizeof(err))) {
        fprintf(stderr, "  virtual open: %s\n", err);
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (seq[i].key) {
            key_output_set_key(&v, seq[i].on, seq[i].at);
        } else {
            key_output_set_ptt(&v, seq[i].on, seq[i].at);
        }
    }
    key_output_close(&v, 400);
    char virt[FAKE_MAX_EDGES][32];
    size_t n_virt = F.n_edges;
    memcpy(virt, F.edges, sizeof(virt));

    fake_reset();
    key_output_t s;
    if (!fake_serial_open(&s, "dtr", "rts", err, sizeof(err))) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (seq[i].key) {
            key_output_set_key(&s, seq[i].on, seq[i].at);
        } else {
            key_output_set_ptt(&s, seq[i].on, seq[i].at);
        }
    }
    key_output_close(&s, 400);

    const char *want[FAKE_MAX_EDGES];
    for (size_t i = 0; i < n_virt; i++) {
        want[i] = virt[i];
    }
    return n_virt == n && expect_edges(want, n_virt);
}

static bool test_line_call_error_recorded(void) {
    fake_reset();
    key_output_t out;
    char err[KEY_OUTPUT_ERR_LEN];
    if (!fake_serial_open(&out, "dtr", "rts", err, sizeof(err))) {
        return false;
    }
    F.fail_call = "TIOCMSET";
    F.fail_nth = 1;
    F.fail_errno = EIO;
    key_output_set_key(&out, true, 500);
    const key_output_fault_t *f = key_output_fault(&out);
    const char *want[] = { "key 1 500" };
    bool ok = expect_edges(want, 1);
    if (f == NULL || !f->set || f->kind != KEY_OUTPUT_FAULT_CALL ||
        strcmp(f->call, "TIOCMSET") != 0 || f->err != EIO ||
        f->duration_us != FAKE_CALL_US) {
        fprintf(stderr, "  fault not recorded as TIOCMSET, EIO, %d us\n", FAKE_CALL_US);
        ok = false;
    }
    key_output_close(&out, 600);
    return ok;
}

/* KTD3: over 100 ms is a FAULT, 100 ms is not. */
static bool test_slow_line_call(void) {
    fake_reset();
    key_output_t out;
    char err[KEY_OUTPUT_ERR_LEN];
    if (!fake_serial_open(&out, "dtr", "rts", err, sizeof(err))) {
        return false;
    }
    F.mset_us[F.n_mset] = KEY_OUTPUT_SERIAL_SLOW_US;
    key_output_set_key(&out, true, 10);
    if (key_output_fault(&out) != NULL) {
        fprintf(stderr, "  100 ms: fault recorded\n");
        return false;
    }
    F.mset_us[F.n_mset] = KEY_OUTPUT_SERIAL_SLOW_US + 1000;
    key_output_set_key(&out, false, 20);
    const key_output_fault_t *f = key_output_fault(&out);
    bool ok = true;
    if (f == NULL || f->kind != KEY_OUTPUT_FAULT_SLOW ||
        f->duration_us != KEY_OUTPUT_SERIAL_SLOW_US + 1000) {
        fprintf(stderr, "  101 ms: no fault, or not recorded as slow\n");
        ok = false;
    }
    if (out.timing.count != 2u || out.timing.slow != 1u ||
        out.timing.max_us != KEY_OUTPUT_SERIAL_SLOW_US + 1000) {
        fprintf(stderr, "  timing %lu changes, %lu slow, max %lld us\n", out.timing.count,
                out.timing.slow, (long long)out.timing.max_us);
        ok = false;
    }
    key_output_close(&out, 30);
    return ok;
}

/* key_output_release()'s order on a port: key up, then PTT off, then close. */
static bool test_close_releases_in_order(void) {
    fake_reset();
    key_output_t out;
    char err[KEY_OUTPUT_ERR_LEN];
    if (!fake_serial_open(&out, "dtr", "rts", err, sizeof(err))) {
        return false;
    }
    key_output_set_ptt(&out, true, 1);
    key_output_set_key(&out, true, 2);
    size_t from = F.n_calls;
    key_output_close(&out, 3);
    if (!expect_calls("close", from, "TIOCMSET TIOCMSET close")) {
        return false;
    }
    if (F.calls[from].arg != TIOCM_RTS || F.calls[from + 1u].arg != 0) {
        fprintf(stderr, "  release set 0x%x then 0x%x, want RTS then 0\n",
                (unsigned)F.calls[from].arg, (unsigned)F.calls[from + 1u].arg);
        return false;
    }
    /* Twice: nothing reaches the OS. */
    size_t n = F.n_calls;
    key_output_close(&out, 4);
    if (F.n_calls != n) {
        fprintf(stderr, "  second close called the OS\n");
        return false;
    }
    return true;
}

/* KTD4: after a failed key-up, no line call at all. A release would skip
 * the key-up (key_down is already false) and drop PTT under a key that may
 * still be down; only the close, with HUPCL, drops both together. */
static bool test_no_line_call_after_failure(void) {
    fake_reset();
    key_output_t out;
    char err[KEY_OUTPUT_ERR_LEN];
    if (!fake_serial_open(&out, "dtr", "rts", err, sizeof(err))) {
        return false;
    }
    key_output_set_ptt(&out, true, 1);
    key_output_set_key(&out, true, 2);
    F.fail_call = "TIOCMSET";
    F.fail_nth = 1;
    F.fail_errno = EIO;
    size_t from = F.n_calls;
    key_output_set_key(&out, false, 3);
    key_output_set_ptt(&out, false, 4);
    key_output_close(&out, 5);
    const char *want[] = { "ptt 1 1", "key 1 2", "key 0 3", "ptt 0 4" };
    return expect_calls("after failure", from, "TIOCMSET close") && expect_edges(want, 4);
}

/* A call that succeeds after a 5000 ms stall: the edges queued behind it
 * must not go out back to back. */
static bool test_no_line_call_after_stall(void) {
    fake_reset();
    key_output_t out;
    char err[KEY_OUTPUT_ERR_LEN];
    if (!fake_serial_open(&out, "dtr", "rts", err, sizeof(err))) {
        return false;
    }
    F.mset_us[F.n_mset] = 5000000;
    size_t from = F.n_calls;
    key_output_set_key(&out, true, 10);
    key_output_set_key(&out, false, 20);
    key_output_set_key(&out, true, 30);
    key_output_set_key(&out, false, 40);
    bool ok = expect_calls("after stall", from, "TIOCMSET");
    key_output_close(&out, 50);
    return ok && F.n_edges == 4u;
}

/*===========================================================================*/
/* The port seen by the loop's poll (R7)                                     */
/*===========================================================================*/

static bool test_poll_fd_only_while_open(void) {
    fake_reset();
    key_output_t v;
    char err[KEY_OUTPUT_ERR_LEN];
    key_output_cfg_t vcfg = { .backend = "virtual" };
    if (!key_output_open(&v, &vcfg, capture_edge, NULL, err, sizeof(err)) ||
        key_output_poll_fd(&v) != -1) {
        fprintf(stderr, "  virtual has a poll fd\n");
        return false;
    }
    key_output_t out;
    if (!fake_serial_open(&out, "dtr", "rts", err, sizeof(err))) {
        return false;
    }
    key_output_close(&out, 0);
    if (key_output_poll_fd(&out) != -1) {
        fprintf(stderr, "  poll fd after close\n");
        return false;
    }
    return true;
}

/* A hang-up is the device gone: a fault, and no line call after it. */
static bool test_hangup_is_a_fault(void) {
    fake_reset();
    key_output_t out;
    char err[KEY_OUTPUT_ERR_LEN];
    if (!fake_serial_open(&out, "dtr", "rts", err, sizeof(err))) {
        return false;
    }
    size_t from = F.n_calls;
    key_output_service(&out, true);
    const key_output_fault_t *f = key_output_fault(&out);
    if (f == NULL || f->kind != KEY_OUTPUT_FAULT_HANGUP) {
        fprintf(stderr, "  hang-up: no fault, or not named hangup\n");
        return false;
    }
    key_output_set_ptt(&out, true, 1);
    key_output_close(&out, 2);
    return expect_calls("after hang-up", from, "close");
}

/* Bytes from the rig are read and dropped until there are none. */
static bool test_received_bytes_drained(void) {
    fake_reset();
    key_output_t out;
    char err[KEY_OUTPUT_ERR_LEN];
    if (!fake_serial_open(&out, "dtr", "rts", err, sizeof(err))) {
        return false;
    }
    F.reads_with_data = 2;
    size_t from = F.n_calls;
    key_output_service(&out, false);
    bool ok = expect_calls("drain", from, "read read read") &&
              key_output_fault(&out) == NULL;
    key_output_close(&out, 0);
    return ok;
}

/* The drain is bounded: a port that never stops talking cannot hold the
 * loop. What is left is read on the next pass. */
static bool test_drain_is_bounded(void) {
    fake_reset();
    key_output_t out;
    char err[KEY_OUTPUT_ERR_LEN];
    if (!fake_serial_open(&out, "dtr", "rts", err, sizeof(err))) {
        return false;
    }
    F.reads_with_data = 1000;
    size_t from = F.n_calls;
    key_output_service(&out, false);
    size_t reads = F.n_calls - from;
    key_output_close(&out, 0);
    if (reads == 0u || reads > (size_t)KEY_OUTPUT_SERIAL_MAX_READS) {
        fprintf(stderr, "  %zu reads, want 1 to %d\n", reads, KEY_OUTPUT_SERIAL_MAX_READS);
        return false;
    }
    return key_output_fault(&out) == NULL;
}

/* End of file or an error on read: the device is gone. */
static bool test_read_end_is_a_fault(void) {
    static const struct { ssize_t end; int err_no; } cases[] = {
        { 0, 0 }, { -1, EIO }, { -1, ENXIO },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        fake_reset();
        key_output_t out;
        char err[KEY_OUTPUT_ERR_LEN];
        if (!fake_serial_open(&out, "dtr", "rts", err, sizeof(err))) {
            return false;
        }
        F.read_end = cases[i].end;
        F.read_errno = cases[i].err_no;
        key_output_service(&out, false);
        const key_output_fault_t *f = key_output_fault(&out);
        key_output_close(&out, 0);
        if (f == NULL || f->kind != KEY_OUTPUT_FAULT_READ || f->err != cases[i].err_no) {
            fprintf(stderr, "  read returning %zd errno %d: no fault, or wrong record\n",
                    cases[i].end, cases[i].err_no);
            return false;
        }
    }
    return true;
}

/*===========================================================================*/
/* Runner                                                                    */
/*===========================================================================*/

typedef struct {
    const char *name;
    bool (*fn)(void);
} test_case_t;

int main(void) {
    const test_case_t tests[] = {
        {"default_mapping", test_default_mapping},
        {"swapped_lines", test_swapped_lines},
        {"inverted_key", test_inverted_key},
        {"inverted_ptt", test_inverted_ptt},
        {"ptt_none", test_ptt_none},
        {"ptt_none_with_inverted_key", test_ptt_none_with_inverted_key},
        {"unset_lines_take_the_defaults", test_unset_lines_take_the_defaults},
        {"same_line_refused", test_same_line_refused},
        {"key_none_refused", test_key_none_refused},
        {"unknown_key_line_refused", test_unknown_key_line_refused},
        {"unknown_ptt_line_refused", test_unknown_ptt_line_refused},
        {"serial_without_device_refused", test_serial_without_device_refused},
        {"unknown_backend_refused", test_unknown_backend_refused},
        {"virtual_accepted", test_virtual_accepted},
        {"root_warning", test_root_warning},
        {"open_order_plain", test_open_order_plain},
        {"open_inverted_keeps_speed", test_open_inverted_keeps_speed},
        {"open_inverted_ptt_keeps_speed", test_open_inverted_ptt_keeps_speed},
        {"open_without_b0", test_open_without_b0},
        {"open_errors_named", test_open_errors_named},
        {"open_readback_mismatch", test_open_readback_mismatch},
        {"key_edge_one_call_one_line", test_key_edge_one_call_one_line},
        {"same_edges_as_virtual", test_same_edges_as_virtual},
        {"line_call_error_recorded", test_line_call_error_recorded},
        {"slow_line_call", test_slow_line_call},
        {"close_releases_in_order", test_close_releases_in_order},
        {"no_line_call_after_failure", test_no_line_call_after_failure},
        {"no_line_call_after_stall", test_no_line_call_after_stall},
        {"poll_fd_only_while_open", test_poll_fd_only_while_open},
        {"hangup_is_a_fault", test_hangup_is_a_fault},
        {"received_bytes_drained", test_received_bytes_drained},
        {"drain_is_bounded", test_drain_is_bounded},
        {"read_end_is_a_fault", test_read_end_is_a_fault},
    };
    size_t n_tests = sizeof(tests) / sizeof(tests[0]);

    size_t passed = 0;
    for (size_t i = 0; i < n_tests; ++i) {
        bool ok = tests[i].fn();
        printf("[%s] %s\n", ok ? "PASS" : "FAIL", tests[i].name);
        if (ok) {
            ++passed;
        }
    }

    printf("%zu/%zu tests passed\n", passed, n_tests);
    return passed == n_tests ? 0 : 1;
}
