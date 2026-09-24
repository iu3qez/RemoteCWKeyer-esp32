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

#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

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
