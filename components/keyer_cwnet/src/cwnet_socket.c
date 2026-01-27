/**
 * @file cwnet_socket.c
 * @brief CWNet TCP socket integration for ESP-IDF
 */

#include "cwnet_socket.h"
#include "config.h"
#include "rt_log.h"

#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

/* External log stream */
extern log_stream_t g_bg_log_stream;

/* Configuration */
extern keyer_config_t g_config;

/*===========================================================================*/
/* Constants                                                                 */
/*===========================================================================*/

#define RECONNECT_DELAY_MS      5000    /* Wait between reconnect attempts */
#define CONNECT_TIMEOUT_MS      10000   /* TCP connect timeout */
#define RECV_TIMEOUT_MS         100     /* Non-blocking receive timeout */

/*===========================================================================*/
/* State                                                                     */
/*===========================================================================*/

static struct {
    cwnet_socket_state_t state;
    cwnet_client_t client;
    int sock;
    int64_t last_attempt_us;
    int64_t connect_start_us;
    char host[CWNET_MAX_HOST_LEN];
    uint16_t port;
    char username[CWNET_MAX_USERNAME_LEN];
    bool enabled;
} s_ctx;

/*===========================================================================*/
/* Callbacks for cwnet_client                                                */
/*===========================================================================*/

static int socket_send_cb(const uint8_t *data, size_t len, void *user_data) {
    (void)user_data;
    if (s_ctx.sock < 0) {
        return -1;
    }
    ssize_t sent = send(s_ctx.sock, data, len, 0);
    return (int)sent;
}

static int32_t get_time_ms_cb(void *user_data) {
    (void)user_data;
    return (int32_t)(esp_timer_get_time() / 1000);
}

static void state_change_cb(cwnet_client_state_t old_state,
                            cwnet_client_state_t new_state,
                            void *user_data) {
    (void)user_data;
    int64_t now_us = esp_timer_get_time();

    if (new_state == CWNET_STATE_READY) {
        RT_INFO(&g_bg_log_stream, now_us, "CWNet: READY (connected to %s:%u)",
                s_ctx.host, s_ctx.port);
        s_ctx.state = CWNET_SOCK_READY;
    } else if (new_state == CWNET_STATE_DISCONNECTED && old_state != CWNET_STATE_DISCONNECTED) {
        RT_WARN(&g_bg_log_stream, now_us, "CWNet: disconnected");
    }
}

/*===========================================================================*/
/* Socket Helpers                                                            */
/*===========================================================================*/

static void close_socket(void) {
    if (s_ctx.sock >= 0) {
        close(s_ctx.sock);
        s_ctx.sock = -1;
    }
    cwnet_client_on_disconnected(&s_ctx.client);
}

static bool set_nonblocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) == 0;
}

static bool start_connect(void) {
    int64_t now_us = esp_timer_get_time();

    /* DNS resolution */
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP
    };
    struct addrinfo *res = NULL;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", s_ctx.port);

    RT_INFO(&g_bg_log_stream, now_us, "CWNet: resolving %s:%s", s_ctx.host, port_str);
    s_ctx.state = CWNET_SOCK_RESOLVING;

    int err = getaddrinfo(s_ctx.host, port_str, &hints, &res);
    if (err != 0 || res == NULL) {
        RT_WARN(&g_bg_log_stream, now_us, "CWNet: DNS failed for %s", s_ctx.host);
        s_ctx.state = CWNET_SOCK_ERROR;
        s_ctx.last_attempt_us = now_us;
        return false;
    }

    /* Create socket */
    s_ctx.sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s_ctx.sock < 0) {
        RT_ERROR(&g_bg_log_stream, now_us, "CWNet: socket() failed: %d", errno);
        freeaddrinfo(res);
        s_ctx.state = CWNET_SOCK_ERROR;
        s_ctx.last_attempt_us = now_us;
        return false;
    }

    /* Set non-blocking */
    if (!set_nonblocking(s_ctx.sock)) {
        RT_ERROR(&g_bg_log_stream, now_us, "CWNet: fcntl failed");
        close_socket();
        freeaddrinfo(res);
        s_ctx.state = CWNET_SOCK_ERROR;
        s_ctx.last_attempt_us = now_us;
        return false;
    }

    /* Start non-blocking connect */
    RT_INFO(&g_bg_log_stream, now_us, "CWNet: connecting to %s:%u", s_ctx.host, s_ctx.port);
    s_ctx.state = CWNET_SOCK_CONNECTING;
    s_ctx.connect_start_us = now_us;

    err = connect(s_ctx.sock, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    if (err < 0 && errno != EINPROGRESS) {
        RT_WARN(&g_bg_log_stream, now_us, "CWNet: connect() failed: %d", errno);
        close_socket();
        s_ctx.state = CWNET_SOCK_ERROR;
        s_ctx.last_attempt_us = now_us;
        return false;
    }

    return true;
}

static bool check_connect_complete(void) {
    int64_t now_us = esp_timer_get_time();

    /* Check for timeout */
    if ((now_us - s_ctx.connect_start_us) > (CONNECT_TIMEOUT_MS * 1000LL)) {
        RT_WARN(&g_bg_log_stream, now_us, "CWNet: connect timeout");
        close_socket();
        s_ctx.state = CWNET_SOCK_ERROR;
        s_ctx.last_attempt_us = now_us;
        return false;
    }

    /* Check if connect completed */
    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(s_ctx.sock, &write_fds);

    struct timeval tv = {0, 0};  /* Non-blocking check */
    int ret = select(s_ctx.sock + 1, NULL, &write_fds, NULL, &tv);

    if (ret > 0 && FD_ISSET(s_ctx.sock, &write_fds)) {
        /* Check for socket error */
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        getsockopt(s_ctx.sock, SOL_SOCKET, SO_ERROR, &so_error, &len);

        if (so_error != 0) {
            RT_WARN(&g_bg_log_stream, now_us, "CWNet: connect error: %d", so_error);
            close_socket();
            s_ctx.state = CWNET_SOCK_ERROR;
            s_ctx.last_attempt_us = now_us;
            return false;
        }

        /* Connected! */
        RT_INFO(&g_bg_log_stream, now_us, "CWNet: TCP connected");
        s_ctx.state = CWNET_SOCK_CONNECTED;
        cwnet_client_on_connected(&s_ctx.client);
        return true;
    }

    return false;  /* Still connecting */
}

static void process_recv(void) {
    if (s_ctx.sock < 0) {
        return;
    }

    uint8_t buf[256];
    ssize_t n = recv(s_ctx.sock, buf, sizeof(buf), MSG_DONTWAIT);

    if (n > 0) {
        cwnet_client_on_data(&s_ctx.client, buf, (size_t)n);
    } else if (n == 0) {
        /* Connection closed by server */
        int64_t now_us = esp_timer_get_time();
        RT_WARN(&g_bg_log_stream, now_us, "CWNet: server closed connection");
        close_socket();
        s_ctx.state = CWNET_SOCK_ERROR;
        s_ctx.last_attempt_us = now_us;
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        /* Actual error */
        int64_t now_us = esp_timer_get_time();
        RT_ERROR(&g_bg_log_stream, now_us, "CWNet: recv error: %d", errno);
        close_socket();
        s_ctx.state = CWNET_SOCK_ERROR;
        s_ctx.last_attempt_us = now_us;
    }
    /* EAGAIN/EWOULDBLOCK is normal for non-blocking - no data available */
}

/*===========================================================================*/
/* Public API                                                                */
/*===========================================================================*/

void cwnet_socket_init(void) {
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.sock = -1;
    s_ctx.state = CWNET_SOCK_DISABLED;

    /* Read config */
    s_ctx.enabled = g_config.remote.cwnet_enabled;
    if (!s_ctx.enabled) {
        int64_t now_us = esp_timer_get_time();
        RT_INFO(&g_bg_log_stream, now_us, "CWNet: disabled in config");
        return;
    }

    /* Copy config values */
    strncpy(s_ctx.host, g_config.remote.server_host, sizeof(s_ctx.host) - 1);
    s_ctx.port = g_config.remote.server_port;
    strncpy(s_ctx.username, g_config.remote.username, sizeof(s_ctx.username) - 1);

    /* Validate */
    if (s_ctx.host[0] == '\0') {
        int64_t now_us = esp_timer_get_time();
        RT_WARN(&g_bg_log_stream, now_us, "CWNet: no server host configured");
        s_ctx.enabled = false;
        return;
    }

    /* Initialize client state machine */
    cwnet_client_config_t cfg = {
        .server_host = s_ctx.host,
        .server_port = s_ctx.port,
        .username = s_ctx.username,
        .send_cb = socket_send_cb,
        .get_time_ms_cb = get_time_ms_cb,
        .state_change_cb = state_change_cb,
        .cw_event_cb = NULL,  /* TODO: handle received CW events */
        .user_data = NULL
    };

    cwnet_client_err_t err = cwnet_client_init(&s_ctx.client, &cfg);
    if (err != CWNET_CLIENT_OK) {
        int64_t now_us = esp_timer_get_time();
        RT_ERROR(&g_bg_log_stream, now_us, "CWNet: client init failed: %d", err);
        s_ctx.enabled = false;
        return;
    }

    int64_t now_us = esp_timer_get_time();
    RT_INFO(&g_bg_log_stream, now_us, "CWNet: initialized, server=%s:%u user=%s",
            s_ctx.host, s_ctx.port, s_ctx.username);

    s_ctx.state = CWNET_SOCK_DISCONNECTED;
}

void cwnet_socket_process(void) {
    if (!s_ctx.enabled) {
        return;
    }

    int64_t now_us = esp_timer_get_time();

    switch (s_ctx.state) {
        case CWNET_SOCK_DISABLED:
            /* Nothing to do */
            break;

        case CWNET_SOCK_DISCONNECTED:
            /* Start connection attempt */
            start_connect();
            break;

        case CWNET_SOCK_RESOLVING:
            /* DNS is blocking in start_connect, shouldn't reach here */
            break;

        case CWNET_SOCK_CONNECTING:
            /* Check if connect completed */
            check_connect_complete();
            break;

        case CWNET_SOCK_CONNECTED:
        case CWNET_SOCK_READY:
            /* Process incoming data */
            process_recv();

            /* Update state from client */
            if (cwnet_client_get_state(&s_ctx.client) == CWNET_STATE_READY) {
                s_ctx.state = CWNET_SOCK_READY;
            }
            break;

        case CWNET_SOCK_ERROR:
            /* Wait before reconnecting */
            if ((now_us - s_ctx.last_attempt_us) > (RECONNECT_DELAY_MS * 1000LL)) {
                RT_INFO(&g_bg_log_stream, now_us, "CWNet: reconnecting...");
                s_ctx.state = CWNET_SOCK_DISCONNECTED;
            }
            break;
    }
}

bool cwnet_socket_send_key_event(bool key_down) {
    if (s_ctx.state != CWNET_SOCK_READY) {
        return false;
    }

    cwnet_client_err_t err = cwnet_client_send_key_event(&s_ctx.client, key_down);
    if (err == CWNET_CLIENT_OK) {
        int64_t now_us = esp_timer_get_time();
        RT_DEBUG(&g_bg_log_stream, now_us, "CWNet TX: %s", key_down ? "DOWN" : "UP");
        return true;
    }
    return false;
}

cwnet_socket_state_t cwnet_socket_get_state(void) {
    return s_ctx.state;
}

bool cwnet_socket_is_ready(void) {
    return s_ctx.state == CWNET_SOCK_READY;
}

int32_t cwnet_socket_get_latency_ms(void) {
    return cwnet_client_get_latency_ms(&s_ctx.client);
}

const char *cwnet_socket_state_str(cwnet_socket_state_t state) {
    switch (state) {
        case CWNET_SOCK_DISABLED:     return "DISABLED";
        case CWNET_SOCK_DISCONNECTED: return "DISCONNECTED";
        case CWNET_SOCK_RESOLVING:    return "RESOLVING";
        case CWNET_SOCK_CONNECTING:   return "CONNECTING";
        case CWNET_SOCK_CONNECTED:    return "CONNECTED";
        case CWNET_SOCK_READY:        return "READY";
        case CWNET_SOCK_ERROR:        return "ERROR";
        default:                      return "UNKNOWN";
    }
}
