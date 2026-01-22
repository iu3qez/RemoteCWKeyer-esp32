/**
 * @file test_winkeyer_parser.c
 * @brief Unit tests for Winkeyer v3 protocol parser
 *
 * Tests the state machine parser for the K1EL Winkeyer v3 binary protocol.
 * The parser is a pure state machine with no side effects - all actions
 * are performed through callbacks.
 */

#include "unity.h"
#include "winkeyer_protocol.h"
#include "winkeyer_parser.h"
#include <string.h>
#include <stdbool.h>

/* ============================================================================
 * Test Fixtures
 * ============================================================================ */

/* Track callback invocations for verification */
static struct {
    bool host_open_called;
    bool host_close_called;
    bool speed_called;
    uint8_t speed_wpm;
    bool sidetone_called;
    uint8_t sidetone_freq_code;
    bool weight_called;
    uint8_t weight_value;
    bool text_called;
    char text_chars[128];
    size_t text_count;
    bool clear_buffer_called;
    bool key_immediate_called;
    bool key_immediate_down;
    bool pause_called;
    bool pause_state;
    bool ptt_timing_called;
    uint8_t ptt_lead_in;
    uint8_t ptt_tail;
    bool pin_config_called;
    uint8_t pin_config_value;
    bool mode_called;
    uint8_t mode_value;
} g_test_state;

static void reset_test_state(void) {
    memset(&g_test_state, 0, sizeof(g_test_state));
}

/* Callback implementations */
static void test_on_host_open(void *ctx) {
    (void)ctx;
    g_test_state.host_open_called = true;
}

static void test_on_host_close(void *ctx) {
    (void)ctx;
    g_test_state.host_close_called = true;
}

static void test_on_speed(void *ctx, uint8_t wpm) {
    (void)ctx;
    g_test_state.speed_called = true;
    g_test_state.speed_wpm = wpm;
}

static void test_on_sidetone(void *ctx, uint8_t freq_code) {
    (void)ctx;
    g_test_state.sidetone_called = true;
    g_test_state.sidetone_freq_code = freq_code;
}

static void test_on_weight(void *ctx, uint8_t weight) {
    (void)ctx;
    g_test_state.weight_called = true;
    g_test_state.weight_value = weight;
}

static void test_on_text(void *ctx, char c) {
    (void)ctx;
    g_test_state.text_called = true;
    if (g_test_state.text_count < sizeof(g_test_state.text_chars) - 1) {
        g_test_state.text_chars[g_test_state.text_count++] = c;
        g_test_state.text_chars[g_test_state.text_count] = '\0';
    }
}

static void test_on_clear_buffer(void *ctx) {
    (void)ctx;
    g_test_state.clear_buffer_called = true;
}

static void test_on_key_immediate(void *ctx, bool down) {
    (void)ctx;
    g_test_state.key_immediate_called = true;
    g_test_state.key_immediate_down = down;
}

static void test_on_pause(void *ctx, bool paused) {
    (void)ctx;
    g_test_state.pause_called = true;
    g_test_state.pause_state = paused;
}

static void test_on_ptt_timing(void *ctx, uint8_t lead_in, uint8_t tail) {
    (void)ctx;
    g_test_state.ptt_timing_called = true;
    g_test_state.ptt_lead_in = lead_in;
    g_test_state.ptt_tail = tail;
}

static void test_on_pin_config(void *ctx, uint8_t config) {
    (void)ctx;
    g_test_state.pin_config_called = true;
    g_test_state.pin_config_value = config;
}

static void test_on_mode(void *ctx, uint8_t mode) {
    (void)ctx;
    g_test_state.mode_called = true;
    g_test_state.mode_value = mode;
}

static winkeyer_callbacks_t test_callbacks = {
    .on_host_open = test_on_host_open,
    .on_host_close = test_on_host_close,
    .on_speed = test_on_speed,
    .on_sidetone = test_on_sidetone,
    .on_weight = test_on_weight,
    .on_text = test_on_text,
    .on_clear_buffer = test_on_clear_buffer,
    .on_key_immediate = test_on_key_immediate,
    .on_pause = test_on_pause,
    .on_ptt_timing = test_on_ptt_timing,
    .on_pin_config = test_on_pin_config,
    .on_mode = test_on_mode,
    .ctx = NULL
};

/* ============================================================================
 * Parser Initialization Tests
 * ============================================================================ */

void test_parser_init(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);

    TEST_ASSERT_EQUAL(WK_STATE_IDLE, parser.state);
    TEST_ASSERT_FALSE(parser.session_open);
}

/* ============================================================================
 * Admin Command Tests
 * ============================================================================ */

void test_parser_host_open(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);
    reset_test_state();

    uint8_t response[8];
    size_t response_len = 0;

    /* Send Admin command */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_ADMIN_WAIT_SUB, parser.state);
    TEST_ASSERT_EQUAL(0, response_len);

    /* Send Host Open sub-command */
    winkeyer_parser_byte(&parser, WK_ADMIN_HOST_OPEN, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_IDLE, parser.state);
    TEST_ASSERT_TRUE(parser.session_open);
    TEST_ASSERT_TRUE(g_test_state.host_open_called);

    /* Should return version byte */
    TEST_ASSERT_EQUAL(1, response_len);
    TEST_ASSERT_EQUAL(WK_VERSION, response[0]);
}

void test_parser_host_close(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);
    reset_test_state();

    uint8_t response[8];
    size_t response_len = 0;

    /* First open session */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, response, &response_len);
    winkeyer_parser_byte(&parser, WK_ADMIN_HOST_OPEN, &test_callbacks, response, &response_len);
    TEST_ASSERT_TRUE(parser.session_open);

    reset_test_state();
    response_len = 0;

    /* Now close session */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, response, &response_len);
    winkeyer_parser_byte(&parser, WK_ADMIN_HOST_CLOSE, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_IDLE, parser.state);
    TEST_ASSERT_FALSE(parser.session_open);
    TEST_ASSERT_TRUE(g_test_state.host_close_called);
    TEST_ASSERT_EQUAL(0, response_len);
}

void test_parser_admin_echo(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);
    reset_test_state();

    uint8_t response[8];
    size_t response_len = 0;

    /* Admin Echo command returns echoed byte */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_ADMIN_WAIT_SUB, parser.state);

    /* Sub-command Echo with parameter */
    winkeyer_parser_byte(&parser, WK_ADMIN_ECHO, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_CMD_WAIT_PARAM1, parser.state);

    /* Send byte to echo */
    winkeyer_parser_byte(&parser, 0x42, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_IDLE, parser.state);
    TEST_ASSERT_EQUAL(1, response_len);
    TEST_ASSERT_EQUAL(0x42, response[0]);
}

void test_parser_admin_reset(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);
    reset_test_state();

    uint8_t response[8];
    size_t response_len = 0;

    /* Open session first */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, response, &response_len);
    winkeyer_parser_byte(&parser, WK_ADMIN_HOST_OPEN, &test_callbacks, response, &response_len);
    TEST_ASSERT_TRUE(parser.session_open);

    response_len = 0;

    /* Reset should close session */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, response, &response_len);
    winkeyer_parser_byte(&parser, WK_ADMIN_RESET, &test_callbacks, response, &response_len);
    TEST_ASSERT_FALSE(parser.session_open);
    TEST_ASSERT_EQUAL(WK_STATE_IDLE, parser.state);
}

/* ============================================================================
 * Speed Command Tests
 * ============================================================================ */

void test_parser_speed_command(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);
    reset_test_state();

    uint8_t response[8];
    size_t response_len = 0;

    /* Open session first */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, response, &response_len);
    winkeyer_parser_byte(&parser, WK_ADMIN_HOST_OPEN, &test_callbacks, response, &response_len);
    reset_test_state();
    response_len = 0;

    /* Send speed command */
    winkeyer_parser_byte(&parser, WK_CMD_SPEED, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_CMD_WAIT_PARAM1, parser.state);

    /* Send WPM value (25 WPM) */
    winkeyer_parser_byte(&parser, 25, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_IDLE, parser.state);
    TEST_ASSERT_TRUE(g_test_state.speed_called);
    TEST_ASSERT_EQUAL(25, g_test_state.speed_wpm);
}

void test_parser_speed_requires_session(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);
    reset_test_state();

    uint8_t response[8];
    size_t response_len = 0;

    /* Send speed command without opening session */
    winkeyer_parser_byte(&parser, WK_CMD_SPEED, &test_callbacks, response, &response_len);
    winkeyer_parser_byte(&parser, 25, &test_callbacks, response, &response_len);

    /* Callback should not be called */
    TEST_ASSERT_FALSE(g_test_state.speed_called);
    TEST_ASSERT_EQUAL(WK_STATE_IDLE, parser.state);
}

/* ============================================================================
 * Sidetone Command Tests
 * ============================================================================ */

void test_parser_sidetone_command(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);
    reset_test_state();

    uint8_t response[8];
    size_t response_len = 0;

    /* Open session */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, response, &response_len);
    winkeyer_parser_byte(&parser, WK_ADMIN_HOST_OPEN, &test_callbacks, response, &response_len);
    reset_test_state();
    response_len = 0;

    /* Send sidetone command */
    winkeyer_parser_byte(&parser, WK_CMD_SIDETONE, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_CMD_WAIT_PARAM1, parser.state);

    /* Send frequency code (5 = 667 Hz) */
    winkeyer_parser_byte(&parser, 5, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_IDLE, parser.state);
    TEST_ASSERT_TRUE(g_test_state.sidetone_called);
    TEST_ASSERT_EQUAL(5, g_test_state.sidetone_freq_code);
}

/* ============================================================================
 * Weight Command Tests
 * ============================================================================ */

void test_parser_weight_command(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);
    reset_test_state();

    uint8_t response[8];
    size_t response_len = 0;

    /* Open session */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, response, &response_len);
    winkeyer_parser_byte(&parser, WK_ADMIN_HOST_OPEN, &test_callbacks, response, &response_len);
    reset_test_state();
    response_len = 0;

    /* Send weight command */
    winkeyer_parser_byte(&parser, WK_CMD_WEIGHT, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_CMD_WAIT_PARAM1, parser.state);

    /* Send weight value (50 = normal) */
    winkeyer_parser_byte(&parser, 50, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_IDLE, parser.state);
    TEST_ASSERT_TRUE(g_test_state.weight_called);
    TEST_ASSERT_EQUAL(50, g_test_state.weight_value);
}

/* ============================================================================
 * PTT Timing Command Tests
 * ============================================================================ */

void test_parser_ptt_timing_command(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);
    reset_test_state();

    uint8_t response[8];
    size_t response_len = 0;

    /* Open session */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, response, &response_len);
    winkeyer_parser_byte(&parser, WK_ADMIN_HOST_OPEN, &test_callbacks, response, &response_len);
    reset_test_state();
    response_len = 0;

    /* Send PTT timing command */
    winkeyer_parser_byte(&parser, WK_CMD_PTT_TIMING, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_CMD_WAIT_PARAM1, parser.state);

    /* Send lead-in */
    winkeyer_parser_byte(&parser, 10, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_CMD_WAIT_PARAM2, parser.state);

    /* Send tail */
    winkeyer_parser_byte(&parser, 5, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_IDLE, parser.state);
    TEST_ASSERT_TRUE(g_test_state.ptt_timing_called);
    TEST_ASSERT_EQUAL(10, g_test_state.ptt_lead_in);
    TEST_ASSERT_EQUAL(5, g_test_state.ptt_tail);
}

/* ============================================================================
 * Pin Config Command Tests
 * ============================================================================ */

void test_parser_pin_config_command(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);
    reset_test_state();

    uint8_t response[8];
    size_t response_len = 0;

    /* Open session */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, response, &response_len);
    winkeyer_parser_byte(&parser, WK_ADMIN_HOST_OPEN, &test_callbacks, response, &response_len);
    reset_test_state();
    response_len = 0;

    /* Send pin config command */
    winkeyer_parser_byte(&parser, WK_CMD_PIN_CONFIG, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_CMD_WAIT_PARAM1, parser.state);

    /* Send config value */
    winkeyer_parser_byte(&parser, 0x05, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_IDLE, parser.state);
    TEST_ASSERT_TRUE(g_test_state.pin_config_called);
    TEST_ASSERT_EQUAL(0x05, g_test_state.pin_config_value);
}

/* ============================================================================
 * Winkeyer Mode Command Tests
 * ============================================================================ */

void test_parser_mode_command(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);
    reset_test_state();

    uint8_t response[8];
    size_t response_len = 0;

    /* Open session */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, response, &response_len);
    winkeyer_parser_byte(&parser, WK_ADMIN_HOST_OPEN, &test_callbacks, response, &response_len);
    reset_test_state();
    response_len = 0;

    /* Send mode command */
    winkeyer_parser_byte(&parser, WK_CMD_WINKEY_MODE, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_CMD_WAIT_PARAM1, parser.state);

    /* Send mode value (iambic B) */
    winkeyer_parser_byte(&parser, 0x02, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_IDLE, parser.state);
    TEST_ASSERT_TRUE(g_test_state.mode_called);
    TEST_ASSERT_EQUAL(0x02, g_test_state.mode_value);
}

/* ============================================================================
 * Text Character Tests
 * ============================================================================ */

void test_parser_text_characters(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);
    reset_test_state();

    uint8_t response[8];
    size_t response_len = 0;

    /* Open session */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, response, &response_len);
    winkeyer_parser_byte(&parser, WK_ADMIN_HOST_OPEN, &test_callbacks, response, &response_len);
    reset_test_state();

    /* Send text "CQ" */
    winkeyer_parser_byte(&parser, 'C', &test_callbacks, response, &response_len);
    winkeyer_parser_byte(&parser, 'Q', &test_callbacks, response, &response_len);

    TEST_ASSERT_TRUE(g_test_state.text_called);
    TEST_ASSERT_EQUAL_STRING("CQ", g_test_state.text_chars);
    TEST_ASSERT_EQUAL(2, g_test_state.text_count);
}

void test_parser_text_requires_session(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);
    reset_test_state();

    uint8_t response[8];
    size_t response_len = 0;

    /* Send text without session - should be ignored */
    winkeyer_parser_byte(&parser, 'A', &test_callbacks, response, &response_len);

    TEST_ASSERT_FALSE(g_test_state.text_called);
    TEST_ASSERT_EQUAL(0, g_test_state.text_count);
}

void test_parser_text_full_alphabet(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);
    reset_test_state();

    uint8_t response[8];
    size_t response_len = 0;

    /* Open session */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, response, &response_len);
    winkeyer_parser_byte(&parser, WK_ADMIN_HOST_OPEN, &test_callbacks, response, &response_len);
    reset_test_state();

    /* Send printable characters (0x20-0x7F range) */
    for (uint8_t c = 0x20; c < 0x7F; c++) {
        winkeyer_parser_byte(&parser, c, &test_callbacks, response, &response_len);
    }

    TEST_ASSERT_TRUE(g_test_state.text_called);
    TEST_ASSERT_EQUAL(95, g_test_state.text_count);  /* 0x7F - 0x20 = 95 chars */
}

/* ============================================================================
 * Clear Buffer Command Tests
 * ============================================================================ */

void test_parser_clear_buffer_command(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);
    reset_test_state();

    uint8_t response[8];
    size_t response_len = 0;

    /* Open session */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, response, &response_len);
    winkeyer_parser_byte(&parser, WK_ADMIN_HOST_OPEN, &test_callbacks, response, &response_len);
    reset_test_state();
    response_len = 0;

    /* Send clear buffer command (no parameters) */
    winkeyer_parser_byte(&parser, WK_CMD_CLEAR_BUFFER, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_IDLE, parser.state);
    TEST_ASSERT_TRUE(g_test_state.clear_buffer_called);
}

/* ============================================================================
 * Key Immediate Command Tests
 * ============================================================================ */

void test_parser_key_immediate_down(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);
    reset_test_state();

    uint8_t response[8];
    size_t response_len = 0;

    /* Open session */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, response, &response_len);
    winkeyer_parser_byte(&parser, WK_ADMIN_HOST_OPEN, &test_callbacks, response, &response_len);
    reset_test_state();
    response_len = 0;

    /* Send key immediate command */
    winkeyer_parser_byte(&parser, WK_CMD_KEY_IMMEDIATE, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_CMD_WAIT_PARAM1, parser.state);

    /* Send key down (non-zero) */
    winkeyer_parser_byte(&parser, 0x01, &test_callbacks, response, &response_len);
    TEST_ASSERT_TRUE(g_test_state.key_immediate_called);
    TEST_ASSERT_TRUE(g_test_state.key_immediate_down);
}

void test_parser_key_immediate_up(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);
    reset_test_state();

    uint8_t response[8];
    size_t response_len = 0;

    /* Open session */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, response, &response_len);
    winkeyer_parser_byte(&parser, WK_ADMIN_HOST_OPEN, &test_callbacks, response, &response_len);
    reset_test_state();
    response_len = 0;

    /* Send key immediate command */
    winkeyer_parser_byte(&parser, WK_CMD_KEY_IMMEDIATE, &test_callbacks, response, &response_len);

    /* Send key up (zero) */
    winkeyer_parser_byte(&parser, 0x00, &test_callbacks, response, &response_len);
    TEST_ASSERT_TRUE(g_test_state.key_immediate_called);
    TEST_ASSERT_FALSE(g_test_state.key_immediate_down);
}

/* ============================================================================
 * Pause Command Tests
 * ============================================================================ */

void test_parser_pause_command(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);
    reset_test_state();

    uint8_t response[8];
    size_t response_len = 0;

    /* Open session */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, response, &response_len);
    winkeyer_parser_byte(&parser, WK_ADMIN_HOST_OPEN, &test_callbacks, response, &response_len);
    reset_test_state();
    response_len = 0;

    /* Send pause command */
    winkeyer_parser_byte(&parser, WK_CMD_PAUSE, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_CMD_WAIT_PARAM1, parser.state);

    /* Send pause state (non-zero = paused) */
    winkeyer_parser_byte(&parser, 0x01, &test_callbacks, response, &response_len);
    TEST_ASSERT_TRUE(g_test_state.pause_called);
    TEST_ASSERT_TRUE(g_test_state.pause_state);
}

void test_parser_unpause_command(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);
    reset_test_state();

    uint8_t response[8];
    size_t response_len = 0;

    /* Open session */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, response, &response_len);
    winkeyer_parser_byte(&parser, WK_ADMIN_HOST_OPEN, &test_callbacks, response, &response_len);
    reset_test_state();
    response_len = 0;

    /* Send pause command */
    winkeyer_parser_byte(&parser, WK_CMD_PAUSE, &test_callbacks, response, &response_len);

    /* Send unpause state (zero = unpaused) */
    winkeyer_parser_byte(&parser, 0x00, &test_callbacks, response, &response_len);
    TEST_ASSERT_TRUE(g_test_state.pause_called);
    TEST_ASSERT_FALSE(g_test_state.pause_state);
}

/* ============================================================================
 * Invalid Command Tests
 * ============================================================================ */

void test_parser_invalid_command_ignored(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);
    reset_test_state();

    uint8_t response[8];
    size_t response_len = 0;

    /* Open session */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, response, &response_len);
    winkeyer_parser_byte(&parser, WK_ADMIN_HOST_OPEN, &test_callbacks, response, &response_len);
    reset_test_state();
    response_len = 0;

    /* Send invalid command code (0x1F is reserved) */
    winkeyer_parser_byte(&parser, 0x1F, &test_callbacks, response, &response_len);

    /* Parser should stay in IDLE and not crash */
    TEST_ASSERT_EQUAL(WK_STATE_IDLE, parser.state);
    TEST_ASSERT_EQUAL(0, response_len);
}

void test_parser_invalid_admin_sub_ignored(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);
    reset_test_state();

    uint8_t response[8];
    size_t response_len = 0;

    /* Send Admin command */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, response, &response_len);

    /* Send invalid sub-command */
    winkeyer_parser_byte(&parser, 0xFF, &test_callbacks, response, &response_len);

    /* Parser should return to IDLE and not crash */
    TEST_ASSERT_EQUAL(WK_STATE_IDLE, parser.state);
}

/* ============================================================================
 * State Machine Transition Tests
 * ============================================================================ */

void test_parser_state_transitions(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);

    uint8_t response[8];
    size_t response_len = 0;

    /* Verify initial state */
    TEST_ASSERT_EQUAL(WK_STATE_IDLE, parser.state);

    /* Admin command -> ADMIN_WAIT_SUB */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_ADMIN_WAIT_SUB, parser.state);

    /* Host Open -> back to IDLE */
    winkeyer_parser_byte(&parser, WK_ADMIN_HOST_OPEN, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_IDLE, parser.state);

    /* Speed command -> CMD_WAIT_PARAM1 */
    winkeyer_parser_byte(&parser, WK_CMD_SPEED, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_CMD_WAIT_PARAM1, parser.state);

    /* Parameter -> back to IDLE */
    winkeyer_parser_byte(&parser, 25, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_IDLE, parser.state);
}

void test_parser_two_param_state_transitions(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);

    uint8_t response[8];
    size_t response_len = 0;

    /* Open session */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, response, &response_len);
    winkeyer_parser_byte(&parser, WK_ADMIN_HOST_OPEN, &test_callbacks, response, &response_len);

    /* PTT timing command -> CMD_WAIT_PARAM1 */
    winkeyer_parser_byte(&parser, WK_CMD_PTT_TIMING, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_CMD_WAIT_PARAM1, parser.state);

    /* First param -> CMD_WAIT_PARAM2 */
    winkeyer_parser_byte(&parser, 10, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_CMD_WAIT_PARAM2, parser.state);

    /* Second param -> back to IDLE */
    winkeyer_parser_byte(&parser, 5, &test_callbacks, response, &response_len);
    TEST_ASSERT_EQUAL(WK_STATE_IDLE, parser.state);
}

/* ============================================================================
 * Protocol Constants Tests
 * ============================================================================ */

void test_protocol_constants(void) {
    /* Verify command codes are in expected range */
    TEST_ASSERT_LESS_OR_EQUAL(0x1F, WK_CMD_ADMIN);
    TEST_ASSERT_LESS_OR_EQUAL(0x1F, WK_CMD_SIDETONE);
    TEST_ASSERT_LESS_OR_EQUAL(0x1F, WK_CMD_SPEED);
    TEST_ASSERT_LESS_OR_EQUAL(0x1F, WK_CMD_WEIGHT);
    TEST_ASSERT_LESS_OR_EQUAL(0x1F, WK_CMD_PTT_TIMING);
    TEST_ASSERT_LESS_OR_EQUAL(0x1F, WK_CMD_CLEAR_BUFFER);

    /* Verify status byte base */
    TEST_ASSERT_EQUAL(0xC0, WK_STATUS_BASE);

    /* Verify version */
    TEST_ASSERT_EQUAL(23, WK_VERSION);
}

/* ============================================================================
 * Null Safety Tests
 * ============================================================================ */

void test_parser_null_callbacks_safe(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);

    uint8_t response[8];
    size_t response_len = 0;

    /* Passing NULL callbacks should not crash */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, NULL, response, &response_len);
    winkeyer_parser_byte(&parser, WK_ADMIN_HOST_OPEN, NULL, response, &response_len);

    /* Parser should still track session state */
    TEST_ASSERT_TRUE(parser.session_open);
}

void test_parser_null_response_safe(void) {
    winkeyer_parser_t parser;
    winkeyer_parser_init(&parser);
    reset_test_state();

    /* Passing NULL response buffer should not crash */
    winkeyer_parser_byte(&parser, WK_CMD_ADMIN, &test_callbacks, NULL, NULL);
    winkeyer_parser_byte(&parser, WK_ADMIN_HOST_OPEN, &test_callbacks, NULL, NULL);

    /* Callback should still be called */
    TEST_ASSERT_TRUE(g_test_state.host_open_called);
}
