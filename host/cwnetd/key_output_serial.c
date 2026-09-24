/**
 * @file key_output_serial.c
 * @brief The serial backend of key_output.h: key and PTT on DTR and RTS.
 *
 * Plan docs/plans/2026-09-24-2143-feat-cwnetd-serial-output-plan.md. The
 * default follows N1MM Logger+ and the DL4YHF server: DTR is the CW key,
 * RTS is PTT, a high line is an active function. Either function can move
 * to the other line or be inverted (KTD7).
 */
#include "key_output.h"
#include "key_output_serial.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*===========================================================================*/
/* Line mapping                                                              */
/*===========================================================================*/

/* The values --key-line accepts, and --ptt-line with "none" added. */
#define LINES_KEY "dtr, rts, dtr-inv, rts-inv"
#define LINES_PTT LINES_KEY ", none"

/*
 * One LINE value to a TIOCM bit and a polarity. "none" is a zero bit. An
 * unknown value returns false; case matters, as it does for every other
 * flag value of the daemon.
 */
static bool parse_line(const char *s, unsigned *bit, bool *inverted) {
    static const struct {
        const char *name;
        unsigned bit;
        bool inverted;
    } lines[] = {
        { "dtr",     (unsigned)TIOCM_DTR, false },
        { "rts",     (unsigned)TIOCM_RTS, false },
        { "dtr-inv", (unsigned)TIOCM_DTR, true },
        { "rts-inv", (unsigned)TIOCM_RTS, true },
        { "none",    0u,                  false },
    };
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        if (strcmp(s, lines[i].name) == 0) {
            *bit = lines[i].bit;
            *inverted = lines[i].inverted;
            return true;
        }
    }
    return false;
}

bool key_output_check(const key_output_cfg_t *cfg, key_output_map_t *map,
                      char *err, size_t err_len) {
    if (cfg == NULL || map == NULL || err == NULL || err_len == 0u) {
        return false;
    }
    memset(map, 0, sizeof(*map));
    err[0] = '\0';

    const char *backend = (cfg->backend != NULL) ? cfg->backend : "";
    if (strcmp(backend, "virtual") == 0) {
        /* A port named with the virtual output: the operator meant to key a
         * rig, and would get a daemon that keys nothing and says nothing. */
        if (cfg->device != NULL) {
            (void)snprintf(err, err_len, "--serial %s richiede --output serial", cfg->device);
            return false;
        }
        return true;
    }
    if (strcmp(backend, "serial") != 0) {
        (void)snprintf(err, err_len, "backend di uscita sconosciuto: %s (validi: %s)",
                       backend, key_output_backends());
        return false;
    }

    if (cfg->device == NULL || cfg->device[0] == '\0') {
        (void)snprintf(err, err_len, "--output serial richiede --serial DEVICE");
        return false;
    }

    const char *key = (cfg->key_line != NULL) ? cfg->key_line : KEY_OUTPUT_DEFAULT_KEY_LINE;
    const char *ptt = (cfg->ptt_line != NULL) ? cfg->ptt_line : KEY_OUTPUT_DEFAULT_PTT_LINE;

    if (!parse_line(key, &map->key_bit, &map->key_inverted)) {
        (void)snprintf(err, err_len, "--key-line %s: valori validi %s", key, LINES_KEY);
        return false;
    }
    if (map->key_bit == 0u) {
        (void)snprintf(err, err_len, "--key-line none: il tasto deve avere una linea (%s)",
                       LINES_KEY);
        return false;
    }
    if (!parse_line(ptt, &map->ptt_bit, &map->ptt_inverted)) {
        (void)snprintf(err, err_len, "--ptt-line %s: valori validi %s", ptt, LINES_PTT);
        return false;
    }
    if (map->ptt_bit == map->key_bit) {
        (void)snprintf(err, err_len, "--key-line %s e --ptt-line %s usano la stessa linea",
                       key, ptt);
        return false;
    }
    return true;
}

unsigned key_output_levels(const key_output_map_t *map, bool key_down, bool ptt_on) {
    unsigned high = 0u;
    if (map == NULL) {
        return high;
    }
    /* A line is high when its function's state differs from its inversion:
     * active on a plain line, at rest on an inverted one. */
    if (map->key_bit != 0u && key_down != map->key_inverted) {
        high |= map->key_bit;
    }
    if (map->ptt_bit != 0u && ptt_on != map->ptt_inverted) {
        high |= map->ptt_bit;
    }
    return high;
}

const char *key_output_root_warning(const char *backend, unsigned long euid) {
    if (backend == NULL || strcmp(backend, "serial") != 0 || euid != 0ul) {
        return NULL;
    }
    return "attenzione: cwnetd gira come root. Il lock esclusivo sulla porta non "
           "ferma un altro processo root, e la sua apertura alza DTR e RTS sotto "
           "questo daemon";
}

/*===========================================================================*/
/* The real OS calls (KTD8)                                                  */
/*===========================================================================*/

/* open() and ioctl() are variadic, so they cannot sit in the table as they
 * are. */
static int posix_open(const char *path, int flags) {
    return open(path, flags);
}

static int posix_ioctl_int(int fd, unsigned long request, int *arg) {
    return ioctl(fd, request, arg);
}

static int posix_ioctl_none(int fd, unsigned long request) {
    return ioctl(fd, request);
}

static int64_t posix_now_us(void) {
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + (int64_t)ts.tv_nsec / 1000;
}

const key_output_os_t key_output_os_posix = {
    .open       = posix_open,
    .close      = close,
    .ioctl_int  = posix_ioctl_int,
    .ioctl_none = posix_ioctl_none,
    .tcgetattr  = tcgetattr,
    .tcsetattr  = tcsetattr,
    .flock      = flock,
    .read       = read,
    .now_us     = posix_now_us,
#if defined(__linux__)
    .b0_at_rest = true,
#else
    .b0_at_rest = false,
#endif
};

/*===========================================================================*/
/* Edges (KTD3, KTD4, KTD5)                                                  */
/*===========================================================================*/

static void record_fault(key_output_t *out, key_output_fault_kind_t kind, const char *call,
                         int err, int64_t duration_us) {
    out->fault.set = true;
    out->fault.kind = kind;
    out->fault.call = call;
    out->fault.err = err;
    out->fault.duration_us = duration_us;
}

/*
 * Both lines to the levels of the current function state, in one TIOCMSET:
 * one control transfer on FTDI, so key and PTT never pass through a
 * combination nobody asked for. key_output_set_*() has already updated the
 * state. The call is timed, and a failure or a call over the PTT tail is
 * recorded (R7).
 *
 * After a failure nothing drives a line again: the state the port is in is
 * unknown, a release could drop PTT under a key still down, and edges held
 * behind a stalled call must not go out back to back. Only the close drops
 * the lines, both together, through HUPCL (KTD4).
 */
static void serial_drive(key_output_t *out) {
    if (out->fault.set || out->fd < 0) {
        return;
    }
    int lines = (int)key_output_levels(&out->map, out->key_down, out->ptt_on);
    int64_t t0 = out->os->now_us();
    int rc = out->os->ioctl_int(out->fd, (unsigned long)TIOCMSET, &lines);
    int err = errno;
    int64_t took = out->os->now_us() - t0;

    out->timing.count++;
    if (took > out->timing.max_us) {
        out->timing.max_us = took;
    }
    if (took > KEY_OUTPUT_SERIAL_SLOW_US) {
        out->timing.slow++;
    }
    if (rc != 0) {
        record_fault(out, KEY_OUTPUT_FAULT_CALL, "TIOCMSET", err, took);
    } else if (took > KEY_OUTPUT_SERIAL_SLOW_US) {
        record_fault(out, KEY_OUTPUT_FAULT_SLOW, "TIOCMSET", 0, took);
    }
}

/* The line first, because that is the timing that matters; then the edge,
 * written even when the change failed, so the trace shows where it
 * stopped (R8). */
static void serial_key(key_output_t *out, bool down, int64_t at_ms) {
    serial_drive(out);
    key_output_edge_line(out, "key", down, at_ms);
}

static void serial_ptt(key_output_t *out, bool on, int64_t at_ms) {
    serial_drive(out);
    key_output_edge_line(out, "ptt", on, at_ms);
}

/* Hang-up, or bytes from the device. The keying interface sends nothing,
 * a rig's own USB port may: those bytes are read and dropped, or the tty
 * stays readable and the loop's poll never sleeps. */
static void serial_service(key_output_t *out, bool hangup) {
    if (out->fd < 0 || out->fault.set) {
        return;
    }
    if (hangup) {
        record_fault(out, KEY_OUTPUT_FAULT_HANGUP, "poll", 0, 0);
        return;
    }
    char buf[256];
    for (int i = 0; i < KEY_OUTPUT_SERIAL_MAX_READS; i++) {
        ssize_t n = out->os->read(out->fd, buf, sizeof(buf));
        if (n > 0) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            return;
        }
        /* 0 is end of file: the tty was hung up under us (Linux). */
        record_fault(out, KEY_OUTPUT_FAULT_READ, "read", (n < 0) ? errno : 0, 0);
        return;
    }
}

/* After the release. Closing the last descriptor is what makes the kernel
 * drop both lines (HUPCL): on the normal path they are already at rest,
 * after a failure this is the only release there is. */
static void serial_finish(key_output_t *out) {
    if (out->fd >= 0) {
        (void)out->os->close(out->fd);
        out->fd = -1;
    }
}

/*===========================================================================*/
/* Open (KTD2)                                                               */
/*===========================================================================*/

/* Closes what a failed open left, and writes why. The descriptor goes with
 * no further line call: the lines drop on this close through the HUPCL
 * that tty_std_termios sets by default, or through ours once it is in. */
static bool open_failed(key_output_t *out, int fd, char *err, size_t err_len,
                        const char *device, const char *call, int e) {
    if (fd >= 0) {
        (void)out->os->close(fd);
    }
    out->fd = -1;
    (void)snprintf(err, err_len, "%s: %s: %s", device, call, strerror(e));
    return false;
}

/* open() itself: the errors an operator can act on, told apart. */
static bool open_refused(key_output_t *out, char *err, size_t err_len,
                         const char *device, int e) {
    const char *why;
    switch (e) {
        case ENOENT:  why = "non esiste (adattatore scollegato? nome sbagliato?)"; break;
        case EACCES:  why = "permesso negato (su Linux: gruppo dialout)"; break;
        case EBUSY:   why = "occupata: un altro processo ha la porta"; break;
        default:      why = strerror(e); break;
    }
    out->fd = -1;
    (void)snprintf(err, err_len, "%s: %s", device, why);
    return false;
}

bool key_output_serial_open(key_output_t *out, const char *device,
                            const key_output_map_t *map, const key_output_os_t *os,
                            char *err, size_t err_len) {
    out->name = "serial";
    out->fd = -1;
    out->map = *map;
    out->os = os;

    /* 1. Non-blocking, so the open does not wait for carrier; no
     *    controlling terminal; not inherited by anything we exec. */
    int fd = os->open(device, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return open_refused(out, err, err_len, device, errno);
    }

    /* 2. Both lines to rest in one call, before anything else. The open
     *    has just raised them, on macOS always and on Linux unless the
     *    saved speed is B0. */
    int rest = (int)key_output_levels(map, false, false);
    int lines = rest;
    if (os->ioctl_int(fd, (unsigned long)TIOCMSET, &lines) != 0) {
        int e = errno;
        /* ENOTTY on Linux, ENODEV on macOS (/dev/null), EINVAL from a pty. */
        if (e == ENOTTY || e == ENODEV || e == EINVAL) {
            (void)os->close(fd);
            (void)snprintf(err, err_len, "%s: non e' una porta seriale (TIOCMSET: %s)",
                           device, strerror(e));
            return false;
        }
        return open_failed(out, fd, err, err_len, device, "TIOCMSET", e);
    }

    /* 3. HUPCL: the last close drops both lines, SIGKILL included.
     *    CLOCAL: no carrier to wait for.
     *    No hardware flow control: with it the driver, or the chip, drives
     *    RTS itself, and RTS is the PTT. Linux keeps termios across closes,
     *    so whatever the last program left is still set.
     *    Raw, VMIN 1: poll() reports each received byte, which
     *    serial_service() drops, and nothing is echoed back to the rig.
     *    B0 where it helps (b0_at_rest); never with an inverted line,
     *    where a dropped line is active. */
    struct termios t;
    if (os->tcgetattr(fd, &t) != 0) {
        return open_failed(out, fd, err, err_len, device, "tcgetattr", errno);
    }
    t.c_cflag |= (tcflag_t)(HUPCL | CLOCAL);
    t.c_cflag &= ~(tcflag_t)CRTSCTS;
#if defined(CDTR_IFLOW)
    t.c_cflag &= ~(tcflag_t)CDTR_IFLOW;
#endif
#if defined(CDSR_OFLOW)
    t.c_cflag &= ~(tcflag_t)CDSR_OFLOW;
#endif
    t.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ISIG | IEXTEN);
    t.c_iflag &= ~(tcflag_t)(IXON | IXOFF | IXANY | ICRNL | INLCR | IGNCR | ISTRIP);
    t.c_oflag &= ~(tcflag_t)OPOST;
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    bool inverted = (map->key_bit != 0u && map->key_inverted) ||
                    (map->ptt_bit != 0u && map->ptt_inverted);
    if (os->b0_at_rest && !inverted) {
        (void)cfsetispeed(&t, B0);
        (void)cfsetospeed(&t, B0);
    }
    if (os->tcsetattr(fd, TCSANOW, &t) != 0) {
        return open_failed(out, fd, err, err_len, device, "tcsetattr", errno);
    }

    /* 4. Ours alone. TIOCEXCL refuses a later open by anyone but root;
     *    flock catches a second cwnetd that got past it. */
    if (os->ioctl_none(fd, (unsigned long)TIOCEXCL) != 0) {
        int e = errno;
        if (e == EBUSY) {
            (void)os->close(fd);
            (void)snprintf(err, err_len, "%s: occupata: TIOCEXCL: %s", device, strerror(e));
            return false;
        }
        return open_failed(out, fd, err, err_len, device, "TIOCEXCL", e);
    }
    if (os->flock(fd, LOCK_EX | LOCK_NB) != 0) {
        int e = errno;
        if (e == EWOULDBLOCK) {
            (void)os->close(fd);
            (void)snprintf(err, err_len, "%s: occupata: un altro processo ha il lock", device);
            return false;
        }
        return open_failed(out, fd, err, err_len, device, "flock", e);
    }

    /* 5. What the port says it holds. CTS, DSR and the other inputs are
     *    not ours and are masked out. */
    int got = 0;
    if (os->ioctl_int(fd, (unsigned long)TIOCMGET, &got) != 0) {
        return open_failed(out, fd, err, err_len, device, "TIOCMGET", errno);
    }
    int mine = (int)(TIOCM_DTR | TIOCM_RTS);
    if ((got & mine) != rest) {
        (void)os->close(fd);
        (void)snprintf(err, err_len,
                       "%s: le linee lette (DTR %d RTS %d) non sono quelle di riposo "
                       "(DTR %d RTS %d)",
                       device, (got & TIOCM_DTR) != 0, (got & TIOCM_RTS) != 0,
                       (rest & TIOCM_DTR) != 0, (rest & TIOCM_RTS) != 0);
        return false;
    }

    out->fd = fd;
    out->apply_key = serial_key;
    out->apply_ptt = serial_ptt;
    out->finish = serial_finish;
    out->service = serial_service;
    return true;
}
