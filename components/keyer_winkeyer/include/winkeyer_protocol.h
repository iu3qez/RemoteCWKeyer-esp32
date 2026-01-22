/**
 * @file winkeyer_protocol.h
 * @brief K1EL Winkeyer v3 protocol constants
 *
 * Defines command codes, admin sub-commands, status flags, and other
 * constants for the Winkeyer v3 binary protocol.
 *
 * Protocol reference: K1EL Winkeyer 3 Interface and Operation Manual
 *
 * ARCHITECTURE.md compliance:
 * - Header-only constants, no runtime overhead
 * - No heap allocation
 */

#ifndef KEYER_WINKEYER_PROTOCOL_H
#define KEYER_WINKEYER_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Version
 * ============================================================================ */

/** Winkeyer version to report (23 = Winkeyer 3) */
#define WK_VERSION              23

/* ============================================================================
 * Command Codes (0x00-0x1F)
 *
 * Bytes in range 0x00-0x1F are interpreted as commands.
 * Bytes in range 0x20-0x7F are text characters (when session is open).
 * ============================================================================ */

#define WK_CMD_ADMIN            0x00  /**< Admin command (requires sub-command) */
#define WK_CMD_SIDETONE         0x01  /**< Set sidetone frequency */
#define WK_CMD_SPEED            0x02  /**< Set keyer speed (WPM) */
#define WK_CMD_WEIGHT           0x03  /**< Set keying weight (dit/dah ratio) */
#define WK_CMD_PTT_TIMING       0x04  /**< Set PTT lead-in/tail timing */
#define WK_CMD_SPEED_POT        0x05  /**< Setup speed potentiometer */
#define WK_CMD_PAUSE            0x06  /**< Pause/resume sending */
#define WK_CMD_GET_SPEED_POT    0x07  /**< Get current speed pot value */
#define WK_CMD_BACKSPACE        0x08  /**< Delete last character in buffer */
#define WK_CMD_PIN_CONFIG       0x09  /**< Configure pin assignments */
#define WK_CMD_CLEAR_BUFFER     0x0A  /**< Clear transmit buffer */
#define WK_CMD_KEY_IMMEDIATE    0x0B  /**< Immediate key down/up */
#define WK_CMD_HSCW_SPEED       0x0C  /**< Set HSCW speed */
#define WK_CMD_FARNSWORTH       0x0D  /**< Set Farnsworth spacing */
#define WK_CMD_WINKEY_MODE      0x0E  /**< Set Winkeyer mode register */
#define WK_CMD_LOAD_DEFAULTS    0x0F  /**< Load default settings */

/* Extended commands (0x10-0x1F) - some are Winkeyer 3 specific */
#define WK_CMD_FIRST_EXTENSION  0x10  /**< First extension command */
#define WK_CMD_KEY_BUFFERED     0x11  /**< Buffered key down/up */
#define WK_CMD_WAIT             0x12  /**< Wait for specified time */
#define WK_CMD_MERGE            0x13  /**< Merge dit/dah letters */
#define WK_CMD_SPEED_CHANGE     0x14  /**< Buffered speed change */
#define WK_CMD_CANCEL_SPEED     0x15  /**< Cancel buffered speed change */
#define WK_CMD_NOP              0x16  /**< No operation (timing) */

/* Note: 0x17-0x1F reserved for future use */

/* ============================================================================
 * Admin Sub-Commands
 *
 * Used after WK_CMD_ADMIN (0x00)
 * ============================================================================ */

#define WK_ADMIN_CALIBRATE      0x00  /**< Calibrate timing */
#define WK_ADMIN_RESET          0x01  /**< Reset Winkeyer to defaults */
#define WK_ADMIN_HOST_OPEN      0x02  /**< Open host session */
#define WK_ADMIN_HOST_CLOSE     0x03  /**< Close host session */
#define WK_ADMIN_ECHO           0x04  /**< Echo test byte */
#define WK_ADMIN_PADDLE_A2D     0x05  /**< Read paddle A2D value */
#define WK_ADMIN_SPEED_A2D      0x06  /**< Read speed pot A2D value */
#define WK_ADMIN_GET_VALUES     0x07  /**< Get current values */
#define WK_ADMIN_RESERVED_08    0x08  /**< Reserved */
#define WK_ADMIN_GET_CAL        0x09  /**< Get calibration value */
#define WK_ADMIN_SET_WK1_MODE   0x0A  /**< Set WK1 compatibility mode */
#define WK_ADMIN_SET_WK2_MODE   0x0B  /**< Set WK2 compatibility mode */
#define WK_ADMIN_DUMP_EEPROM    0x0C  /**< Dump EEPROM contents */
#define WK_ADMIN_LOAD_EEPROM    0x0D  /**< Load EEPROM contents */
#define WK_ADMIN_SEND_MSG       0x0E  /**< Send stored message */
#define WK_ADMIN_LOAD_X1MODE    0x0F  /**< Load extension mode */
#define WK_ADMIN_RESERVED_10    0x10  /**< Reserved */
#define WK_ADMIN_SET_HIGHBAUD   0x11  /**< Set high baud rate */
#define WK_ADMIN_SET_LOWBAUD    0x12  /**< Set low baud rate (default) */
#define WK_ADMIN_RESERVED_13    0x13  /**< Reserved */
#define WK_ADMIN_STANDALONE     0x14  /**< Enter standalone mode */
#define WK_ADMIN_SET_WK3_MODE   0x15  /**< Set WK3 mode */

/* ============================================================================
 * Status Byte
 *
 * Status bytes have format: 0xC0 | flags
 * This makes them distinguishable from commands (0x00-0x1F) and text (0x20-0x7F)
 * ============================================================================ */

#define WK_STATUS_BASE          0xC0  /**< Status byte base value */
#define WK_STATUS_MASK          0x3F  /**< Mask for status flags */

/* Status flags (OR'd with WK_STATUS_BASE) */
#define WK_STATUS_XOFF          0x01  /**< Buffer 2/3 full, XOFF */
#define WK_STATUS_BREAKIN       0x02  /**< Break-in (paddle interrupt) */
#define WK_STATUS_BUSY          0x04  /**< Currently transmitting */
#define WK_STATUS_WAIT          0x08  /**< Waiting */
#define WK_STATUS_KEYDOWN       0x10  /**< Key is down */

/* ============================================================================
 * Mode Register Bits (for WK_CMD_WINKEY_MODE)
 * ============================================================================ */

#define WK_MODE_PADDLE_SWAP     0x01  /**< Swap dit/dah paddles */
#define WK_MODE_AUTO_SPACE      0x02  /**< Auto letter spacing */
#define WK_MODE_CT_SPACE        0x04  /**< Contest spacing (shorter) */
#define WK_MODE_PADDLE_ECHO     0x08  /**< Echo paddle characters */
#define WK_MODE_IAMBIC_B        0x10  /**< Iambic mode B (vs A) */
#define WK_MODE_PADDLE_FLIP     0x20  /**< Reverse paddle polarity */
#define WK_MODE_SERIAL          0x40  /**< Serial number mode */
#define WK_MODE_SIDETONE_ON     0x80  /**< Sidetone enabled */

/* ============================================================================
 * Pin Configuration Bits (for WK_CMD_PIN_CONFIG)
 * ============================================================================ */

#define WK_PIN_PTT_ENABLE       0x01  /**< Enable PTT output */
#define WK_PIN_SIDETONE_ENABLE  0x02  /**< Enable sidetone output */
#define WK_PIN_KEY_INVERT       0x04  /**< Invert key output polarity */
#define WK_PIN_PTT_INVERT       0x08  /**< Invert PTT output polarity */
#define WK_PIN_PADDLE_ONLY      0x10  /**< Paddle only mode */

/* ============================================================================
 * Default Values
 * ============================================================================ */

#define WK_DEFAULT_SPEED        20    /**< Default speed: 20 WPM */
#define WK_DEFAULT_WEIGHT       50    /**< Default weight: 50 (neutral) */
#define WK_DEFAULT_SIDETONE     5     /**< Default sidetone: ~667 Hz */
#define WK_DEFAULT_PTT_LEAD     0     /**< Default PTT lead-in: 0 */
#define WK_DEFAULT_PTT_TAIL     0     /**< Default PTT tail: 0 */

/* ============================================================================
 * Protocol Limits
 * ============================================================================ */

#define WK_MIN_SPEED            5     /**< Minimum speed: 5 WPM */
#define WK_MAX_SPEED            99    /**< Maximum speed: 99 WPM */
#define WK_MIN_WEIGHT           10    /**< Minimum weight */
#define WK_MAX_WEIGHT           90    /**< Maximum weight */

/* Text character range */
#define WK_TEXT_MIN             0x20  /**< First printable character (space) */
#define WK_TEXT_MAX             0x7E  /**< Last printable character (~) */

/* Command range */
#define WK_CMD_MIN              0x00  /**< First command */
#define WK_CMD_MAX              0x1F  /**< Last command */

#ifdef __cplusplus
}
#endif

#endif /* KEYER_WINKEYER_PROTOCOL_H */
