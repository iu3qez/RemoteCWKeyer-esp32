/**
 * @file test_completion.c
 * @brief Tests for tab completion (show-all approach)
 */

#include "unity.h"
#include "console.h"
#include <string.h>

/**
 * @brief Test completing unique command "help" from "hel"
 */
void test_complete_command_help(void) {
    char line[64] = "hel";
    size_t pos = 3;

    bool completed = console_complete(line, &pos, sizeof(line));
    TEST_ASSERT_TRUE(completed);
    TEST_ASSERT_EQUAL_STRING("help", line);
    TEST_ASSERT_EQUAL(4, pos);
}

/**
 * @brief Test completing unique parameter after "set "
 */
void test_complete_param_after_set(void) {
    char line[64] = "set wp";
    size_t pos = 6;

    bool completed = console_complete(line, &pos, sizeof(line));
    TEST_ASSERT_TRUE(completed);
    TEST_ASSERT_EQUAL_STRING("set wpm", line);
    TEST_ASSERT_EQUAL(7, pos);
}

/**
 * @brief Test completing parameter after "show " with multiple matches
 */
void test_complete_param_after_show(void) {
    char line[64] = "show side";
    size_t pos = 9;

    /* "side" matches sidetone_freq_hz and sidetone_vol - shows both, completes common prefix */
    bool completed = console_complete(line, &pos, sizeof(line));
    TEST_ASSERT_TRUE(completed);
    /* Common prefix is "sidetone_" */
    TEST_ASSERT_EQUAL_STRING("show sidetone_", line);
    TEST_ASSERT_EQUAL(14, pos);
}

/**
 * @brief Test no match for invalid prefix
 */
void test_complete_no_match(void) {
    char line[64] = "xyz";
    size_t pos = 3;

    bool completed = console_complete(line, &pos, sizeof(line));
    TEST_ASSERT_FALSE(completed);
}

/**
 * @brief Test multiple matches show all (returns true, prints options)
 */
void test_complete_multiple_matches(void) {
    char line[64] = "s";
    size_t pos = 1;

    /* Multiple commands start with 's' - should return true and show all */
    bool completed = console_complete(line, &pos, sizeof(line));
    TEST_ASSERT_TRUE(completed);
    /* Line unchanged since no common prefix beyond 's' */
    TEST_ASSERT_EQUAL_STRING("s", line);
}

/**
 * @brief Test completion reset (no-op in show-all mode)
 */
void test_complete_reset(void) {
    char line[64] = "hel";
    size_t pos = 3;

    bool completed = console_complete(line, &pos, sizeof(line));
    TEST_ASSERT_TRUE(completed);
    TEST_ASSERT_EQUAL_STRING("help", line);

    /* Reset should be no-op */
    console_complete_reset();

    /* Should still work */
    strcpy(line, "hel");
    pos = 3;
    completed = console_complete(line, &pos, sizeof(line));
    TEST_ASSERT_TRUE(completed);
    TEST_ASSERT_EQUAL_STRING("help", line);
}

/**
 * @brief Test completion with empty line
 */
void test_complete_empty_line(void) {
    char line[64] = "";
    size_t pos = 0;

    bool completed = console_complete(line, &pos, sizeof(line));
    TEST_ASSERT_FALSE(completed);
}

/**
 * @brief Test completion at buffer limit
 */
void test_complete_buffer_limit(void) {
    char line[16] = "h";
    size_t pos = 1;

    /* Completing "h" to "help" would fit */
    bool completed = console_complete(line, &pos, sizeof(line));
    TEST_ASSERT_TRUE(completed);
    TEST_ASSERT_EQUAL_STRING("help", line);

    /* Multiple matches in small buffer - shows all, can't complete common prefix */
    char small_line[8] = "set sid";
    pos = 7;
    completed = console_complete(small_line, &pos, sizeof(small_line));
    /* Shows options but can't fit common prefix "sidetone_" - still returns true */
    TEST_ASSERT_TRUE(completed);
    /* Line unchanged since common prefix won't fit */
    TEST_ASSERT_EQUAL_STRING("set sid", small_line);
}

/**
 * @brief Test debug command shows all options
 */
void test_complete_debug_shows_all(void) {
    char line[64] = "debug ";
    size_t pos = 6;

    /* Empty prefix after "debug " - should show all and return true */
    bool completed = console_complete(line, &pos, sizeof(line));
    TEST_ASSERT_TRUE(completed);
    /* Line unchanged since many matches with no common prefix */
    TEST_ASSERT_EQUAL_STRING("debug ", line);
}

/**
 * @brief Test debug with prefix filters options
 */
void test_complete_debug_with_prefix(void) {
    char line[64] = "debug inf";
    size_t pos = 9;

    /* "inf" matches only "info" - should complete */
    bool completed = console_complete(line, &pos, sizeof(line));
    TEST_ASSERT_TRUE(completed);
    TEST_ASSERT_EQUAL_STRING("debug info", line);
}

/**
 * @brief Test diag command completion
 */
void test_complete_diag(void) {
    char line[64] = "diag o";
    size_t pos = 6;

    /* "o" matches "on" and "off" - shows both, completes to "o" */
    bool completed = console_complete(line, &pos, sizeof(line));
    TEST_ASSERT_TRUE(completed);
    /* Common prefix is just "o" */
    TEST_ASSERT_EQUAL_STRING("diag o", line);
}
