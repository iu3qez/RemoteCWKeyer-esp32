/**
 * @file console.h
 * @brief Serial console interface
 *
 * Interactive command interface with history and tab completion.
 * Uses USB Serial JTAG for input/output.
 */

#ifndef KEYER_CONSOLE_H
#define KEYER_CONSOLE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum command line length */
#define CONSOLE_LINE_MAX 256

/** History depth */
#define CONSOLE_HISTORY_SIZE 10

/**
 * @brief Initialize console
 */
void console_init(void);

/**
 * @brief Console task (runs on Core 1)
 * @param arg Unused
 */
void console_task(void *arg);

/**
 * @brief Process single character input
 * @param c Input character
 * @return true if command was executed
 */
bool console_process_char(char c);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_CONSOLE_H */
