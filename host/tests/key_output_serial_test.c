/**
 * @file key_output_serial_test.c
 * @brief The serial output of cwnetd: line mapping and configuration (U1).
 *
 * What a real adapter does with these levels is the bench's job (U6). This
 * file proves what the daemon asks for: which line goes high for which
 * function, and which configurations it refuses before a port is touched.
 */
#include "../cwnetd/key_output.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

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
