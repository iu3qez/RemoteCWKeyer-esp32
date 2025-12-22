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
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum command line length */
#define CONSOLE_LINE_MAX 64

/** History depth */
#define CONSOLE_HISTORY_SIZE 4

/** Maximum number of arguments */
#define CONSOLE_MAX_ARGS 3

/* ============================================================================
 * Error codes
 * ============================================================================ */

typedef enum {
    CONSOLE_OK = 0,
    CONSOLE_ERR_UNKNOWN_CMD,    /**< E01: Unknown command */
    CONSOLE_ERR_INVALID_VALUE,  /**< E02: Invalid value */
    CONSOLE_ERR_MISSING_ARG,    /**< E03: Missing argument */
    CONSOLE_ERR_OUT_OF_RANGE,   /**< E04: Out of range */
    CONSOLE_ERR_REQUIRES_CONFIRM, /**< E05: Requires 'confirm' */
    CONSOLE_ERR_NVS_ERROR,      /**< E06: NVS error */
} console_error_t;

/**
 * @brief Get error code string (E01, E02, etc.)
 */
const char *console_error_code(console_error_t err);

/**
 * @brief Get error message
 */
const char *console_error_message(console_error_t err);

/* ============================================================================
 * Parser
 * ============================================================================ */

/**
 * @brief Parsed command structure
 */
typedef struct {
    const char *command;                /**< Command name (first token) */
    const char *args[CONSOLE_MAX_ARGS]; /**< Arguments (up to 3) */
    int argc;                           /**< Number of arguments */
} console_parsed_cmd_t;

/**
 * @brief Parse a command line into tokens
 * @param line Input line (will be modified)
 * @param out Output parsed command
 */
void console_parse_line(const char *line, console_parsed_cmd_t *out);

/* ============================================================================
 * Command registry
 * ============================================================================ */

/**
 * @brief Command handler function type
 */
typedef console_error_t (*console_cmd_handler_t)(const console_parsed_cmd_t *cmd);

/**
 * @brief Command descriptor
 */
typedef struct {
    const char *name;           /**< Command name */
    const char *brief;          /**< Brief description */
    const char *usage;          /**< Detailed usage (NULL if none) */
    console_cmd_handler_t handler; /**< Handler function */
} console_cmd_t;

/**
 * @brief Get all available commands
 * @param count Output: number of commands
 * @return Pointer to command array
 */
const console_cmd_t *console_get_commands(size_t *count);

/**
 * @brief Execute a parsed command
 * @param cmd Parsed command
 * @return Error code
 */
console_error_t console_execute(const console_parsed_cmd_t *cmd);

/**
 * @brief Find command by name
 * @param name Command name
 * @return Pointer to command or NULL
 */
const console_cmd_t *console_find_command(const char *name);

/* ============================================================================
 * Console main interface
 * ============================================================================ */

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

/**
 * @brief Push character to console (from USB callback)
 *
 * Same as console_process_char but without echo (echo handled by USB).
 *
 * @param c Input character
 * @return true if command was executed
 */
bool console_push_char(char c);

/**
 * @brief Print the console prompt
 */
void console_print_prompt(void);

/* ============================================================================
 * History
 * ============================================================================ */

/**
 * @brief Initialize command history
 */
void console_history_init(void);

/**
 * @brief Add command to history (skips duplicates of last entry)
 * @param line Command line to add
 */
void console_history_push(const char *line);

/**
 * @brief Navigate to older entry (arrow up)
 * @return Pointer to history entry or NULL if at oldest
 */
const char *console_history_prev(void);

/**
 * @brief Navigate to newer entry (arrow down)
 * @return Pointer to history entry or NULL if at newest
 */
const char *console_history_next(void);

/**
 * @brief Reset navigation state
 */
void console_history_reset_nav(void);

/* ============================================================================
 * Tab completion
 * ============================================================================ */

/**
 * @brief Complete current token with matching command or parameter
 *
 * Completes the token at cursor position:
 * - First token: complete commands from console_get_commands()
 * - After "set " or "show ": complete parameter names from CONSOLE_PARAMS
 * - Tab cycles through matches
 *
 * @param line Line buffer (modified in-place)
 * @param pos Pointer to cursor position (updated after completion)
 * @param max_len Maximum length of line buffer
 * @return true if completion was applied
 */
bool console_complete(char *line, size_t *pos, size_t max_len);

/**
 * @brief Reset completion cycling state
 *
 * Call this when user types any character other than Tab.
 */
void console_complete_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_CONSOLE_H */
