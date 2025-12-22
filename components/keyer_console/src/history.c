/**
 * @file history.c
 * @brief Command history implementation
 *
 * Static ring buffer for command history with arrow key navigation.
 */

#include "console.h"
#include <string.h>

/** Ring buffer for history entries */
static char s_history[CONSOLE_HISTORY_SIZE][CONSOLE_LINE_MAX];

/** Number of entries in history (0 to CONSOLE_HISTORY_SIZE) */
static size_t s_history_count = 0;

/** Write position (where next entry will be written) */
static size_t s_history_write = 0;

/** Navigation position (current position when navigating) */
static size_t s_history_nav = 0;

/** Whether we're currently navigating through history */
static bool s_navigating = false;

/** Number of entries returned during current navigation */
static size_t s_nav_count = 0;

void console_history_init(void) {
    s_history_count = 0;
    s_history_write = 0;
    s_history_nav = 0;
    s_navigating = false;
    s_nav_count = 0;
    memset(s_history, 0, sizeof(s_history));
}

void console_history_push(const char *line) {
    if (line == NULL || line[0] == '\0') {
        return;
    }

    /* Don't add duplicates of the last entry */
    if (s_history_count > 0) {
        size_t last = (s_history_write + CONSOLE_HISTORY_SIZE - 1) % CONSOLE_HISTORY_SIZE;
        if (strcmp(s_history[last], line) == 0) {
            return;
        }
    }

    /* Copy line to history buffer */
    strncpy(s_history[s_history_write], line, CONSOLE_LINE_MAX - 1);
    s_history[s_history_write][CONSOLE_LINE_MAX - 1] = '\0';

    /* Advance write pointer */
    s_history_write = (s_history_write + 1) % CONSOLE_HISTORY_SIZE;
    if (s_history_count < CONSOLE_HISTORY_SIZE) {
        s_history_count++;
    }

    /* Reset navigation state */
    s_navigating = false;
    s_nav_count = 0;
}

const char *console_history_prev(void) {
    if (s_history_count == 0) {
        return NULL;
    }

    if (!s_navigating) {
        /* Start navigating from the position after the most recent entry */
        /* (s_history_write points to where the NEXT entry will go) */
        s_history_nav = s_history_write;
        s_navigating = true;
        s_nav_count = 0;
    }

    /* Check if we've already returned all available entries */
    if (s_nav_count >= s_history_count) {
        return NULL;
    }

    /* Move to previous entry */
    s_history_nav = (s_history_nav + CONSOLE_HISTORY_SIZE - 1) % CONSOLE_HISTORY_SIZE;
    s_nav_count++;

    return s_history[s_history_nav];
}

const char *console_history_next(void) {
    if (!s_navigating || s_history_count == 0 || s_nav_count == 0) {
        return NULL;
    }

    /* Move to next (more recent) entry */
    s_history_nav = (s_history_nav + 1) % CONSOLE_HISTORY_SIZE;
    s_nav_count--;

    if (s_nav_count == 0) {
        /* Navigated past newest entry, return to current input */
        s_navigating = false;
        return NULL;
    }

    return s_history[s_history_nav];
}

void console_history_reset_nav(void) {
    s_navigating = false;
    s_nav_count = 0;
}
