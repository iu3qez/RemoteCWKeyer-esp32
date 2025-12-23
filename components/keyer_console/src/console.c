/**
 * @file console.c
 * @brief Serial console implementation
 *
 * Interactive command interface with history and tab completion.
 * Uses USB Serial JTAG for input/output.
 */

#include "console.h"
#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "usb_console.h"
/* Use USB console printf for console output (skip for IDE analyzers) */
#if !defined(__INTELLISENSE__) && !defined(__clang_analyzer__) && !defined(__clangd__)
#define printf usb_console_printf
#endif
#endif

static char s_line_buf[CONSOLE_LINE_MAX];
static size_t s_line_pos = 0;

/** Saved line buffer for history navigation */
static char s_saved_line[CONSOLE_LINE_MAX];
static size_t s_saved_pos = 0;

/** Escape sequence state machine */
typedef enum {
    ESC_NONE,
    ESC_RECEIVED,
    ESC_BRACKET_RECEIVED,
} escape_state_t;

static escape_state_t s_escape_state = ESC_NONE;

void console_init(void) {
    s_line_pos = 0;
    memset(s_line_buf, 0, sizeof(s_line_buf));
    s_escape_state = ESC_NONE;
    console_history_init();
}

void console_print_prompt(void) {
    printf("> ");
    fflush(stdout);
}

bool console_push_char(char c) {
    /* Handle escape sequences for arrow keys */
    if (s_escape_state == ESC_BRACKET_RECEIVED) {
        s_escape_state = ESC_NONE;

        if (c == 'A') {
            /* Arrow up - previous history entry */
            const char *hist = console_history_prev();
            if (hist != NULL) {
                /* Save current line on first navigation */
                if (s_saved_pos == 0 && s_line_pos > 0) {
                    memcpy(s_saved_line, s_line_buf, s_line_pos);
                    s_saved_pos = s_line_pos;
                }

                /* Replace line with history entry */
                strncpy(s_line_buf, hist, CONSOLE_LINE_MAX - 1);
                s_line_buf[CONSOLE_LINE_MAX - 1] = '\0';
                s_line_pos = strlen(s_line_buf);

                /* Clear and redraw line */
                printf("\r> %s\033[K", s_line_buf);
                fflush(stdout);
            }
            return false;
        } else if (c == 'B') {
            /* Arrow down - next history entry */
            const char *hist = console_history_next();
            if (hist != NULL) {
                /* Replace line with history entry */
                strncpy(s_line_buf, hist, CONSOLE_LINE_MAX - 1);
                s_line_buf[CONSOLE_LINE_MAX - 1] = '\0';
                s_line_pos = strlen(s_line_buf);
            } else {
                /* Restore saved line */
                if (s_saved_pos > 0) {
                    memcpy(s_line_buf, s_saved_line, s_saved_pos);
                    s_line_pos = s_saved_pos;
                    s_saved_pos = 0;
                } else {
                    s_line_pos = 0;
                }
            }

            /* Clear and redraw line */
            printf("\r> %s\033[K", s_line_buf);
            fflush(stdout);
            return false;
        }
        return false;
    }

    if (s_escape_state == ESC_RECEIVED) {
        if (c == '[') {
            s_escape_state = ESC_BRACKET_RECEIVED;
        } else {
            s_escape_state = ESC_NONE;
        }
        return false;
    }

    if (c == 0x1B) {
        /* ESC character - start escape sequence */
        s_escape_state = ESC_RECEIVED;
        return false;
    }

    /* Handle Tab completion */
    if (c == 0x09) {
        /* Tab character - try to complete */
        if (console_complete(s_line_buf, &s_line_pos, CONSOLE_LINE_MAX)) {
            /* Completion succeeded - redraw line */
            printf("\r> %s", s_line_buf);
            fflush(stdout);
        }
        return false;
    }

    /* Reset history navigation and completion state on any normal input */
    console_history_reset_nav();
    console_complete_reset();

    if (c == '\r' || c == '\n') {
        if (s_line_pos > 0) {
            s_line_buf[s_line_pos] = '\0';

            /* Add to history */
            console_history_push(s_line_buf);

            /* Parse and execute command */
            console_parsed_cmd_t cmd;
            console_parse_line(s_line_buf, &cmd);

            console_error_t err = console_execute(&cmd);
            if (err != CONSOLE_OK) {
                printf("%s: %s\r\n",
                       console_error_code(err),
                       console_error_message(err));
            }

            s_line_pos = 0;
            s_saved_pos = 0;
            return true;
        }
        return false;
    } else if (c == '\b' || c == 0x7F) {
        /* Backspace */
        if (s_line_pos > 0) {
            s_line_pos--;
        }
    } else if (c == 0x03) {
        /* Ctrl+C - cancel current line */
        s_line_pos = 0;
        s_saved_pos = 0;
        return true;
    } else if (c == 0x15) {
        /* Ctrl+U - clear line */
        s_line_pos = 0;
        s_saved_pos = 0;
    } else if (c >= 0x20 && c <= 0x7E) {
        /* Printable character */
        if (s_line_pos < CONSOLE_LINE_MAX - 1) {
            s_line_buf[s_line_pos++] = c;
        }
    }

    return false;
}

bool console_process_char(char c) {
    /* Echo character (for non-USB usage) */
    if (c >= 0x20 && c <= 0x7E) {
        putchar(c);
    } else if (c == '\r' || c == '\n') {
        printf("\r\n");
    } else if (c == '\b' || c == 0x7F) {
        printf("\b \b");
    } else if (c == 0x03) {
        printf("^C\r\n");
    }
    fflush(stdout);

    return console_push_char(c);
}

#ifdef ESP_PLATFORM
void console_task(void *arg) {
    (void)arg;

    printf("\r\nCW Keyer Console\r\n");
    printf("Type 'help' for available commands\r\n");
    console_print_prompt();

    for (;;) {
        int c = getchar();
        if (c != EOF) {
            if (console_process_char((char)c)) {
                console_print_prompt();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
#else
void console_task(void *arg) {
    (void)arg;
}
#endif
