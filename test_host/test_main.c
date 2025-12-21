/**
 * @file test_main.c
 * @brief Unity test runner for host tests
 */

#include "unity.h"

/* Declare test functions from other files */

/* test_console_parser.c */
void test_parse_empty_line(void);
void test_parse_simple_command(void);
void test_parse_command_with_one_arg(void);
void test_parse_command_with_two_args(void);
void test_parse_command_with_three_args(void);
void test_parse_extra_args_ignored(void);
void test_parse_leading_whitespace(void);
void test_parse_trailing_whitespace(void);
void test_parse_multiple_spaces(void);

/* test_console_commands.c */
void test_find_command_help(void);
void test_find_command_version(void);
void test_find_command_unknown(void);
void test_execute_empty_command(void);
void test_execute_unknown_command(void);
void test_execute_reboot_requires_confirm(void);
void test_execute_set_requires_args(void);
void test_command_registry_has_all_commands(void);

void setUp(void) {
    /* Called before each test */
}

void tearDown(void) {
    /* Called after each test */
}

int main(void) {
    UNITY_BEGIN();

    /* Parser tests */
    RUN_TEST(test_parse_empty_line);
    RUN_TEST(test_parse_simple_command);
    RUN_TEST(test_parse_command_with_one_arg);
    RUN_TEST(test_parse_command_with_two_args);
    RUN_TEST(test_parse_command_with_three_args);
    RUN_TEST(test_parse_extra_args_ignored);
    RUN_TEST(test_parse_leading_whitespace);
    RUN_TEST(test_parse_trailing_whitespace);
    RUN_TEST(test_parse_multiple_spaces);

    /* Command tests */
    RUN_TEST(test_find_command_help);
    RUN_TEST(test_find_command_version);
    RUN_TEST(test_find_command_unknown);
    RUN_TEST(test_execute_empty_command);
    RUN_TEST(test_execute_unknown_command);
    RUN_TEST(test_execute_reboot_requires_confirm);
    RUN_TEST(test_execute_set_requires_args);
    RUN_TEST(test_command_registry_has_all_commands);

    return UNITY_END();
}
