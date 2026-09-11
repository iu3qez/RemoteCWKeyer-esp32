/**
 * @file main.c
 * @brief cwnetd: the CWNet station daemon.
 *
 * Everything the station side needs to be a server lives in the pure core
 * (components/keyer_cwnet/cwnet_server.c and cwnet_play.c, KTD1). This file
 * is the part that cannot be pure: sockets, the clock, the command line, and
 * stdout. It owns no protocol knowledge at all — it carries bytes in, edges
 * out, and prints what the core reports (KTD10: the core does not log).
 *
 * The loop (KTD7)
 * ---------------
 * One thread, absolute deadlines. sock_poll() waits until the *next*
 * deadline the core names, never a fixed tick: a 10 ms tick would put up to
 * 10 ms of jitter on every edge of a 20 ms dot, which is exactly the defect
 * this program exists to avoid. CLOCK_MONOTONIC through clock_now_ms(),
 * 64-bit milliseconds so nothing wraps; the 31-bit wire clock stays inside
 * the core. TCP_NODELAY is host/platform's business. SIGPIPE is ignored
 * process-wide here, because a client that dies mid-write must not kill the
 * station. Foreground, no fork: systemd and launchd want it that way, and
 * the unit files are Deferred work.
 *
 * Nothing in this loop may block on something a peer controls:
 *   - stdout is non-blocking, and a line that does not fit is dropped and
 *     counted rather than stalling the timing (R12);
 *   - the key and PTT edges do NOT go there. They have a descriptor of
 *     their own (--edges, R11/KTD9) that never drops a line, because the
 *     jitter measurement reads exactly those lines and a trace that loses
 *     the edges it measures measures nothing. See edge_write() for how the
 *     two rules — never drop, never stall — are reconciled;
 *   - every client has a ceiling on unsent bytes; over it, that client is
 *     closed and the others keep their PING and TX_INFO on time;
 *   - text that came from a client is escaped before it reaches stdout, so
 *     a callsign carrying ESC[2J cannot clear the operator's terminal.
 *
 * Events are diagnostics, the state is the truth
 * ----------------------------------------------
 * A cwnet_server_result_t carries at most CWNET_SERVER_MAX_EVENTS and counts
 * the rest in `dropped`; a single burst of keying schedules more than that.
 * So the edges are never what the output is built from: after every batch of
 * events the key and the PTT are aligned to the state the core declares
 * (reconcile_output()). A lost key-up must not leave the transmitter keyed —
 * corrupted timing is worse than silence, and a key stuck down is worse than
 * both (ARCHITECTURE.md 8.1).
 *
 * SIGINT and SIGTERM close the clients and put the output back to rest. The
 * key is never left down on the way out.
 */
#include "key_output.h"

#include "../platform/clock.h"
#include "../platform/sock.h"

#include "cwnet_server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/*===========================================================================*/
/* Sizes                                                                     */
/*===========================================================================*/

/** Default TCP port: the reference's, and parameters.yaml's since 2026-09-08 */
#define CWNETD_DEFAULT_PORT 7355u

/** listen(2) backlog. Past cfg.max_clients we accept and close at once (R1). */
#define CWNETD_BACKLOG 8

/** One read from a socket per pass round the FIFO */
#define CWNETD_RECV_CHUNK 4096u

/**
 * Reads from one client in a single pass of the loop at most.
 *
 * Without a ceiling, a peer that keeps its socket full holds the only thread
 * inside do_recv(): playback does not run, the safety nets do not fire, and
 * the timeline stands still for as long as the peer cares to push. Bounded,
 * the backlog is taken over several passes instead — the socket stays
 * readable, so the next sock_poll() returns at once and nothing is lost.
 * Same shape as CWNET_FEED_MAX_EDGES_PER_PASS on the box side.
 */
#define CWNETD_RECV_MAX_PASSES 8u

/**
 * Unsent bytes one client may accumulate before we give up on it (R13).
 *
 * The server sends a client tens of bytes every two seconds (a PING, the odd
 * TX_INFO); 16 KiB is minutes of backlog. A peer that has stopped reading
 * for that long is not coming back, and holding its bytes any longer only
 * costs the others. --out-cap moves it, which is also how the "reader
 * stopped" path gets exercised without pushing a hundred kilobytes through
 * a kernel socket buffer first.
 */
#define CWNETD_DEFAULT_OUT_CAP 16384u

/** Smallest --out-cap that still holds the longest frame the server sends */
#define CWNETD_MIN_OUT_CAP 256u

/** Largest --out-cap: past this the ceiling is not a safety net any more */
#define CWNETD_MAX_OUT_CAP (16u * 1024u * 1024u)

/** How long edge_write() waits on a full descriptor before saying so */
#define CWNETD_EDGE_WAIT_MS 100

/**
 * How long edge_write() keeps waiting once a shutdown has been asked for.
 *
 * Never dropping an edge is the rule while the station is on the air. It
 * cannot also be the rule on the way out, or a sink nobody is draining
 * makes the daemon unkillable by anything short of SIGKILL. So: unbounded
 * patience until SIGINT/SIGTERM, one second of it afterwards.
 */
#define CWNETD_EDGE_STOP_WAIT_MS 1000

/** Longest status line. Kept well under PIPE_BUF: see status_line(). */
#define CWNETD_LINE_MAX 256u

/** "255.255.255.255:65535" and room to spare */
#define CWNETD_PEER_LEN 32u

/** A sanitised name: worst case four characters ("\xNN") per source byte */
#define CWNETD_SAFE_NAME_LEN (CWNET_SERVER_NAME_LEN * 4u)

/*===========================================================================*/
/* State                                                                     */
/*===========================================================================*/

/**
 * @brief One accepted connection: the socket the core does not know about.
 *
 * Indexed by the core's client index minus CWNET_SERVER_FIRST_CLIENT, so the
 * two views never need a lookup table.
 */
typedef struct {
    bool in_use;
    sock_handle_t sock;
    char peer[CWNETD_PEER_LEN];        /**< Address, for the status lines */
    char name[CWNETD_SAFE_NAME_LEN];   /**< Announced name, already escaped */
    uint8_t *out;                      /**< Bytes the core handed us, unsent */
    size_t out_len;
} conn_t;

static conn_t g_conn[CWNET_SERVER_MAX_CLIENTS];
static cwnet_server_t g_srv;
static key_output_t g_out;

/** --out-cap: the size of every conn_t::out, allocated once at start-up */
static size_t g_out_cap = CWNETD_DEFAULT_OUT_CAP;

/** Set by the signal handler; the loop reads it and stops. */
static volatile sig_atomic_t g_stop = 0;

/** Status lines stdout would not take, and how many of those we have said. */
static unsigned long g_lines_dropped = 0;
static unsigned long g_lines_reported = 0;

/*===========================================================================*/
/* stdout: never block the timing loop                                       */
/*===========================================================================*/

/* One write(2) of the whole line. On a pipe, a non-blocking write below
 * PIPE_BUF either takes everything or takes nothing (POSIX), so a reader
 * that stops leaves whole lines missing rather than half a line — hence
 * CWNETD_LINE_MAX being small. */
static bool write_line(const char *buf, size_t len) {
    for (;;) {
        ssize_t n = write(STDOUT_FILENO, buf, len);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return (n >= 0) && ((size_t)n == len);
    }
}

/**
 * @brief One status line, dropped rather than blocking (R12, KTD7)
 *
 * Before a line goes out, any backlog of dropped ones is confessed: the
 * operator has to be able to tell "nothing happened" from "you were not
 * reading".
 */
static void status_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void status_line(const char *fmt, ...) {
    char line[CWNETD_LINE_MAX];

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line) - 1u, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }

    size_t len = (size_t)n;
    if (len > sizeof(line) - 2u) {
        len = sizeof(line) - 2u; /* vsnprintf truncated: so do we */
    }
    line[len] = '\n';
    len++;

    if (g_lines_dropped != g_lines_reported) {
        char note[CWNETD_LINE_MAX];
        int m = snprintf(note, sizeof(note),
                         "stato stdout %lu righe scartate\n", g_lines_dropped);
        if (m > 0 && write_line(note, (size_t)m)) {
            g_lines_reported = g_lines_dropped;
        }
    }

    if (!write_line(line, len)) {
        g_lines_dropped++;
    }
}

/*===========================================================================*/
/* The edge descriptor: never drops (R11, KTD9)                              */
/*===========================================================================*/

/*
 * Two rules that pull against each other, and how they are reconciled.
 *
 * R11 says the edges go to a descriptor of their own that never drops. KTD7
 * says nothing may stall the timing loop. A descriptor that never drops can
 * block, so the two cannot both hold unconditionally against a sink that
 * has stopped draining. The order of precedence here is R11's, and the
 * reason is what the lines are FOR: they are the jitter measurement, and a
 * trace that quietly loses the edges it measures is not a worse measurement
 * but a wrong one.
 *
 * So: never drop, and make the cost visible instead of hiding it.
 *   - A regular file cannot stall this: write(2) to one never reports
 *     EAGAIN and has no reader to wait for. That is the descriptor to point
 *     --edges at when the numbers matter, and the README says so.
 *   - A pipe or a terminal can. The write is retried through EINTR, short
 *     writes and EAGAIN (poll for POLLOUT), so the line always lands in the
 *     end — and every millisecond spent waiting is counted and confessed on
 *     a status line, which is droppable and therefore cannot itself stall.
 *     A pipe nobody reads costs nothing until its buffer fills (16-64 KiB,
 *     some thousands of edges) and stalls the loop from then on. That is a
 *     real defect of that setup and it now reports itself rather than
 *     showing up as inexplicable jitter.
 *   - The flags of the descriptor are never touched. stderr may share its
 *     open file description with our own non-blocking stdout (the shell's
 *     "2>&1" does exactly that), and clearing O_NONBLOCK on one would clear
 *     it on the other, quietly making the status lines blocking. Handling
 *     EAGAIN instead works whatever we inherited.
 */

static int g_edge_fd = STDERR_FILENO;
static const char *g_edge_name = "stderr";
static bool g_edge_owned = false;       /**< A file we opened, so we close it */

static unsigned long g_edge_stalls = 0;      /**< Edges that had to wait */
static int64_t g_edge_stall_ms = 0;          /**< How long they waited, in total */
static unsigned long g_edge_errors = 0;      /**< Edges the descriptor refused */
static unsigned long g_edge_given_up = 0;    /**< Edges dropped while shutting down */

/**
 * @brief One edge line on the edge descriptor. Does not return until it is
 *        written — see the note above for the one exception.
 */
static void edge_write(void *ctx, const char *line) {
    (void)ctx;

    char buf[CWNETD_LINE_MAX];
    int n = snprintf(buf, sizeof(buf), "%s\n", line);
    if (n <= 0) {
        return;
    }
    size_t len = (size_t)n;
    if (len > sizeof(buf) - 1u) {
        len = sizeof(buf) - 1u;  /* snprintf truncated: write what it kept */
    }

    size_t off = 0;
    int64_t wait_started_ms = 0;
    bool waited = false;
    bool said_so = false;

    while (off < len) {
        ssize_t w = write(g_edge_fd, buf + off, len - off);
        if (w > 0) {
            off += (size_t)w;
            continue;
        }
        if (w < 0 && errno == EINTR) {
            continue;
        }
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!waited) {
                wait_started_ms = (int64_t)clock_now_ms();
                waited = true;
            }
            struct pollfd p;
            p.fd = g_edge_fd;
            p.events = POLLOUT;
            p.revents = 0;
            (void)poll(&p, 1, CWNETD_EDGE_WAIT_MS);

            int64_t waited_ms = (int64_t)clock_now_ms() - wait_started_ms;
            if (!said_so && waited_ms >= CWNETD_EDGE_WAIT_MS) {
                /* Live, while still stuck: the operator sees the cause of
                 * the jitter as it happens, not in the totals afterwards. */
                status_line("stato uscita fronti (%s) non drena: %lld ms e aspetto",
                            g_edge_name, (long long)waited_ms);
                said_so = true;
            }
            if (g_stop != 0 && waited_ms >= CWNETD_EDGE_STOP_WAIT_MS) {
                g_edge_given_up++;
                return;
            }
            continue;
        }
        /* Anything else — EPIPE, ENOSPC, a closed descriptor — cannot be
         * waited out. Count it; the next edge tries again, because a full
         * disk that gets emptied is a sink that comes back. */
        g_edge_errors++;
        status_line("stato uscita fronti (%s) rifiuta: %s", g_edge_name, strerror(errno));
        return;
    }

    if (waited) {
        g_edge_stalls++;
        g_edge_stall_ms += (int64_t)clock_now_ms() - wait_started_ms;
    }
}

/**
 * @brief Point the edges at "stderr" or at a file. Called before the loop.
 */
static bool edge_open(const char *dest) {
    if (dest == NULL) {
        return false;
    }
    if (strcmp(dest, "stderr") == 0) {
        g_edge_fd = STDERR_FILENO;
        g_edge_name = "stderr";
        g_edge_owned = false;
        return true;
    }
    if (strcmp(dest, "stdout") == 0 || strcmp(dest, "-") == 0) {
        /* R11 wants the edges separated from the status lines, and stdout
         * is the one descriptor that cannot give them that. Refusing is
         * kinder than creating a file called "stdout". */
        fprintf(stderr, "cwnetd: --edges non puo' essere stdout: i fronti vogliono un "
                        "descrittore separato dalle righe di stato (R11). Usa 'stderr' "
                        "o un file.\n");
        return false;
    }
    /* Append: a restart adds to the trace, it does not erase the one that
     * showed why the daemon was restarted. O_NONBLOCK: on a regular file it
     * changes nothing, and on a FIFO it is the difference between failing
     * with a message and hanging in open(2) until somebody opens the other
     * end. What the flag would otherwise cost — a write that returns EAGAIN
     * — edge_write() already handles, because it has to handle an inherited
     * stderr that carries it. */
    int fd = open(dest, O_WRONLY | O_CREAT | O_APPEND | O_NONBLOCK, 0644);
    if (fd < 0) {
        fprintf(stderr, "cwnetd: --edges %s: %s\n", dest, strerror(errno));
        return false;
    }
    g_edge_fd = fd;
    g_edge_name = dest;
    g_edge_owned = true;
    return true;
}

static void edge_close(void) {
    if (g_edge_owned && g_edge_fd >= 0) {
        (void)close(g_edge_fd);
    }
    g_edge_fd = -1;
    g_edge_owned = false;
}

/**
 * @brief Escape anything that came from a peer before it reaches a terminal
 *
 * Printable ASCII survives; everything else — control bytes, the ESC that
 * starts a CSI sequence, high bytes — becomes "\xNN", and a backslash is
 * doubled so the escaping is unambiguous. R12: "le stringhe che vengono dal
 * client escono con i byte di controllo e le sequenze di escape rimossi".
 * Removing ESC alone would not do: it is the byte that arms the sequence, so
 * escaping it disarms the whole of it.
 */
static void sanitize(char *dst, size_t dst_size, const char *src) {
    if (dst == NULL || dst_size == 0u) {
        return;
    }
    size_t o = 0;
    for (size_t i = 0; src != NULL && src[i] != '\0'; i++) {
        unsigned char c = (unsigned char)src[i];
        char esc[5];
        size_t need;

        if (c == (unsigned char)'\\') {
            esc[0] = '\\';
            esc[1] = '\\';
            need = 2u;
        } else if (c >= 0x20u && c < 0x7Fu) {
            esc[0] = (char)c;
            need = 1u;
        } else {
            (void)snprintf(esc, sizeof(esc), "\\x%02X", (unsigned)c);
            need = 4u;
        }

        if (o + need + 1u > dst_size) {
            break;
        }
        memcpy(dst + o, esc, need);
        o += need;
    }
    dst[o] = '\0';
}

/*===========================================================================*/
/* Connections                                                               */
/*===========================================================================*/

static conn_t *conn_of(int client_idx) {
    int i = client_idx - CWNET_SERVER_FIRST_CLIENT;
    if (i < 0 || i >= CWNET_SERVER_MAX_CLIENTS) {
        return NULL;
    }
    return &g_conn[i];
}

/** Push out whatever the socket will take right now. Never blocks. */
static void conn_flush(conn_t *c) {
    while (c->out_len > 0u) {
        int n = sock_send(c->sock, c->out, c->out_len);
        if (n <= 0) {
            /* Would block: the poll set asks for SOCK_POLLOUT and we come
             * back. A hard error surfaces on the next recv() as a peer that
             * has gone, which is the one place a close is decided. */
            return;
        }
        size_t sent = (size_t)n;
        if (sent >= c->out_len) {
            c->out_len = 0;
            return;
        }
        memmove(c->out, c->out + sent, c->out_len - sent);
        c->out_len -= sent;
    }
}

/**
 * @brief The core's only way out (KTD1)
 *
 * Queue, then try to drain. Returning anything but @p len makes the core
 * close this client, which is exactly what a peer over the ceiling deserves:
 * the loop is not going to wait for it.
 */
static int send_cb(int client_idx, const uint8_t *data, size_t len, void *user_data) {
    (void)user_data;
    conn_t *c = conn_of(client_idx);
    if (c == NULL || !c->in_use) {
        return -1;
    }
    if (c->out == NULL || len > g_out_cap - c->out_len) {
        status_line("stato client %d lettore fermo: %zu byte non inviati, chiudo",
                    client_idx, c->out_len);
        return -1;
    }
    memcpy(c->out + c->out_len, data, len);
    c->out_len += len;
    conn_flush(c);
    return (int)len;
}

static void conn_drop(int client_idx) {
    conn_t *c = conn_of(client_idx);
    if (c == NULL || !c->in_use) {
        return;
    }
    sock_close(&c->sock);
    c->in_use = false;
    c->out_len = 0;
}

/*===========================================================================*/
/* Naming what the core reports                                              */
/*===========================================================================*/

static const char *close_reason_name(int32_t v) {
    switch ((cwnet_server_close_reason_t)v) {
        case CWNET_SERVER_CLOSE_NO_SLOT:      return "nessuno slot libero";
        case CWNET_SERVER_CLOSE_BAD_CONNECT:  return "CONNECT di lunghezza sbagliata";
        case CWNET_SERVER_CLOSE_PARSE_ERROR:  return "errore di parse";
        case CWNET_SERVER_CLOSE_BAD_STRING:   return "stringa 0x06 senza NUL";
        case CWNET_SERVER_CLOSE_HANDSHAKE:    return "CONNECT non arrivato in tempo";
        case CWNET_SERVER_CLOSE_PING_TIMEOUT: return "tre PING senza risposta";
        case CWNET_SERVER_CLOSE_SEND_FAILED:  return "invio fallito";
        case CWNET_SERVER_CLOSE_PEER:         return "chiuso dal peer";
        default:                              return "?";
    }
}

static const char *fault_name(int32_t v) {
    switch ((cwnet_server_fault_t)v) {
        case CWNET_SERVER_FAULT_HOLDER_GONE:   return "titolare sparito a meta' over";
        case CWNET_SERVER_FAULT_IDLE:          return "titolare muto oltre l'inattivita' col tasto su";
        case CWNET_SERVER_FAULT_OVER_TOO_LONG: return "over oltre il tetto";
        default:                               return "?";
    }
}

static const char *name_of(int client_idx) {
    conn_t *c = conn_of(client_idx);
    if (c == NULL || c->name[0] == '\0') {
        return "(senza nome)";
    }
    return c->name;
}

/**
 * @brief Align the output to the state the core declares
 *
 * The edges are not the authority: a result holds at most
 * CWNET_SERVER_MAX_EVENTS, and one burst of keying can schedule more, so a
 * key-up can be counted in `dropped` instead of reported. Nothing else
 * drives key_output, and the core never re-sends an edge it has already
 * applied — the key would stay down for the rest of the session. Hence this,
 * after every batch: two comparisons against the truth.
 *
 * key_output_set_*() drops a write that changes nothing, so on the normal
 * path it prints nothing at all. The order is key_output_release()'s, and for
 * the same reason: raise the PTT before the key goes down, and lower the key
 * before the PTT goes away. Never a key down with no PTT behind it.
 *
 * @param now_ms The correction happens now; it is not a scheduled instant.
 */
static void reconcile_output(int64_t now_ms) {
    bool key = cwnet_server_key_down(&g_srv);
    bool ptt = cwnet_server_ptt_on(&g_srv);

    if (ptt) {
        key_output_set_ptt(&g_out, true, now_ms);
        key_output_set_key(&g_out, key, now_ms);
    } else {
        key_output_set_key(&g_out, key, now_ms);
        key_output_set_ptt(&g_out, false, now_ms);
    }
}

/**
 * @brief Print what the core did, and put its keying edges on the output
 *
 * The core reports; the daemon decides what that looks like (KTD10). Every
 * line carries the client index, so two clients never blur together, and the
 * instants are the scheduled ones, so an over can be reconstructed from the
 * lines alone (U6 Verification).
 *
 * @param now_ms The instant of the call, for the reconciliation that closes
 *               it — not for the events, which carry their own.
 */
static void handle_events(const cwnet_server_result_t *res, int64_t now_ms) {
    for (size_t i = 0; i < res->count; i++) {
        const cwnet_server_event_t *e = &res->ev[i];
        switch (e->type) {
            case CWNET_SERVER_EV_CLIENT_READY: {
                conn_t *c = conn_of(e->client_idx);
                if (c != NULL) {
                    sanitize(c->name, sizeof(c->name),
                             cwnet_server_client_name(&g_srv, e->client_idx));
                    status_line("stato connesso client %d %s da %s",
                                e->client_idx, c->name, c->peer);
                }
                break;
            }
            case CWNET_SERVER_EV_CLIENT_CLOSED: {
                if (e->client_idx == 0) {
                    /* Refused past the client limit; the accept site said so
                     * already, with the address this one does not carry. */
                    break;
                }
                conn_t *c = conn_of(e->client_idx);
                status_line("stato disconnesso client %d %s da %s: %s",
                            e->client_idx, name_of(e->client_idx),
                            (c != NULL) ? c->peer : "?",
                            close_reason_name(e->value));
                conn_drop(e->client_idx);
                break;
            }
            case CWNET_SERVER_EV_KEY_HOLDER:
                if (e->client_idx == CWNET_SERVER_NOBODY) {
                    status_line("stato chiave libera");
                } else {
                    status_line("stato chiave client %d %s",
                                e->client_idx, name_of(e->client_idx));
                }
                break;
            case CWNET_SERVER_EV_LATENCY:
                status_line("stato latenza client %d %d ms peak %d ms",
                            e->client_idx, e->value, e->peak_ms);
                break;
            case CWNET_SERVER_EV_LINK_UNFIT:
                status_line("stato link non idoneo client %d %s: peak %d ms",
                            e->client_idx, name_of(e->client_idx), e->value);
                break;
            case CWNET_SERVER_EV_OVER_BUFFER:
                status_line("stato over client %d %s B %d ms",
                            e->client_idx, name_of(e->client_idx), e->value);
                break;
            case CWNET_SERVER_EV_KEY_DOWN:
                key_output_set_key(&g_out, true, e->at_ms);
                break;
            case CWNET_SERVER_EV_KEY_UP:
                key_output_set_key(&g_out, false, e->at_ms);
                break;
            case CWNET_SERVER_EV_PTT_ON:
                key_output_set_ptt(&g_out, true, e->at_ms);
                break;
            case CWNET_SERVER_EV_PTT_OFF:
                key_output_set_ptt(&g_out, false, e->at_ms);
                break;
            case CWNET_SERVER_EV_LATE_BYTE:
                /* The link is slipping: a byte turned up after the edge it
                 * carried was due, so that element went out longer than the
                 * operator keyed it. Cumulative, so two lines apart say how
                 * fast it is slipping. It is the warning that comes before
                 * the link slips far enough to stop answering the PINGs. */
                status_line("stato byte in ritardo client %d %s: %d byte, %d ms in totale",
                            e->client_idx, name_of(e->client_idx), e->value, e->peak_ms);
                break;
            case CWNET_SERVER_EV_FAULT:
                /* A safety net that has already freed the key reports it
                 * against CWNET_SERVER_NOBODY; the client is named on the
                 * line above it, so say "nobody" rather than "client -1". */
                if (e->client_idx == CWNET_SERVER_NOBODY) {
                    status_line("stato fault: %s", fault_name(e->value));
                } else {
                    status_line("stato fault client %d %s: %s", e->client_idx,
                                name_of(e->client_idx), fault_name(e->value));
                }
                break;
            default:
                break;
        }
    }
    if (res->dropped > 0u) {
        status_line("stato eventi persi %zu", res->dropped);
    }
    reconcile_output(now_ms);
}

/*===========================================================================*/
/* Socket work                                                               */
/*===========================================================================*/

static void do_accept(sock_handle_t listener, int64_t now_ms) {
    for (;;) {
        sock_handle_t h = SOCK_INVALID;
        if (!sock_accept(listener, &h)) {
            /* "Nothing pending" and "the listener is in trouble" arrive the
             * same way; every other error path here says its piece, and a
             * station that has quietly stopped accepting is worth a line.
             * One per pass at most: we return either way. */
            int err = sock_last_error();
            if (!sock_would_block(err) && err != EINTR) {
                status_line("stato accept fallita: %s", strerror(err));
            }
            return;
        }

        cwnet_server_result_t res;
        int idx = cwnet_server_on_connected(&g_srv, now_ms, &res);
        if (idx == CWNET_SERVER_NO_SLOT) {
            char peer[CWNETD_PEER_LEN];
            (void)sock_peer_string(h, peer, sizeof(peer));
            sock_close(&h);
            status_line("stato rifiutato da %s: nessuno slot libero", peer);
            continue; /* R1: accepted and closed, the accept never blocks */
        }

        conn_t *c = conn_of(idx);
        if (c == NULL) {
            sock_close(&h);
            continue;
        }
        if (c->in_use) {
            /* The core handed back a slot the daemon still believes is live:
             * the CLIENT_CLOSED that freed it was one of the events a full
             * result dropped. Overwriting c->sock here would leak the
             * descriptor and leave the old peer connected to nothing. Say so
             * — silence is the worse half of this — and close it properly. */
            status_line("stato slot client %d ancora in uso da %s: chiudo la "
                        "connessione precedente", idx, c->peer);
            conn_drop(idx);
        }
        c->in_use = true;
        c->sock = h;
        c->out_len = 0;
        c->name[0] = '\0';
        (void)sock_peer_string(h, c->peer, sizeof(c->peer));
        status_line("stato accettato client %d da %s", idx, c->peer);
        handle_events(&res, now_ms);
    }
}

static void do_disconnect(int client_idx, int64_t now_ms) {
    cwnet_server_result_t res;
    cwnet_server_on_disconnected(&g_srv, client_idx, now_ms, &res);
    handle_events(&res, now_ms);
    conn_drop(client_idx); /* no-op when the event above already did it */
}

static void do_recv(int client_idx, int64_t now_ms) {
    conn_t *c = conn_of(client_idx);
    if (c == NULL || !c->in_use) {
        return;
    }
    uint8_t buf[CWNETD_RECV_CHUNK];
    int64_t t_ms = now_ms;

    /* Bounded: a client that never lets the socket run short does not get to
     * own the thread. What it left behind keeps the socket readable, so the
     * next sock_poll() returns immediately and the reading resumes there —
     * with the playback and the safety nets having had their turn between. */
    for (unsigned pass = 0; pass < CWNETD_RECV_MAX_PASSES; pass++) {
        if (pass > 0u) {
            /* Re-read the clock every round: the core schedules against the
             * instant it is handed, and a burst must not freeze it. */
            t_ms = (int64_t)clock_now_ms();
        }

        int n = sock_recv(c->sock, buf, sizeof(buf));
        if (n > 0) {
            cwnet_server_result_t res;
            cwnet_server_on_data(&g_srv, client_idx, buf, (size_t)n, t_ms, &res);
            handle_events(&res, t_ms);
            if (!c->in_use) {
                return; /* the core closed it on us */
            }
            if ((size_t)n < sizeof(buf)) {
                return; /* drained */
            }
            continue;
        }
        if (n == 0) {
            do_disconnect(client_idx, t_ms);
            return;
        }
        if (sock_would_block(sock_last_error())) {
            return;
        }
        do_disconnect(client_idx, t_ms);
        return;
    }
}

/*===========================================================================*/
/* Signals                                                                   */
/*===========================================================================*/

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

/*
 * sigaction without SA_RESTART on purpose. signal() gives BSD semantics on
 * macOS, which restarts poll() and would leave a Ctrl-C sitting unnoticed in
 * a daemon that is idle — exactly the state it is in most of the time.
 */
static bool install_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) != 0 || sigaction(SIGTERM, &sa, NULL) != 0) {
        return false;
    }

    /* A client that dies while we are writing must not kill the station. */
    struct sigaction ign;
    memset(&ign, 0, sizeof(ign));
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    return sigaction(SIGPIPE, &ign, NULL) == 0;
}

/*===========================================================================*/
/* Command line (R13, KTD12)                                                 */
/*===========================================================================*/

typedef struct {
    const char *listen_addr;
    unsigned long port;
    unsigned long max_clients;
    unsigned long play_floor_ms;
    unsigned long link_ceiling_ms;
    unsigned long ptt_tail_ms;
    unsigned long ptt_lead_ms;
    unsigned long idle_ms;
    unsigned long over_max_ms;
    unsigned long handshake_ms;
    unsigned long out_cap;
    const char *output;
    const char *edges;
} args_t;

static void usage(const char *argv0, const args_t *d) {
    printf(
        "cwnetd - daemon di stazione CWNet\n"
        "\n"
        "Uso: %s [opzioni]\n"
        "\n"
        "  --listen ADDR       indirizzo IPv4 di ascolto (default %s: tutte le\n"
        "                      interfacce; il confine di fiducia e' la LAN o la VPN)\n"
        "  --port N            porta TCP (default %lu)\n"
        "  --max-clients N     client serviti insieme, max %d (default %lu)\n"
        "  --play-floor MS     pavimento del buffer B (default %lu)\n"
        "  --link-ceiling MS   peak-hold oltre cui il link non e' idoneo (default %lu)\n"
        "  --ptt-tail MS       coda del PTT dopo l'ultimo key-up (default %lu)\n"
        "  --ptt-lead MS       anticipo del PTT sul primo key-down, mai oltre B (default %lu)\n"
        "  --idle MS           silenzio del titolare col tasto su che rilascia la\n"
        "                      chiave (default %lu; col tasto giu' decide il PING)\n"
        "  --over-max MS       tetto di un over (default %lu)\n"
        "  --handshake MS      tempo per completare il CONNECT (default %lu)\n"
        "  --out-cap BYTE      byte non inviati per client oltre i quali lo chiudo\n"
        "                      (default %lu, min %lu, max %lu)\n"
        "  --output BACKEND    uscita di tasto e PTT: %s (default %s)\n"
        "  --edges DEST        descrittore dei fronti: 'stderr' o un file, mai stdout\n"
        "                      (default %s; il file si apre in append)\n"
        "  --help              questo testo\n"
        "\n"
        "Lo stato esce su stdout a righe, non bloccante: una riga che non entra si\n"
        "scarta e la successiva lo confessa. I fronti NO: escono sul descrittore di\n"
        "--edges, che non ne scarta mai uno, come \"key 1 <ms>\" / \"ptt 0 <ms>\" con\n"
        "l'istante per cui il fronte era programmato. Un file non puo' fermare il\n"
        "loop; una pipe che nessuno legge si', quando il suo buffer si riempie, e\n"
        "allora una riga di stato dice per quanto. SIGINT e SIGTERM chiudono i\n"
        "client e rilasciano l'uscita.\n",
        argv0, d->listen_addr, d->port, CWNET_SERVER_MAX_CLIENTS, d->max_clients,
        d->play_floor_ms, d->link_ceiling_ms, d->ptt_tail_ms, d->ptt_lead_ms,
        d->idle_ms, d->over_max_ms, d->handshake_ms,
        d->out_cap, (unsigned long)CWNETD_MIN_OUT_CAP, (unsigned long)CWNETD_MAX_OUT_CAP,
        key_output_backends(), d->output, d->edges);
}

static bool parse_ulong(const char *s, unsigned long max, unsigned long *out) {
    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v > max) {
        return false;
    }
    *out = v;
    return true;
}

enum {
    OPT_LISTEN = 1000, OPT_PORT, OPT_MAX_CLIENTS, OPT_PLAY_FLOOR, OPT_LINK_CEILING,
    OPT_PTT_TAIL, OPT_PTT_LEAD, OPT_IDLE, OPT_OVER_MAX, OPT_HANDSHAKE,
    OPT_OUT_CAP, OPT_OUTPUT, OPT_EDGES,
    OPT_HELP
};

static bool parse_args(int argc, char **argv, args_t *a, bool *want_help) {
    static const struct option opts[] = {
        { "listen",       required_argument, NULL, OPT_LISTEN },
        { "port",         required_argument, NULL, OPT_PORT },
        { "max-clients",  required_argument, NULL, OPT_MAX_CLIENTS },
        { "play-floor",   required_argument, NULL, OPT_PLAY_FLOOR },
        { "link-ceiling", required_argument, NULL, OPT_LINK_CEILING },
        { "ptt-tail",     required_argument, NULL, OPT_PTT_TAIL },
        { "ptt-lead",     required_argument, NULL, OPT_PTT_LEAD },
        { "idle",         required_argument, NULL, OPT_IDLE },
        { "over-max",     required_argument, NULL, OPT_OVER_MAX },
        { "handshake",    required_argument, NULL, OPT_HANDSHAKE },
        { "out-cap",      required_argument, NULL, OPT_OUT_CAP },
        { "output",       required_argument, NULL, OPT_OUTPUT },
        { "edges",        required_argument, NULL, OPT_EDGES },
        { "help",         no_argument,       NULL, OPT_HELP },
        { NULL, 0, NULL, 0 },
    };

    *want_help = false;
    for (;;) {
        int long_idx = -1;
        int c = getopt_long(argc, argv, "h", opts, &long_idx);
        if (c == -1) {
            break;
        }
        bool ok = true;
        switch (c) {
            case OPT_LISTEN:       a->listen_addr = optarg; break;
            case OPT_PORT:         ok = parse_ulong(optarg, 65535u, &a->port); break;
            case OPT_MAX_CLIENTS:
                ok = parse_ulong(optarg, (unsigned long)CWNET_SERVER_MAX_CLIENTS,
                                 &a->max_clients) && a->max_clients > 0u;
                break;
            case OPT_PLAY_FLOOR:   ok = parse_ulong(optarg, 60000u, &a->play_floor_ms); break;
            case OPT_LINK_CEILING: ok = parse_ulong(optarg, 60000u, &a->link_ceiling_ms); break;
            case OPT_PTT_TAIL:     ok = parse_ulong(optarg, 60000u, &a->ptt_tail_ms); break;
            case OPT_PTT_LEAD:     ok = parse_ulong(optarg, 60000u, &a->ptt_lead_ms); break;
            case OPT_IDLE:         ok = parse_ulong(optarg, 3600000u, &a->idle_ms) && a->idle_ms > 0u; break;
            case OPT_OVER_MAX:     ok = parse_ulong(optarg, 3600000u, &a->over_max_ms) && a->over_max_ms > 0u; break;
            case OPT_HANDSHAKE:    ok = parse_ulong(optarg, 3600000u, &a->handshake_ms) && a->handshake_ms > 0u; break;
            case OPT_OUT_CAP:
                ok = parse_ulong(optarg, (unsigned long)CWNETD_MAX_OUT_CAP, &a->out_cap) &&
                     a->out_cap >= (unsigned long)CWNETD_MIN_OUT_CAP;
                break;
            case OPT_OUTPUT:       a->output = optarg; break;
            case OPT_EDGES:        a->edges = optarg; break;
            case OPT_HELP:
            case 'h':              *want_help = true; return true;
            default:               return false;
        }
        if (!ok) {
            fprintf(stderr, "cwnetd: valore non valido per --%s: %s\n",
                    (long_idx >= 0) ? opts[long_idx].name : "?",
                    (optarg != NULL) ? optarg : "");
            return false;
        }
    }
    if (optind < argc) {
        fprintf(stderr, "cwnetd: argomento inatteso: %s\n", argv[optind]);
        return false;
    }
    return true;
}

/*===========================================================================*/
/* main                                                                      */
/*===========================================================================*/

static void shutdown_clients(void) {
    for (int i = 0; i < CWNET_SERVER_MAX_CLIENTS; i++) {
        int idx = CWNET_SERVER_FIRST_CLIENT + i;
        conn_t *c = &g_conn[i];
        if (!c->in_use) {
            continue;
        }
        status_line("stato disconnesso client %d %s da %s: arresto",
                    idx, name_of(idx), c->peer);
        conn_drop(idx);
    }
}

int main(int argc, char **argv) {
    args_t args = {
        .listen_addr     = "0.0.0.0",
        .port            = CWNETD_DEFAULT_PORT,
        .max_clients     = CWNET_SERVER_DEFAULT_MAX_CLIENTS,
        .play_floor_ms   = CWNET_SERVER_DEFAULT_BUFFER_FLOOR_MS,
        .link_ceiling_ms = CWNET_SERVER_DEFAULT_BUFFER_CEILING_MS,
        .ptt_tail_ms     = CWNET_PLAY_DEFAULT_PTT_TAIL_MS,
        .ptt_lead_ms     = 0u,
        .idle_ms         = CWNET_SERVER_DEFAULT_IDLE_MS,
        .over_max_ms     = CWNET_SERVER_DEFAULT_OVER_MAX_MS,
        .handshake_ms    = CWNET_SERVER_DEFAULT_HANDSHAKE_MS,
        .out_cap         = CWNETD_DEFAULT_OUT_CAP,
        .output          = "virtual",
        .edges           = "stderr",
    };
    const args_t defaults = args;

    bool want_help = false;
    if (!parse_args(argc, argv, &args, &want_help)) {
        usage(argv[0], &defaults);
        return 2;
    }
    if (want_help) {
        usage(argv[0], &defaults);
        return 0;
    }

    /* stdout must never be what stalls the timing (KTD7) */
    int flags = fcntl(STDOUT_FILENO, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(STDOUT_FILENO, F_SETFL, flags | O_NONBLOCK);
    }

    if (!edge_open(args.edges)) {
        return 2;
    }
    g_out_cap = (size_t)args.out_cap;
    for (int i = 0; i < CWNET_SERVER_MAX_CLIENTS; i++) {
        g_conn[i].out = malloc(g_out_cap);
        if (g_conn[i].out == NULL) {
            fprintf(stderr, "cwnetd: --out-cap %lu: memoria insufficiente\n", args.out_cap);
            return 1;
        }
    }

    if (!key_output_open(&g_out, args.output, edge_write, NULL)) {
        fprintf(stderr, "cwnetd: backend di uscita sconosciuto: %s (validi: %s)\n",
                args.output, key_output_backends());
        return 2;
    }
    if (!install_signals()) {
        fprintf(stderr, "cwnetd: sigaction: %s\n", strerror(errno));
        return 1;
    }
    if (!sock_init()) {
        fprintf(stderr, "cwnetd: sock_init fallita\n");
        return 1;
    }

    cwnet_server_cfg_t cfg;
    cwnet_server_cfg_defaults(&cfg);
    cfg.max_clients         = (uint16_t)args.max_clients;
    cfg.buffer_floor_ms     = (uint32_t)args.play_floor_ms;
    cfg.buffer_ceiling_ms   = (uint32_t)args.link_ceiling_ms;
    cfg.idle_timeout_ms     = (uint32_t)args.idle_ms;
    cfg.over_max_ms         = (uint32_t)args.over_max_ms;
    cfg.handshake_timeout_ms = (uint32_t)args.handshake_ms;
    cfg.play.ptt_tail_ms    = (uint32_t)args.ptt_tail_ms;
    cfg.play.ptt_lead_ms    = (uint32_t)args.ptt_lead_ms;
    cfg.send_cb             = send_cb;
    cfg.user_data           = NULL;
    if (!cwnet_server_init(&g_srv, &cfg)) {
        fprintf(stderr, "cwnetd: cwnet_server_init fallita\n");
        sock_cleanup();
        return 1;
    }

    sock_handle_t listener = sock_listen(args.listen_addr, (uint16_t)args.port,
                                          CWNETD_BACKLOG);
    if (!sock_valid(listener)) {
        fprintf(stderr, "cwnetd: ascolto su %s:%lu fallito: %s\n",
                args.listen_addr, args.port, strerror(sock_last_error()));
        sock_cleanup();
        return 1;
    }

    uint16_t bound = (uint16_t)args.port;
    (void)sock_local_port(listener, &bound);
    status_line("stato ascolto %s:%u max-clients %lu B>=%lu ms tetto %lu ms "
                "coda %lu ms lead %lu ms out-cap %lu byte",
                args.listen_addr, (unsigned)bound, args.max_clients,
                args.play_floor_ms, args.link_ceiling_ms, args.ptt_tail_ms,
                args.ptt_lead_ms, args.out_cap);
    /* Its own line: a path can be long, and truncating it must not take the
     * configuration with it. */
    status_line("stato uscita %s fronti %s", g_out.name, g_edge_name);

    while (g_stop == 0) {
        int64_t now_ms = (int64_t)clock_now_ms();

        sock_pollfd_t fds[1 + CWNET_SERVER_MAX_CLIENTS];
        int owner[1 + CWNET_SERVER_MAX_CLIENTS];
        size_t nfds = 0;

        fds[nfds].handle = listener;
        fds[nfds].events = SOCK_POLLIN;
        fds[nfds].revents = 0;
        owner[nfds] = 0;
        nfds++;

        for (int i = 0; i < CWNET_SERVER_MAX_CLIENTS; i++) {
            conn_t *c = &g_conn[i];
            if (!c->in_use) {
                continue;
            }
            fds[nfds].handle = c->sock;
            fds[nfds].events = (short)(SOCK_POLLIN |
                                       ((c->out_len > 0u) ? SOCK_POLLOUT : 0));
            fds[nfds].revents = 0;
            owner[nfds] = CWNET_SERVER_FIRST_CLIENT + i;
            nfds++;
        }

        /* The timeout is the next deadline the core names, never a tick */
        int timeout = -1;
        int64_t deadline = 0;
        if (cwnet_server_next_deadline(&g_srv, &deadline)) {
            int64_t d = deadline - now_ms;
            if (d < 0) {
                d = 0;
            }
            if (d > (int64_t)INT_MAX) {
                d = (int64_t)INT_MAX;
            }
            timeout = (int)d;
        }

        int ready = sock_poll(fds, nfds, timeout);
        if (ready < 0 && !sock_would_block(sock_last_error()) &&
            sock_last_error() != EINTR) {
            status_line("stato poll fallita: %s", strerror(sock_last_error()));
            break;
        }

        now_ms = (int64_t)clock_now_ms();

        if (ready > 0) {
            for (size_t k = 1; k < nfds; k++) {
                if ((fds[k].revents & SOCK_POLLOUT) != 0) {
                    conn_t *c = conn_of(owner[k]);
                    if (c != NULL && c->in_use) {
                        conn_flush(c);
                    }
                }
            }
            for (size_t k = 1; k < nfds; k++) {
                if ((fds[k].revents & SOCK_POLLIN) != 0) {
                    do_recv(owner[k], now_ms);
                }
            }
            if ((fds[0].revents & SOCK_POLLIN) != 0) {
                do_accept(listener, now_ms);
            }
        }

        /* Read again: the socket work above is bounded but not free, and the
         * engine must be ticked with the instant it is actually at. */
        now_ms = (int64_t)clock_now_ms();

        cwnet_server_result_t res;
        cwnet_server_poll(&g_srv, now_ms, &res);
        handle_events(&res, now_ms);
    }

    int64_t now_ms = (int64_t)clock_now_ms();
    status_line("stato arresto");
    shutdown_clients();
    key_output_close(&g_out, now_ms);
    sock_close(&listener);
    sock_cleanup();
    if (g_lines_dropped > 0u) {
        /* Best effort: stdout may still refuse it, and by now nothing else
         * is waiting on this process. */
        status_line("stato stdout %lu righe scartate in totale", g_lines_dropped);
    }
    if (g_edge_stalls > 0u || g_edge_errors > 0u || g_edge_given_up > 0u) {
        /* The one number that says whether the trace above can be trusted as
         * a measurement, and whether this descriptor was what stalled the
         * loop. Nothing was dropped unless `persi` is non-zero. */
        status_line("stato uscita fronti (%s): %lu attese per %lld ms, %lu errori, %lu persi",
                    g_edge_name, g_edge_stalls, (long long)g_edge_stall_ms,
                    g_edge_errors, g_edge_given_up);
    }
    edge_close();
    for (int i = 0; i < CWNET_SERVER_MAX_CLIENTS; i++) {
        free(g_conn[i].out);
        g_conn[i].out = NULL;
    }
    return 0;
}
