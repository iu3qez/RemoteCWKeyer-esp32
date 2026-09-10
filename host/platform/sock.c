/**
 * @file sock.c
 * @brief POSIX backing for sock.h (see that file for the winsock seam).
 */
#include "sock.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

const sock_handle_t SOCK_INVALID = { .native_handle = -1 };

bool sock_valid(sock_handle_t h) {
    return h.native_handle != -1;
}

bool sock_init(void) {
    /* POSIX: nothing to do. The winsock port calls WSAStartup() here. */
    return true;
}

void sock_cleanup(void) {
    /* POSIX: nothing to do. The winsock port calls WSACleanup() here. */
}

/*===========================================================================*/
/* Internal helpers                                                          */
/*===========================================================================*/

static bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

/* TCP_NODELAY on every socket per KTD7, plus non-blocking mode. On BSD/macOS
 * also SO_NOSIGPIPE per-socket, since send() there has no MSG_NOSIGNAL flag
 * and the daemon-wide SIGPIPE ignore (KTD7, done by cwnetd's main) is not
 * this library's to assume for every caller (this loopback test included). */
static bool set_common_opts(int fd) {
    int one = 1;

    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) != 0) {
        return false;
    }

#ifdef SO_NOSIGPIPE
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif

    return set_nonblocking(fd);
}

/*===========================================================================*/
/* Listen / connect / accept                                                 */
/*===========================================================================*/

sock_handle_t sock_listen(const char *bind_addr, uint16_t port, int backlog) {
    sock_handle_t h = SOCK_INVALID;

    struct in_addr bind_ip;
    if (bind_addr == NULL) {
        bind_ip.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, bind_addr, &bind_ip) != 1) {
        errno = EINVAL;
        return h;
    }

    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        return h;
    }

    int one = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0) {
        close(fd);
        return h;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr = bind_ip;
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return h;
    }

    if (listen(fd, backlog) != 0) {
        close(fd);
        return h;
    }

    if (!set_common_opts(fd)) {
        close(fd);
        return h;
    }

    h.native_handle = fd;
    return h;
}

bool sock_local_port(sock_handle_t h, uint16_t *out_port) {
    if (out_port == NULL || !sock_valid(h)) {
        return false;
    }

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    memset(&addr, 0, sizeof(addr));

    if (getsockname((int)h.native_handle, (struct sockaddr *)&addr, &len) != 0) {
        return false;
    }

    *out_port = ntohs(addr.sin_port);
    return true;
}

bool sock_peer_string(sock_handle_t h, char *dst, size_t dst_size) {
    if (dst == NULL || dst_size == 0u) {
        return false;
    }

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    char ip[INET_ADDRSTRLEN];

    memset(&addr, 0, sizeof(addr));
    if (!sock_valid(h) ||
        getpeername((int)h.native_handle, (struct sockaddr *)&addr, &len) != 0 ||
        addr.sin_family != AF_INET ||
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip)) == NULL) {
        (void)snprintf(dst, dst_size, "?");
        return false;
    }

    (void)snprintf(dst, dst_size, "%s:%u", ip, (unsigned)ntohs(addr.sin_port));
    return true;
}

sock_handle_t sock_connect(const char *host, uint16_t port) {
    sock_handle_t h = SOCK_INVALID;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char port_str[8];
    (void)snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);

    struct addrinfo *res = NULL;
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || res == NULL) {
        return h;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return h;
    }

    if (!set_common_opts(fd)) {
        close(fd);
        freeaddrinfo(res);
        return h;
    }

    int err = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    if (err != 0 && errno != EINPROGRESS) {
        close(fd);
        return h;
    }

    h.native_handle = fd;
    return h;
}

bool sock_accept(sock_handle_t listener, sock_handle_t *out) {
    if (out == NULL || !sock_valid(listener)) {
        return false;
    }

    int fd = accept((int)listener.native_handle, NULL, NULL);
    if (fd < 0) {
        /* EAGAIN/EWOULDBLOCK: nothing pending. A real error also lands
         * here; sock_last_error()/sock_would_block() tell them apart. */
        return false;
    }

    if (!set_common_opts(fd)) {
        close(fd);
        return false;
    }

    out->native_handle = fd;
    return true;
}

/*===========================================================================*/
/* I/O                                                                       */
/*===========================================================================*/

int sock_send(sock_handle_t h, const void *data, size_t len) {
    if (!sock_valid(h)) {
        errno = EBADF;
        return -1;
    }

#ifdef MSG_NOSIGNAL
    ssize_t n = send((int)h.native_handle, data, len, MSG_NOSIGNAL);
#else
    ssize_t n = send((int)h.native_handle, data, len, 0);
#endif

    if (n < 0) {
        return -1;
    }
    /* send() never returns more than len; the cast just narrows the type. */
    return (int)n;
}

int sock_recv(sock_handle_t h, void *buf, size_t len) {
    if (!sock_valid(h)) {
        errno = EBADF;
        return -1;
    }

    ssize_t n = recv((int)h.native_handle, buf, len, 0);
    if (n < 0) {
        return -1;
    }
    return (int)n;
}

void sock_close(sock_handle_t *h) {
    if (h == NULL || !sock_valid(*h)) {
        return;
    }
    (void)close((int)h->native_handle);
    *h = SOCK_INVALID;
}

/*===========================================================================*/
/* poll                                                                      */
/*===========================================================================*/

static short to_native_events(short events) {
    short native = 0;
    if (events & SOCK_POLLIN) {
        native = (short)(native | POLLIN);
    }
    if (events & SOCK_POLLOUT) {
        native = (short)(native | POLLOUT);
    }
    return native;
}

static short from_native_revents(short revents) {
    short out = 0;
    /* POLLHUP/POLLERR fold into SOCK_POLLIN: a remote close or a socket
     * error both mean "the next sock_recv()/sock_send() will tell you
     * what happened", i.e. the handle is ready to be serviced. */
    if (revents & (POLLIN | POLLHUP | POLLERR)) {
        out = (short)(out | SOCK_POLLIN);
    }
    if (revents & POLLOUT) {
        out = (short)(out | SOCK_POLLOUT);
    }
    return out;
}

int sock_poll(sock_pollfd_t *fds, size_t nfds, int timeout_ms) {
    if (fds == NULL && nfds != 0) {
        errno = EINVAL;
        return -1;
    }
    if (nfds > SOCK_POLL_MAX_FDS) {
        errno = EINVAL;
        return -1;
    }

    struct pollfd native[SOCK_POLL_MAX_FDS];
    for (size_t i = 0; i < nfds; ++i) {
        native[i].fd = (int)fds[i].handle.native_handle;
        native[i].events = to_native_events(fds[i].events);
        native[i].revents = 0;
    }

    int rc = poll(native, (nfds_t)nfds, timeout_ms);

    for (size_t i = 0; i < nfds; ++i) {
        fds[i].revents = from_native_revents(native[i].revents);
    }

    return rc;
}

/*===========================================================================*/
/* Errors                                                                    */
/*===========================================================================*/

int sock_last_error(void) {
    return errno;
}

bool sock_would_block(int err) {
    return err == EAGAIN || err == EWOULDBLOCK || err == EINPROGRESS;
}
