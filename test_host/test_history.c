/**
 * @file test_history.c
 * @brief Tests for command history
 */

#include "unity.h"
#include "console.h"
#include <string.h>

/**
 * @brief Test basic push and prev navigation
 */
void test_history_push_and_prev(void) {
    console_history_init();
    console_history_push("help");
    console_history_push("show");

    const char *line = console_history_prev();
    TEST_ASSERT_NOT_NULL(line);
    TEST_ASSERT_EQUAL_STRING("show", line);

    line = console_history_prev();
    TEST_ASSERT_NOT_NULL(line);
    TEST_ASSERT_EQUAL_STRING("help", line);

    /* Should stop at oldest */
    line = console_history_prev();
    TEST_ASSERT_NULL(line);
}

/**
 * @brief Test navigating forward with next
 */
void test_history_next(void) {
    console_history_init();
    console_history_push("cmd1");
    console_history_push("cmd2");

    /* Navigate backward */
    const char *line = console_history_prev();
    TEST_ASSERT_EQUAL_STRING("cmd2", line);
    line = console_history_prev();
    TEST_ASSERT_EQUAL_STRING("cmd1", line);

    /* Navigate forward */
    line = console_history_next();
    TEST_ASSERT_NOT_NULL(line);
    TEST_ASSERT_EQUAL_STRING("cmd2", line);

    /* Navigate past newest returns NULL */
    line = console_history_next();
    TEST_ASSERT_NULL(line);
}

/**
 * @brief Test ring buffer wrapping at HISTORY_SIZE
 */
void test_history_wrap_at_depth(void) {
    console_history_init();
    console_history_push("old1");
    console_history_push("old2");
    console_history_push("old3");
    console_history_push("old4");
    console_history_push("new");  /* Should overwrite old1 */

    /* Navigate back - should only find 4 entries (CONSOLE_HISTORY_SIZE) */
    const char *line = console_history_prev();
    TEST_ASSERT_NOT_NULL(line);
    TEST_ASSERT_EQUAL_STRING("new", line);

    line = console_history_prev();
    TEST_ASSERT_NOT_NULL(line);
    TEST_ASSERT_EQUAL_STRING("old4", line);

    line = console_history_prev();
    TEST_ASSERT_NOT_NULL(line);
    TEST_ASSERT_EQUAL_STRING("old3", line);

    line = console_history_prev();
    TEST_ASSERT_NOT_NULL(line);
    TEST_ASSERT_EQUAL_STRING("old2", line);

    /* Should stop here - old1 was overwritten */
    line = console_history_prev();
    TEST_ASSERT_NULL(line);
}

/**
 * @brief Test skipping duplicate consecutive entries
 */
void test_history_skip_duplicates(void) {
    console_history_init();
    console_history_push("help");
    console_history_push("help");  /* Should be skipped */
    console_history_push("show");

    const char *line = console_history_prev();
    TEST_ASSERT_EQUAL_STRING("show", line);

    line = console_history_prev();
    TEST_ASSERT_EQUAL_STRING("help", line);

    line = console_history_prev();
    TEST_ASSERT_NULL(line);
}

/**
 * @brief Test empty line is not added to history
 */
void test_history_skip_empty(void) {
    console_history_init();
    console_history_push("cmd1");
    console_history_push("");  /* Should be skipped */
    console_history_push(NULL);  /* Should be skipped */
    console_history_push("cmd2");

    const char *line = console_history_prev();
    TEST_ASSERT_EQUAL_STRING("cmd2", line);

    line = console_history_prev();
    TEST_ASSERT_EQUAL_STRING("cmd1", line);

    line = console_history_prev();
    TEST_ASSERT_NULL(line);
}

/**
 * @brief Test reset navigation
 */
void test_history_reset_nav(void) {
    console_history_init();
    console_history_push("cmd1");
    console_history_push("cmd2");

    /* Start navigating */
    console_history_prev();

    /* Reset navigation */
    console_history_reset_nav();

    /* Next call to prev should start from beginning */
    const char *line = console_history_prev();
    TEST_ASSERT_EQUAL_STRING("cmd2", line);
}

/**
 * @brief Test navigation on empty history
 */
void test_history_empty(void) {
    console_history_init();
    const char *line = console_history_prev();
    TEST_ASSERT_NULL(line);

    line = console_history_next();
    TEST_ASSERT_NULL(line);
}
