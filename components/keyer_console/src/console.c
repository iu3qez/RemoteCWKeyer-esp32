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

#ifdef CONFIG_IDF_TARGET
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#endif

static char s_line_buf[CONSOLE_LINE_MAX];
static size_t s_line_pos = 0;

void console_init(void) {
    s_line_pos = 0;
    memset(s_line_buf, 0, sizeof(s_line_buf));
}

void console_print_prompt(void) {
    printf("> ");
    fflush(stdout);
}

bool console_process_char(char c) {
    if (c == '\r' || c == '\n') {
        if (s_line_pos > 0) {
            s_line_buf[s_line_pos] = '\0';
            printf("\r\n");

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
            return true;
        }
        return false;
    } else if (c == '\b' || c == 0x7F) {
        /* Backspace */
        if (s_line_pos > 0) {
            s_line_pos--;
            printf("\b \b");
        }
    } else if (c == 0x03) {
        /* Ctrl+C - cancel current line */
        printf("^C\r\n");
        s_line_pos = 0;
        return true;
    } else if (c == 0x15) {
        /* Ctrl+U - clear line */
        while (s_line_pos > 0) {
            s_line_pos--;
            printf("\b \b");
        }
    } else if (c >= 0x20 && c <= 0x7E) {
        /* Printable character */
        if (s_line_pos < CONSOLE_LINE_MAX - 1) {
            s_line_buf[s_line_pos++] = c;
            putchar(c);
        }
    }
    /* Ignore other characters (escape sequences, etc.) for now */

    fflush(stdout);
    return false;
}

#ifdef CONFIG_IDF_TARGET
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
