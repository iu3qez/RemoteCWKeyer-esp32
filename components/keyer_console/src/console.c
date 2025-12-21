/**
 * @file console.c
 * @brief Serial console implementation (stub)
 *
 * TODO: Port from Rust console module
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

    /* TODO: Initialize ESP console */
}

bool console_process_char(char c) {
    if (c == '\r' || c == '\n') {
        if (s_line_pos > 0) {
            s_line_buf[s_line_pos] = '\0';
            printf("\r\n");

            /* TODO: Execute command */
            printf("CMD: %s\r\n", s_line_buf);

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
    } else if (s_line_pos < CONSOLE_LINE_MAX - 1) {
        s_line_buf[s_line_pos++] = c;
        putchar(c);
    }

    return false;
}

#ifdef CONFIG_IDF_TARGET
void console_task(void *arg) {
    (void)arg;

    printf("\r\nkeyer_c console ready\r\n> ");
    fflush(stdout);

    for (;;) {
        int c = getchar();
        if (c != EOF) {
            if (console_process_char((char)c)) {
                printf("> ");
                fflush(stdout);
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
