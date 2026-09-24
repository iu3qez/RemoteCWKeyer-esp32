/**
 * @file key_output.c
 * @brief The interface of key_output.h, and its virtual backend: one line
 *        per edge. The serial backend is key_output_serial.c.
 */
#include "key_output.h"
#include "key_output_serial.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/*===========================================================================*/
/* Virtual backend                                                           */
/*===========================================================================*/

/*
 * "key 1 123456" / "ptt 0 123556", the shape U6 asks for: state then the
 * scheduled instant in monotonic milliseconds. Two fields and a name, so a
 * shell pipeline can diff the deltas against the encoded waits without a
 * parser (Success Criteria). Where the line lands is the daemon's --edges
 * descriptor, which never drops one.
 */
void key_output_edge_line(key_output_t *out, const char *what, bool on, int64_t at_ms) {
    char line[64];
    (void)snprintf(line, sizeof(line), "%s %d %" PRId64, what, on ? 1 : 0, at_ms);
    out->line(out->line_ctx, line);
}

static void virtual_key(key_output_t *out, bool down, int64_t at_ms) {
    key_output_edge_line(out, "key", down, at_ms);
}

static void virtual_ptt(key_output_t *out, bool on, int64_t at_ms) {
    key_output_edge_line(out, "ptt", on, at_ms);
}

/*===========================================================================*/
/* Interface                                                                 */
/*===========================================================================*/

const char *key_output_backends(void) {
    return "virtual, serial";
}

bool key_output_open(key_output_t *out, const key_output_cfg_t *cfg,
                     key_output_line_fn line, void *line_ctx,
                     char *err, size_t err_len) {
    if (out == NULL || cfg == NULL || line == NULL || err == NULL || err_len == 0u) {
        return false;
    }
    key_output_map_t map;
    if (!key_output_check(cfg, &map, err, err_len)) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->fd = -1;
    out->line = line;
    out->line_ctx = line_ctx;
    if (strcmp(cfg->backend, "serial") == 0) {
        return key_output_serial_open(out, cfg->device, &map, &key_output_os_posix,
                                      err, err_len);
    }
    out->name = "virtual";
    out->apply_key = virtual_key;
    out->apply_ptt = virtual_ptt;
    return true;
}

const key_output_fault_t *key_output_fault(const key_output_t *out) {
    return (out != NULL && out->fault.set) ? &out->fault : NULL;
}

void key_output_fail(key_output_t *out, key_output_fault_kind_t kind, const char *call,
                     int err, int64_t duration_us) {
    if (out == NULL || out->fault.set) {
        return;
    }
    out->fault.set = true;
    out->fault.kind = kind;
    out->fault.call = call;
    out->fault.err = err;
    out->fault.duration_us = duration_us;
}

int64_t key_output_edge_wait_ms(const key_output_t *out) {
    if (out == NULL || out->os == NULL) {
        return KEY_OUTPUT_EDGE_WAIT_FOREVER;
    }
    return out->fault.set ? 0 : (int64_t)(KEY_OUTPUT_SERIAL_SLOW_US / 1000);
}

int key_output_poll_fd(const key_output_t *out) {
    return (out != NULL && out->service != NULL) ? out->fd : -1;
}

void key_output_service(key_output_t *out, bool hangup) {
    if (out != NULL && out->service != NULL) {
        out->service(out, hangup);
    }
}

void key_output_set_key(key_output_t *out, bool down, int64_t at_ms) {
    if (out == NULL || out->apply_key == NULL || out->key_down == down) {
        return;
    }
    out->key_down = down;
    out->apply_key(out, down, at_ms);
}

void key_output_set_ptt(key_output_t *out, bool on, int64_t at_ms) {
    if (out == NULL || out->apply_ptt == NULL || out->ptt_on == on) {
        return;
    }
    out->ptt_on = on;
    out->apply_ptt(out, on, at_ms);
}

void key_output_release(key_output_t *out, int64_t at_ms) {
    key_output_set_key(out, false, at_ms);
    key_output_set_ptt(out, false, at_ms);
}

void key_output_close(key_output_t *out, int64_t at_ms) {
    if (out == NULL) {
        return;
    }
    key_output_release(out, at_ms);
    out->apply_key = NULL;
    out->apply_ptt = NULL;
    out->service = NULL;
    if (out->finish != NULL) {
        out->finish(out);
        out->finish = NULL;
    }
}
