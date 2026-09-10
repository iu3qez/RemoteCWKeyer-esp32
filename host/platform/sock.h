/**
 * @file sock.h
 * @brief Non-blocking TCP sockets for host programs, with a winsock seam.
 *
 * Today this is POSIX (BSD sockets, poll()). It is written so a Windows
 * port (the host client of issue #68) can implement the same functions
 * against winsock without touching any caller:
 *   - close()        -> closesocket()
 *   - fcntl(O_NONBLOCK) -> ioctlsocket(FIONBIO)
 *   - poll()         -> WSAPoll() is broken before Windows 10 2004, so that
 *                       port likely reimplements sock_poll() on select();
 *                       either way the signature below does not change.
 *
 * The handle is a small transparent struct, not a bare int, precisely so a
 * winsock SOCKET (an unsigned handle, not a POSIX fd) can be stored in the
 * same field later. INVALID_SOCKET on Windows is (SOCKET)(~0); stored in a
 * signed intptr_t that is exactly -1, matching SOCK_INVALID below bit for
 * bit — the sentinel survives the port unchanged.
 */
#ifndef HOST_PLATFORM_SOCK_H
#define HOST_PLATFORM_SOCK_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque-in-spirit handle. POSIX stores a plain fd; see the file banner. */
typedef struct sock_handle {
    intptr_t native_handle;
} sock_handle_t;

/** A closed/never-opened handle. Compare with sock_valid(), not by hand. */
extern const sock_handle_t SOCK_INVALID;

bool sock_valid(sock_handle_t h);

/**
 * Process-wide setup/teardown. No-ops on POSIX; the winsock port calls
 * WSAStartup()/WSACleanup() here. Callers call sock_init() once before any
 * other function in this file and sock_cleanup() once at exit, so the seam
 * stays invisible to them either way.
 */
bool sock_init(void);
void sock_cleanup(void);

/**
 * Listening socket bound to `bind_addr`:`port`, with the given backlog.
 * `bind_addr` is a dotted-quad IPv4 address; NULL or "0.0.0.0" means every
 * interface, which is the daemon's default (R13 makes it a flag: the trust
 * boundary is the LAN or the VPN, so the operator can narrow it). Anything
 * that is not a dotted quad is refused here rather than resolved — a
 * listening address is configuration, not a name to look up.
 *
 * SO_REUSEADDR and TCP_NODELAY are set, and the socket is non-blocking.
 * Pass port 0 to let the OS choose an ephemeral port; recover it with
 * sock_local_port(). Returns SOCK_INVALID on failure.
 */
sock_handle_t sock_listen(const char *bind_addr, uint16_t port, int backlog);

/**
 * The port a sock_listen()'d (or connected) socket actually ended up bound
 * to — needed after sock_listen(NULL, 0, ...) picked an ephemeral one. Returns
 * false on failure, leaving *out_port untouched.
 */
bool sock_local_port(sock_handle_t h, uint16_t *out_port);

/**
 * The peer of a connected handle, written as "a.b.c.d:port". Writes "?"
 * and returns false when the peer cannot be had — an already-closed
 * handle, or a family this layer does not speak. Never leaves dst unset.
 *
 * It lives here, and not in the caller, because reading the peer means
 * touching the native handle: a caller that does that itself is a caller
 * the winsock port has to rewrite.
 */
bool sock_peer_string(sock_handle_t h, char *dst, size_t dst_size);

/**
 * Client-side connect, non-blocking: this returns before the handshake
 * completes (mirrors cwnet_socket.c's start_connect()/EINPROGRESS pattern).
 * Poll the handle for writability, then check sock_last_error() the way
 * cwnet_socket.c's check_connect_complete() checks SO_ERROR. Returns
 * SOCK_INVALID on a synchronous failure (bad host, socket() failed, ...).
 */
sock_handle_t sock_connect(const char *host, uint16_t port);

/**
 * Non-blocking accept. On success, *out receives a connected, non-blocking,
 * TCP_NODELAY handle and this returns true. Returns false with *out left
 * untouched when nothing is pending or on error — call sock_last_error()
 * and sock_would_block() to tell the two apart, the same way cwnet_socket.c
 * distinguishes EAGAIN from a real failure.
 *
 * No accept4(): it does not exist on macOS. This is accept() followed by
 * setting O_NONBLOCK, same as cwnet_socket.c's set_nonblocking() step.
 */
bool sock_accept(sock_handle_t listener, sock_handle_t *out);

/**
 * Send with partial-write awareness: returns the number of bytes actually
 * queued, which may be less than `len` on a full send buffer — the same
 * contract cwnet_socket.c's socket_send_cb() gives its caller, which decides
 * there that a short write ends the session. -1 on error (see
 * sock_last_error()).
 */
int sock_send(sock_handle_t h, const void *data, size_t len);

/**
 * Returns bytes read, 0 on an orderly remote close, or -1 on error/would-
 * block (see sock_last_error() / sock_would_block()).
 */
int sock_recv(sock_handle_t h, void *buf, size_t len);

/** Closes the handle and sets *h to SOCK_INVALID. No-op on an already-closed
 *  or NULL handle. */
void sock_close(sock_handle_t *h);

/** Readiness bits for sock_pollfd_t, decoupled from native POLLIN/POLLOUT
 *  values so a winsock backend can translate freely without this header
 *  changing. A remote close is reported as SOCK_POLLIN (readable — the
 *  next sock_recv() returns 0), matching plain poll() semantics. */
#define SOCK_POLLIN  0x0001
#define SOCK_POLLOUT 0x0002

typedef struct sock_pollfd {
    sock_handle_t handle;
    short events;   /**< SOCK_POLLIN / SOCK_POLLOUT, OR'd together */
    short revents;  /**< filled in by sock_poll() */
} sock_pollfd_t;

/**
 * poll() over a set of handles. timeout_ms < 0 blocks forever, 0 returns
 * immediately. Returns the number of handles with revents set, 0 on
 * timeout, -1 on error. `nfds` above SOCK_POLL_MAX_FDS fails with -1
 * (EINVAL) rather than allocating — a daemon serving a handful of clients
 * never approaches it.
 */
#define SOCK_POLL_MAX_FDS 64

int sock_poll(sock_pollfd_t *fds, size_t nfds, int timeout_ms);

/** errno-equivalent of the last failing call on this thread. Plain errno on
 *  POSIX; the winsock port returns WSAGetLastError(). */
int sock_last_error(void);

/** True when `err` (as returned by sock_last_error()) means "would block,
 *  try again" rather than a real failure — EAGAIN/EWOULDBLOCK/EINPROGRESS
 *  on POSIX, their WSA* counterparts on the winsock port. Callers compare
 *  through this instead of the raw errno constants so the port does not
 *  have to touch call sites that check it. */
bool sock_would_block(int err);

#ifdef __cplusplus
}
#endif

#endif /* HOST_PLATFORM_SOCK_H */
