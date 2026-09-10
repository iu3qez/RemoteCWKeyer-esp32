/**
 * @file key_output.c
 * @brief The virtual backend of key_output.h: one line per edge.
 */
#include "key_output.h"

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
static void virtual_line(key_output_t *out, const char *what, bool on, int64_t at_ms) {
    char line[64];
    (void)snprintf(line, sizeof(line), "%s %d %" PRId64, what, on ? 1 : 0, at_ms);
    out->line(out->line_ctx, line);
}

static void virtual_key(key_output_t *out, bool down, int64_t at_ms) {
    virtual_line(out, "key", down, at_ms);
}

static void virtual_ptt(key_output_t *out, bool on, int64_t at_ms) {
    virtual_line(out, "ptt", on, at_ms);
}

/*===========================================================================*/
/* Interface                                                                 */
/*===========================================================================*/

const char *key_output_backends(void) {
    /* One for now. The physical transport is a Decision that is not open
     * (KTD9, Deferred to Follow-Up Work), so there is nothing else to name. */
    return "virtual";
}

bool key_output_open(key_output_t *out, const char *backend,
                     key_output_line_fn line, void *line_ctx) {
    if (out == NULL || backend == NULL || line == NULL) {
        return false;
    }
    if (strcmp(backend, "virtual") != 0) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->name = "virtual";
    out->line = line;
    out->line_ctx = line_ctx;
    out->apply_key = virtual_key;
    out->apply_ptt = virtual_ptt;
    return true;
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
}
