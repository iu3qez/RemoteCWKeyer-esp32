/**
 * @file completion.c
 * @brief Tab completion for commands and parameters
 *
 * Shows all matching completions on one line instead of cycling.
 */

#include "console.h"
#include "config_console.h"
#include "log_tags.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#ifdef CONFIG_IDF_TARGET
#include "usb_console.h"
/* Use USB console printf for console output */
#define printf usb_console_printf
#endif

/** Maximum completions to show */
#define MAX_COMPLETIONS 32

/**
 * @brief Completion context type
 */
typedef enum {
    COMPLETE_COMMAND,
    COMPLETE_PARAM,
    COMPLETE_DIAG,
    COMPLETE_DEBUG
} complete_type_t;

/**
 * @brief Get all matching commands
 */
static size_t get_matching_commands(const char *prefix, size_t len,
                                    const char **matches, size_t max_matches) {
    size_t count = 0;
    size_t cmd_count;
    const console_cmd_t *cmds = console_get_commands(&cmd_count);

    for (size_t i = 0; i < cmd_count && count < max_matches; i++) {
        if (strncmp(cmds[i].name, prefix, len) == 0) {
            matches[count++] = cmds[i].name;
        }
    }
    return count;
}

/**
 * @brief Get all matching parameters
 */
static size_t get_matching_params(const char *prefix, size_t len,
                                  const char **matches, size_t max_matches) {
    size_t count = 0;
    for (size_t i = 0; i < CONSOLE_PARAM_COUNT && count < max_matches; i++) {
        if (strncmp(CONSOLE_PARAMS[i].name, prefix, len) == 0) {
            matches[count++] = CONSOLE_PARAMS[i].name;
        }
    }
    return count;
}

/**
 * @brief Get all matching diag arguments
 */
static size_t get_matching_diag_args(const char *prefix, size_t len,
                                     const char **matches, size_t max_matches) {
    static const char *diag_args[] = { "on", "off" };
    size_t count = 0;

    for (size_t i = 0; i < 2 && count < max_matches; i++) {
        if (strncmp(diag_args[i], prefix, len) == 0) {
            matches[count++] = diag_args[i];
        }
    }
    return count;
}

/**
 * @brief Get all matching debug arguments
 *
 * Groups: special commands, log tags, log levels
 */
static size_t get_matching_debug_args(const char *prefix, size_t len,
                                      const char **matches, size_t max_matches) {
    static const char *special_cmds[] = { "info", "none", "*" };
    static const char *levels[] = { "error", "warn", "debug", "verbose" };
    size_t count = 0;

    /* Special commands */
    for (size_t i = 0; i < 3 && count < max_matches; i++) {
        if (strncmp(special_cmds[i], prefix, len) == 0) {
            matches[count++] = special_cmds[i];
        }
    }

    /* Log tags */
    for (size_t i = 0; i < LOG_TAGS_COUNT && count < max_matches; i++) {
        if (strncmp(LOG_TAGS[i], prefix, len) == 0) {
            matches[count++] = LOG_TAGS[i];
        }
    }

    /* Log levels */
    for (size_t i = 0; i < 4 && count < max_matches; i++) {
        if (strncmp(levels[i], prefix, len) == 0) {
            matches[count++] = levels[i];
        }
    }

    return count;
}

/**
 * @brief Find common prefix length among all matches
 */
static size_t find_common_prefix_len(const char **matches, size_t count) {
    if (count == 0) return 0;
    if (count == 1) return strlen(matches[0]);

    size_t common_len = strlen(matches[0]);
    for (size_t i = 1; i < count; i++) {
        size_t j = 0;
        while (j < common_len && matches[0][j] == matches[i][j]) {
            j++;
        }
        common_len = j;
    }
    return common_len;
}

bool console_complete(char *line, size_t *pos, size_t max_len) {
    if (line == NULL || pos == NULL || *pos == 0) {
        return false;
    }

    /* Find start of current token */
    size_t token_start = *pos;
    while (token_start > 0 && line[token_start - 1] != ' ') {
        token_start--;
    }

    const char *prefix = &line[token_start];
    size_t prefix_len = *pos - token_start;

    /* Determine completion type */
    complete_type_t type = COMPLETE_COMMAND;
    if (token_start > 0) {
        if (strncmp(line, "set ", 4) == 0 || strncmp(line, "show ", 5) == 0) {
            type = COMPLETE_PARAM;
        } else if (strncmp(line, "diag ", 5) == 0) {
            type = COMPLETE_DIAG;
        } else if (strncmp(line, "debug ", 6) == 0) {
            type = COMPLETE_DEBUG;
        }
    }

    /* Get all matches */
    const char *matches[MAX_COMPLETIONS];
    size_t match_count = 0;

    switch (type) {
        case COMPLETE_COMMAND:
            match_count = get_matching_commands(prefix, prefix_len, matches, MAX_COMPLETIONS);
            break;
        case COMPLETE_PARAM:
            match_count = get_matching_params(prefix, prefix_len, matches, MAX_COMPLETIONS);
            break;
        case COMPLETE_DIAG:
            match_count = get_matching_diag_args(prefix, prefix_len, matches, MAX_COMPLETIONS);
            break;
        case COMPLETE_DEBUG:
            match_count = get_matching_debug_args(prefix, prefix_len, matches, MAX_COMPLETIONS);
            break;
    }

    if (match_count == 0) {
        return false;
    }

    if (match_count == 1) {
        /* Single match - complete it */
        size_t match_len = strlen(matches[0]);
        size_t new_line_len = token_start + match_len;

        if (new_line_len >= max_len) {
            return false;
        }

        strcpy(&line[token_start], matches[0]);
        line[new_line_len] = '\0';
        *pos = new_line_len;
        return true;
    }

    /* Multiple matches - show all and complete common prefix */
    printf("\n");
    for (size_t i = 0; i < match_count; i++) {
        printf("%s ", matches[i]);
    }
    printf("\n");
    fflush(stdout);

    /* Complete to common prefix if longer than current */
    size_t common_len = find_common_prefix_len(matches, match_count);
    if (common_len > prefix_len) {
        size_t new_line_len = token_start + common_len;
        if (new_line_len < max_len) {
            memcpy(&line[token_start], matches[0], common_len);
            line[new_line_len] = '\0';
            *pos = new_line_len;
        }
    }

    return true;
}

void console_complete_reset(void) {
    /* No state to reset with show-all approach */
}
