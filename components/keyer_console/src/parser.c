/**
 * @file parser.c
 * @brief Command line parser
 *
 * Simple tokenizer that splits input on whitespace.
 * Supports up to 3 arguments after the command.
 */

#include "console.h"
#include <string.h>
#include <ctype.h>

/**
 * @brief Skip whitespace in string
 * @note Reserved for future tab completion implementation
 */
static const char *skip_whitespace(const char *s) {
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

/**
 * @brief Find end of token (next whitespace or end of string)
 * @note Reserved for future tab completion implementation
 */
static const char *find_token_end(const char *s) {
    while (*s != '\0' && !isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

void console_parse_line(const char *line, console_parsed_cmd_t *out) {
    /* Initialize output */
    out->command = "";
    out->argc = 0;
    for (int i = 0; i < CONSOLE_MAX_ARGS; i++) {
        out->args[i] = NULL;
    }

    if (line == NULL || *line == '\0') {
        return;
    }

    /* We need a static buffer to store parsed tokens */
    static char s_parse_buf[CONSOLE_LINE_MAX];
    strncpy(s_parse_buf, line, CONSOLE_LINE_MAX - 1);
    s_parse_buf[CONSOLE_LINE_MAX - 1] = '\0';

    char *p = s_parse_buf;

    /* Skip leading whitespace */
    while (*p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }

    if (*p == '\0') {
        return;
    }

    /* Extract command */
    out->command = p;
    while (*p != '\0' && !isspace((unsigned char)*p)) {
        p++;
    }

    if (*p != '\0') {
        *p = '\0';
        p++;
    }

    /* Extract arguments */
    for (int i = 0; i < CONSOLE_MAX_ARGS && *p != '\0'; i++) {
        /* Skip whitespace */
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }

        if (*p == '\0') {
            break;
        }

        out->args[i] = p;
        out->argc++;

        /* Find end of argument */
        while (*p != '\0' && !isspace((unsigned char)*p)) {
            p++;
        }

        if (*p != '\0') {
            *p = '\0';
            p++;
        }
    }
}
