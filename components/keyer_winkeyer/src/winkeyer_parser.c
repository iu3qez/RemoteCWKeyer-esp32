/**
 * @file winkeyer_parser.c
 * @brief Winkeyer v3 protocol parser implementation
 *
 * Implements a pure state machine parser for the K1EL Winkeyer v3 protocol.
 *
 * ARCHITECTURE.md compliance:
 * - No heap allocation
 * - Pure state machine with no side effects
 * - O(1) time complexity per byte
 */

#include "winkeyer_parser.h"
#include "winkeyer_protocol.h"
#include <string.h>

/* ============================================================================
 * Helper Macros
 * ============================================================================ */

/** Safely invoke a callback if it exists */
#define INVOKE_CALLBACK(cb_ptr, ...) \
    do { \
        if ((cb_ptr) != NULL) { \
            (cb_ptr)(__VA_ARGS__); \
        } \
    } while (0)

/** Set response byte if buffer is valid */
#define SET_RESPONSE(resp, len_ptr, value) \
    do { \
        if ((resp) != NULL && (len_ptr) != NULL) { \
            (resp)[*(len_ptr)] = (value); \
            (*(len_ptr))++; \
        } \
    } while (0)

/* ============================================================================
 * Command Parameter Counts
 *
 * Defines how many parameter bytes each command expects.
 * Returns 0 for commands with no parameters, 1 for single-param, etc.
 * ============================================================================ */

static uint8_t get_param_count(uint8_t cmd) {
    switch (cmd) {
        /* Commands with no parameters */
        case WK_CMD_CLEAR_BUFFER:
        case WK_CMD_BACKSPACE:
        case WK_CMD_GET_SPEED_POT:
        case WK_CMD_LOAD_DEFAULTS:
            return 0;

        /* Commands with 1 parameter */
        case WK_CMD_SIDETONE:
        case WK_CMD_SPEED:
        case WK_CMD_WEIGHT:
        case WK_CMD_SPEED_POT:
        case WK_CMD_PAUSE:
        case WK_CMD_PIN_CONFIG:
        case WK_CMD_KEY_IMMEDIATE:
        case WK_CMD_HSCW_SPEED:
        case WK_CMD_FARNSWORTH:
        case WK_CMD_WINKEY_MODE:
            return 1;

        /* Commands with 2 parameters */
        case WK_CMD_PTT_TIMING:
            return 2;

        default:
            return 0;
    }
}

/* ============================================================================
 * Admin Sub-Command Processing
 * ============================================================================ */

static void process_admin_subcmd(winkeyer_parser_t *parser,
                                 uint8_t subcmd,
                                 winkeyer_callbacks_t *cb,
                                 uint8_t *response,
                                 size_t *response_len) {
    switch (subcmd) {
        case WK_ADMIN_HOST_OPEN:
            parser->session_open = true;
            if (cb != NULL) {
                INVOKE_CALLBACK(cb->on_host_open, cb->ctx);
            }
            /* Return version byte */
            SET_RESPONSE(response, response_len, WK_VERSION);
            parser->state = WK_STATE_IDLE;
            break;

        case WK_ADMIN_HOST_CLOSE:
            parser->session_open = false;
            if (cb != NULL) {
                INVOKE_CALLBACK(cb->on_host_close, cb->ctx);
            }
            parser->state = WK_STATE_IDLE;
            break;

        case WK_ADMIN_RESET:
            parser->session_open = false;
            parser->state = WK_STATE_IDLE;
            break;

        case WK_ADMIN_ECHO:
            /* Echo requires one more byte */
            parser->current_cmd = WK_CMD_ADMIN;  /* Mark as admin echo */
            parser->param1 = WK_ADMIN_ECHO;      /* Remember sub-command */
            parser->state = WK_STATE_CMD_WAIT_PARAM1;
            break;

        case WK_ADMIN_CALIBRATE:
        case WK_ADMIN_PADDLE_A2D:
        case WK_ADMIN_SPEED_A2D:
        case WK_ADMIN_GET_VALUES:
        case WK_ADMIN_GET_CAL:
        case WK_ADMIN_SET_WK1_MODE:
        case WK_ADMIN_SET_WK2_MODE:
        case WK_ADMIN_SET_WK3_MODE:
        case WK_ADMIN_SET_HIGHBAUD:
        case WK_ADMIN_SET_LOWBAUD:
        case WK_ADMIN_STANDALONE:
            /* These commands are acknowledged but not implemented */
            parser->state = WK_STATE_IDLE;
            break;

        default:
            /* Unknown sub-command, return to idle */
            parser->state = WK_STATE_IDLE;
            break;
    }
}

/* ============================================================================
 * Command Processing
 * ============================================================================ */

static void process_command(winkeyer_parser_t *parser,
                            uint8_t cmd,
                            winkeyer_callbacks_t *cb,
                            uint8_t *response,
                            size_t *response_len) {
    (void)response;
    (void)response_len;

    /* Admin command is special - needs sub-command */
    if (cmd == WK_CMD_ADMIN) {
        parser->state = WK_STATE_ADMIN_WAIT_SUB;
        return;
    }

    /* Most commands require an open session */
    if (!parser->session_open) {
        /* Without session, consume any expected parameters silently */
        uint8_t params = get_param_count(cmd);
        if (params > 0) {
            parser->current_cmd = cmd;
            parser->state = WK_STATE_CMD_WAIT_PARAM1;
        }
        return;
    }

    /* Check if command has parameters */
    uint8_t param_count = get_param_count(cmd);

    if (param_count == 0) {
        /* Immediate command - execute now */
        switch (cmd) {
            case WK_CMD_CLEAR_BUFFER:
                if (cb != NULL) {
                    INVOKE_CALLBACK(cb->on_clear_buffer, cb->ctx);
                }
                break;

            case WK_CMD_BACKSPACE:
            case WK_CMD_GET_SPEED_POT:
            case WK_CMD_LOAD_DEFAULTS:
                /* Acknowledged but not fully implemented */
                break;
        }
        /* Stay in IDLE */
    } else {
        /* Command needs parameters */
        parser->current_cmd = cmd;
        parser->state = WK_STATE_CMD_WAIT_PARAM1;
    }
}

/* ============================================================================
 * Parameter Processing
 * ============================================================================ */

static void process_param1(winkeyer_parser_t *parser,
                           uint8_t param,
                           winkeyer_callbacks_t *cb,
                           uint8_t *response,
                           size_t *response_len) {
    /* Special case: Admin Echo - just echo the byte back */
    if (parser->current_cmd == WK_CMD_ADMIN && parser->param1 == WK_ADMIN_ECHO) {
        SET_RESPONSE(response, response_len, param);
        parser->state = WK_STATE_IDLE;
        return;
    }

    /* If session not open, just consume parameters silently */
    if (!parser->session_open) {
        uint8_t param_count = get_param_count(parser->current_cmd);
        if (param_count == 2) {
            parser->state = WK_STATE_CMD_WAIT_PARAM2;
        } else {
            parser->state = WK_STATE_IDLE;
        }
        return;
    }

    /* Check if we need a second parameter */
    uint8_t param_count = get_param_count(parser->current_cmd);
    if (param_count == 2) {
        /* Save first param and wait for second */
        parser->param1 = param;
        parser->state = WK_STATE_CMD_WAIT_PARAM2;
        return;
    }

    /* Single parameter command - execute now */
    switch (parser->current_cmd) {
        case WK_CMD_SPEED:
            if (cb != NULL) {
                INVOKE_CALLBACK(cb->on_speed, cb->ctx, param);
            }
            break;

        case WK_CMD_SIDETONE:
            if (cb != NULL) {
                INVOKE_CALLBACK(cb->on_sidetone, cb->ctx, param);
            }
            break;

        case WK_CMD_WEIGHT:
            if (cb != NULL) {
                INVOKE_CALLBACK(cb->on_weight, cb->ctx, param);
            }
            break;

        case WK_CMD_PIN_CONFIG:
            if (cb != NULL) {
                INVOKE_CALLBACK(cb->on_pin_config, cb->ctx, param);
            }
            break;

        case WK_CMD_WINKEY_MODE:
            if (cb != NULL) {
                INVOKE_CALLBACK(cb->on_mode, cb->ctx, param);
            }
            break;

        case WK_CMD_KEY_IMMEDIATE:
            if (cb != NULL) {
                INVOKE_CALLBACK(cb->on_key_immediate, cb->ctx, param != 0);
            }
            break;

        case WK_CMD_PAUSE:
            if (cb != NULL) {
                INVOKE_CALLBACK(cb->on_pause, cb->ctx, param != 0);
            }
            break;

        case WK_CMD_SPEED_POT:
        case WK_CMD_HSCW_SPEED:
        case WK_CMD_FARNSWORTH:
            /* Acknowledged but not fully implemented */
            break;
    }

    parser->state = WK_STATE_IDLE;
}

static void process_param2(winkeyer_parser_t *parser,
                           uint8_t param,
                           winkeyer_callbacks_t *cb) {
    /* If session not open, just consume */
    if (!parser->session_open) {
        parser->state = WK_STATE_IDLE;
        return;
    }

    switch (parser->current_cmd) {
        case WK_CMD_PTT_TIMING:
            if (cb != NULL) {
                INVOKE_CALLBACK(cb->on_ptt_timing, cb->ctx, parser->param1, param);
            }
            break;
    }

    parser->state = WK_STATE_IDLE;
}

/* ============================================================================
 * Text Character Processing
 * ============================================================================ */

static void process_text(winkeyer_parser_t *parser,
                         uint8_t c,
                         winkeyer_callbacks_t *cb) {
    /* Text only accepted when session is open */
    if (!parser->session_open) {
        return;
    }

    if (cb != NULL) {
        INVOKE_CALLBACK(cb->on_text, cb->ctx, (char)c);
    }
}

/* ============================================================================
 * Public API
 * ============================================================================ */

void winkeyer_parser_init(winkeyer_parser_t *parser) {
    if (parser == NULL) {
        return;
    }

    parser->state = WK_STATE_IDLE;
    parser->current_cmd = 0;
    parser->param1 = 0;
    parser->session_open = false;
}

void winkeyer_parser_byte(winkeyer_parser_t *parser,
                          uint8_t byte,
                          winkeyer_callbacks_t *callbacks,
                          uint8_t *response,
                          size_t *response_len) {
    if (parser == NULL) {
        return;
    }

    /* Initialize response length if provided */
    if (response_len != NULL) {
        *response_len = 0;
    }

    switch (parser->state) {
        case WK_STATE_IDLE:
            if (byte <= WK_CMD_MAX) {
                /* Command byte */
                process_command(parser, byte, callbacks, response, response_len);
            } else if (byte >= WK_TEXT_MIN && byte <= WK_TEXT_MAX) {
                /* Text character */
                process_text(parser, byte, callbacks);
            }
            /* Bytes 0x80-0xBF and 0xC0-0xFF are ignored in IDLE state */
            break;

        case WK_STATE_ADMIN_WAIT_SUB:
            process_admin_subcmd(parser, byte, callbacks, response, response_len);
            break;

        case WK_STATE_CMD_WAIT_PARAM1:
            process_param1(parser, byte, callbacks, response, response_len);
            break;

        case WK_STATE_CMD_WAIT_PARAM2:
            process_param2(parser, byte, callbacks);
            break;
    }
}
