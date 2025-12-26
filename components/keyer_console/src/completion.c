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

#ifdef ESP_PLATFORM
#include "usb_console.h"
/* Use USB console printf for console output (skip for IDE analyzers) */
#if !defined(__INTELLISENSE__) && !defined(__clang_analyzer__) && !defined(__clangd__)
#define printf usb_console_printf
#endif
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
    COMPLETE_DEBUG,
    COMPLETE_MEM_SLOT,
    COMPLETE_MEM_SUBCMD
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
 * @brief Expand family alias to canonical name
 * @param alias Alias to expand (e.g., "k", "hw")
 * @param out Output buffer for canonical name
 * @param out_len Output buffer length
 * @return true if alias was expanded
 */
static bool expand_family_alias(const char *alias, size_t alias_len,
                                char *out, size_t out_len) {
    for (size_t i = 0; i < FAMILY_COUNT; i++) {
        const family_descriptor_t *f = &CONSOLE_FAMILIES[i];
        /* Check canonical name */
        if (strlen(f->name) == alias_len &&
            strncmp(f->name, alias, alias_len) == 0) {
            strncpy(out, f->name, out_len - 1);
            out[out_len - 1] = '\0';
            return true;
        }
        /* Check aliases */
        if (f->aliases != NULL) {
            const char *p = f->aliases;
            while (*p) {
                const char *comma = strchr(p, ',');
                size_t a_len = comma ? (size_t)(comma - p) : strlen(p);
                if (a_len == alias_len && strncmp(p, alias, alias_len) == 0) {
                    strncpy(out, f->name, out_len - 1);
                    out[out_len - 1] = '\0';
                    return true;
                }
                p = comma ? comma + 1 : p + a_len;
            }
        }
    }
    return false;
}

/**
 * @brief Get all matching parameters (supports dot notation paths)
 *
 * Matches against:
 * - Full paths: keyer.wpm, audio.sidetone_freq_hz
 * - Short names: wpm, sidetone_freq_hz (backwards compatible)
 * - Family prefixes: keyer. → shows all keyer.* params
 * - Alias expansion: k. → keyer., hw. → hardware.
 */
static size_t get_matching_params(const char *prefix, size_t len,
                                  const char **matches, size_t max_matches) {
    size_t count = 0;
    char expanded[64];
    const char *match_prefix = prefix;
    size_t match_len = len;

    /* Check for alias expansion (e.g., "k." → "keyer.") */
    const char *dot = memchr(prefix, '.', len);
    if (dot != NULL) {
        size_t alias_len = (size_t)(dot - prefix);
        char canonical[32];
        if (expand_family_alias(prefix, alias_len, canonical, sizeof(canonical))) {
            /* Build expanded prefix: canonical + rest */
            size_t rest_len = len - alias_len;
            snprintf(expanded, sizeof(expanded), "%s%.*s",
                     canonical, (int)rest_len, dot);
            match_prefix = expanded;
            match_len = strlen(expanded);
        }
    }

    /* Match full paths first */
    for (size_t i = 0; i < CONSOLE_PARAM_COUNT && count < max_matches; i++) {
        if (strncmp(CONSOLE_PARAMS[i].full_path, match_prefix, match_len) == 0) {
            matches[count++] = CONSOLE_PARAMS[i].full_path;
        }
    }

    /* If no path matches, try short names for backwards compatibility */
    if (count == 0) {
        for (size_t i = 0; i < CONSOLE_PARAM_COUNT && count < max_matches; i++) {
            if (strncmp(CONSOLE_PARAMS[i].name, prefix, len) == 0) {
                matches[count++] = CONSOLE_PARAMS[i].full_path;
            }
        }
    }

    /* If prefix is empty or just a family start, add family suggestions */
    if (count == 0 && len > 0 && dot == NULL) {
        for (size_t i = 0; i < FAMILY_COUNT && count < max_matches; i++) {
            const family_descriptor_t *f = &CONSOLE_FAMILIES[i];
            if (strncmp(f->name, prefix, len) == 0) {
                /* Return family.* pattern hint - use static strings */
                static const char *family_hints[5] = {
                    "keyer.*", "audio.*", "hardware.*", "timing.*", "system.*"
                };
                if (i < 5) {
                    matches[count++] = family_hints[i];
                }
            }
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
 * @brief Get matching mem slot numbers
 */
static size_t get_matching_mem_slots(const char *prefix, size_t len,
                                     const char **matches, size_t max_matches) {
    static const char *slots[] = {"1", "2", "3", "4", "5", "6", "7", "8"};
    size_t count = 0;

    for (size_t i = 0; i < 8 && count < max_matches; i++) {
        if (strncmp(slots[i], prefix, len) == 0) {
            matches[count++] = slots[i];
        }
    }
    return count;
}

/**
 * @brief Get matching mem subcommands
 */
static size_t get_matching_mem_subcmds(const char *prefix, size_t len,
                                       const char **matches, size_t max_matches) {
    static const char *subcmds[] = {"clear", "label"};
    size_t count = 0;

    for (size_t i = 0; i < 2 && count < max_matches; i++) {
        if (strncmp(subcmds[i], prefix, len) == 0) {
            matches[count++] = subcmds[i];
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
        } else if (strncmp(line, "mem ", 4) == 0) {
            /* After "mem " - check if first or second arg */
            const char *arg_start = line + 4;
            while (*arg_start == ' ') arg_start++;
            const char *space = strchr(arg_start, ' ');
            if (space == NULL || prefix >= space) {
                /* Still on first arg or it's a slot - check context */
                if (space == NULL) {
                    type = COMPLETE_MEM_SLOT;
                } else {
                    type = COMPLETE_MEM_SUBCMD;
                }
            } else {
                type = COMPLETE_MEM_SUBCMD;
            }
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
        case COMPLETE_MEM_SLOT:
            match_count = get_matching_mem_slots(prefix, prefix_len, matches, MAX_COMPLETIONS);
            break;
        case COMPLETE_MEM_SUBCMD:
            match_count = get_matching_mem_subcmds(prefix, prefix_len, matches, MAX_COMPLETIONS);
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
