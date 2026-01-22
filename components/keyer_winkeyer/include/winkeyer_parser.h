/**
 * @file winkeyer_parser.h
 * @brief Winkeyer v3 protocol parser state machine
 *
 * Implements a pure state machine parser for the K1EL Winkeyer v3 binary
 * protocol. The parser has no side effects - all actions are performed
 * through callbacks.
 *
 * Protocol overview:
 * - Bytes 0x00-0x1F: Commands (some require parameter bytes)
 * - Bytes 0x20-0x7F: Text characters (when session is open)
 * - Admin command (0x00) requires a sub-command byte
 *
 * ARCHITECTURE.md compliance:
 * - No heap allocation
 * - Pure state machine with no side effects
 * - Callbacks handle all actions
 */

#ifndef KEYER_WINKEYER_PARSER_H
#define KEYER_WINKEYER_PARSER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Parser State Machine
 * ============================================================================ */

/**
 * @brief Parser state machine states
 */
typedef enum {
    WK_STATE_IDLE,           /**< Waiting for command or text */
    WK_STATE_ADMIN_WAIT_SUB, /**< Waiting for admin sub-command */
    WK_STATE_CMD_WAIT_PARAM1,/**< Waiting for first parameter byte */
    WK_STATE_CMD_WAIT_PARAM2,/**< Waiting for second parameter byte */
} winkeyer_parser_state_t;

/**
 * @brief Parser context structure
 *
 * Maintains parser state between byte processing calls.
 * All fields are internal state - do not modify directly.
 */
typedef struct {
    winkeyer_parser_state_t state;    /**< Current parser state */
    uint8_t current_cmd;              /**< Command being processed */
    uint8_t param1;                   /**< First parameter (if waiting for second) */
    bool session_open;                /**< Host session is active */
} winkeyer_parser_t;

/* ============================================================================
 * Callback Interface
 *
 * Callbacks are invoked when the parser recognizes a complete command.
 * NULL callbacks are safely ignored.
 * ============================================================================ */

/**
 * @brief Callback interface for parser actions
 *
 * All callbacks are optional - set to NULL if not needed.
 * The ctx pointer is passed to all callbacks for user context.
 */
typedef struct {
    /** Called when host opens session */
    void (*on_host_open)(void *ctx);

    /** Called when host closes session */
    void (*on_host_close)(void *ctx);

    /** Called when speed command is received */
    void (*on_speed)(void *ctx, uint8_t wpm);

    /** Called when sidetone command is received */
    void (*on_sidetone)(void *ctx, uint8_t freq_code);

    /** Called when weight command is received */
    void (*on_weight)(void *ctx, uint8_t weight);

    /** Called for each text character received */
    void (*on_text)(void *ctx, char c);

    /** Called when clear buffer command is received */
    void (*on_clear_buffer)(void *ctx);

    /** Called when key immediate command is received */
    void (*on_key_immediate)(void *ctx, bool down);

    /** Called when pause command is received */
    void (*on_pause)(void *ctx, bool paused);

    /** Called when PTT timing command is received */
    void (*on_ptt_timing)(void *ctx, uint8_t lead_in, uint8_t tail);

    /** Called when pin config command is received */
    void (*on_pin_config)(void *ctx, uint8_t config);

    /** Called when mode command is received */
    void (*on_mode)(void *ctx, uint8_t mode);

    /** User context pointer passed to all callbacks */
    void *ctx;
} winkeyer_callbacks_t;

/* ============================================================================
 * Parser Functions
 * ============================================================================ */

/**
 * @brief Initialize parser to default state
 *
 * Must be called before processing any bytes.
 *
 * @param parser Parser context to initialize
 */
void winkeyer_parser_init(winkeyer_parser_t *parser);

/**
 * @brief Process a single byte through the parser
 *
 * The parser maintains state between calls. Feed bytes one at a time
 * from the USB CDC receive buffer.
 *
 * Callbacks are invoked when complete commands are recognized.
 * Response bytes (if any) are written to the response buffer.
 *
 * @param parser Parser context
 * @param byte Input byte to process
 * @param callbacks Callback interface (may be NULL)
 * @param response Buffer for response bytes (may be NULL)
 * @param response_len Output: number of response bytes written (may be NULL)
 */
void winkeyer_parser_byte(winkeyer_parser_t *parser,
                          uint8_t byte,
                          winkeyer_callbacks_t *callbacks,
                          uint8_t *response,
                          size_t *response_len);

/**
 * @brief Check if session is currently open
 *
 * @param parser Parser context
 * @return true if session is open, false otherwise
 */
static inline bool winkeyer_parser_is_open(const winkeyer_parser_t *parser) {
    return parser->session_open;
}

/**
 * @brief Reset parser to initial state
 *
 * Clears all state including session_open flag.
 * Equivalent to winkeyer_parser_init().
 *
 * @param parser Parser context
 */
static inline void winkeyer_parser_reset(winkeyer_parser_t *parser) {
    winkeyer_parser_init(parser);
}

#ifdef __cplusplus
}
#endif

#endif /* KEYER_WINKEYER_PARSER_H */
