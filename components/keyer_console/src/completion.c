/**
 * @file completion.c
 * @brief Tab completion for commands and parameters
 */

#include "console.h"
#include "config_console.h"
#include "log_tags.h"
#include <string.h>
#include <stddef.h>

/** Completion state for cycling through matches */
static size_t s_last_match_idx = 0;
static size_t s_prefix_len = 0;
static bool s_cycling = false;

/**
 * @brief Find matching command starting from index
 * @param prefix Prefix to match
 * @param len Length of prefix
 * @param start_idx Pointer to start index (updated to next index after match)
 * @return Matching command name or NULL
 */
static const char *find_matching_command(const char *prefix, size_t len, size_t *start_idx) {
    size_t count;
    const console_cmd_t *cmds = console_get_commands(&count);

    for (size_t i = *start_idx; i < count; i++) {
        if (strncmp(cmds[i].name, prefix, len) == 0) {
            *start_idx = i + 1;
            return cmds[i].name;
        }
    }
    return NULL;
}

/**
 * @brief Find matching parameter starting from index
 * @param prefix Prefix to match
 * @param len Length of prefix
 * @param start_idx Pointer to start index (updated to next index after match)
 * @return Matching parameter name or NULL
 */
static const char *find_matching_param(const char *prefix, size_t len, size_t *start_idx) {
    for (size_t i = *start_idx; i < CONSOLE_PARAM_COUNT; i++) {
        if (strncmp(CONSOLE_PARAMS[i].name, prefix, len) == 0) {
            *start_idx = i + 1;
            return CONSOLE_PARAMS[i].name;
        }
    }
    return NULL;
}

/**
 * @brief Find matching diag subcommand starting from index
 */
static const char *find_matching_diag_arg(const char *prefix, size_t len, size_t *start_idx) {
    static const char *diag_args[] = { "on", "off" };
    static const size_t diag_args_count = 2;

    for (size_t i = *start_idx; i < diag_args_count; i++) {
        if (strncmp(diag_args[i], prefix, len) == 0) {
            *start_idx = i + 1;
            return diag_args[i];
        }
    }
    return NULL;
}

/**
 * @brief Find matching debug subcommand starting from index
 *
 * Debug completions are organized in 3 groups:
 * 1. Special commands: info, none
 * 2. Tags from LOG_TAGS (generated from codebase)
 * 3. Log levels for second argument
 */
static const char *find_matching_debug_arg(const char *prefix, size_t len, size_t *start_idx) {
    /* Group 1: Special commands */
    static const char *special_cmds[] = { "info", "none", "*" };
    static const size_t special_count = 3;

    /* Group 2: Tags from log_tags.h (LOG_TAGS, LOG_TAGS_COUNT) */

    /* Group 3: Log levels */
    static const char *levels[] = { "error", "warn", "debug", "verbose" };
    static const size_t levels_count = 4;

    size_t total = special_count + LOG_TAGS_COUNT + levels_count;
    size_t i = *start_idx;

    while (i < total) {
        const char *candidate;

        if (i < special_count) {
            candidate = special_cmds[i];
        } else if (i < special_count + LOG_TAGS_COUNT) {
            candidate = LOG_TAGS[i - special_count];
        } else {
            candidate = levels[i - special_count - LOG_TAGS_COUNT];
        }

        if (strncmp(candidate, prefix, len) == 0) {
            *start_idx = i + 1;
            return candidate;
        }
        i++;
    }
    return NULL;
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

    /* Determine what we're completing */
    bool completing_param = false;
    bool completing_diag = false;
    bool completing_debug = false;

    if (token_start > 0) {
        if (strncmp(line, "set ", 4) == 0 || strncmp(line, "show ", 5) == 0) {
            completing_param = true;
        } else if (strncmp(line, "diag ", 5) == 0) {
            completing_diag = true;
        } else if (strncmp(line, "debug ", 6) == 0) {
            completing_debug = true;
        }
    }

    /* Check if we're cycling through matches */
    if (s_cycling && s_prefix_len == prefix_len) {
        /* Continue from last match */
    } else {
        s_last_match_idx = 0;
        s_prefix_len = prefix_len;
        s_cycling = true;
    }

    const char *match = NULL;
    size_t idx = s_last_match_idx;

    if (completing_debug) {
        match = find_matching_debug_arg(prefix, prefix_len, &idx);
    } else if (completing_diag) {
        match = find_matching_diag_arg(prefix, prefix_len, &idx);
    } else if (completing_param) {
        match = find_matching_param(prefix, prefix_len, &idx);
    } else {
        match = find_matching_command(prefix, prefix_len, &idx);
    }

    if (match == NULL) {
        /* Wrap around */
        idx = 0;
        if (completing_debug) {
            match = find_matching_debug_arg(prefix, prefix_len, &idx);
        } else if (completing_diag) {
            match = find_matching_diag_arg(prefix, prefix_len, &idx);
        } else if (completing_param) {
            match = find_matching_param(prefix, prefix_len, &idx);
        } else {
            match = find_matching_command(prefix, prefix_len, &idx);
        }
    }

    if (match == NULL) {
        s_cycling = false;
        return false;
    }

    s_last_match_idx = idx;

    /* Replace the prefix with the match */
    size_t match_len = strlen(match);
    size_t new_line_len = token_start + match_len;

    if (new_line_len >= max_len) {
        return false;
    }

    strcpy(&line[token_start], match);
    line[new_line_len] = '\0';
    *pos = new_line_len;

    return true;
}

void console_complete_reset(void) {
    s_cycling = false;
    s_last_match_idx = 0;
}
