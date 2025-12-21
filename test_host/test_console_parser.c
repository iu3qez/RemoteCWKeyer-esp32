/**
 * @file test_console_parser.c
 * @brief Unit tests for console command parser
 */

#include "unity.h"
#include "console.h"
#include <string.h>

void test_parse_empty_line(void) {
    console_parsed_cmd_t cmd;
    console_parse_line("", &cmd);

    TEST_ASSERT_EQUAL_STRING("", cmd.command);
    TEST_ASSERT_EQUAL_INT(0, cmd.argc);
}

void test_parse_simple_command(void) {
    console_parsed_cmd_t cmd;
    console_parse_line("help", &cmd);

    TEST_ASSERT_EQUAL_STRING("help", cmd.command);
    TEST_ASSERT_EQUAL_INT(0, cmd.argc);
}

void test_parse_command_with_one_arg(void) {
    console_parsed_cmd_t cmd;
    console_parse_line("show wpm", &cmd);

    TEST_ASSERT_EQUAL_STRING("show", cmd.command);
    TEST_ASSERT_EQUAL_INT(1, cmd.argc);
    TEST_ASSERT_EQUAL_STRING("wpm", cmd.args[0]);
}

void test_parse_command_with_two_args(void) {
    console_parsed_cmd_t cmd;
    console_parse_line("set wpm 25", &cmd);

    TEST_ASSERT_EQUAL_STRING("set", cmd.command);
    TEST_ASSERT_EQUAL_INT(2, cmd.argc);
    TEST_ASSERT_EQUAL_STRING("wpm", cmd.args[0]);
    TEST_ASSERT_EQUAL_STRING("25", cmd.args[1]);
}

void test_parse_command_with_three_args(void) {
    console_parsed_cmd_t cmd;
    console_parse_line("debug wifi warn extra", &cmd);

    TEST_ASSERT_EQUAL_STRING("debug", cmd.command);
    TEST_ASSERT_EQUAL_INT(3, cmd.argc);
    TEST_ASSERT_EQUAL_STRING("wifi", cmd.args[0]);
    TEST_ASSERT_EQUAL_STRING("warn", cmd.args[1]);
    TEST_ASSERT_EQUAL_STRING("extra", cmd.args[2]);
}

void test_parse_extra_args_ignored(void) {
    console_parsed_cmd_t cmd;
    console_parse_line("cmd a b c d e f", &cmd);

    TEST_ASSERT_EQUAL_STRING("cmd", cmd.command);
    TEST_ASSERT_EQUAL_INT(3, cmd.argc); /* Max 3 args */
    TEST_ASSERT_EQUAL_STRING("a", cmd.args[0]);
    TEST_ASSERT_EQUAL_STRING("b", cmd.args[1]);
    TEST_ASSERT_EQUAL_STRING("c", cmd.args[2]);
}

void test_parse_leading_whitespace(void) {
    console_parsed_cmd_t cmd;
    console_parse_line("   help", &cmd);

    TEST_ASSERT_EQUAL_STRING("help", cmd.command);
    TEST_ASSERT_EQUAL_INT(0, cmd.argc);
}

void test_parse_trailing_whitespace(void) {
    console_parsed_cmd_t cmd;
    console_parse_line("help   ", &cmd);

    TEST_ASSERT_EQUAL_STRING("help", cmd.command);
    TEST_ASSERT_EQUAL_INT(0, cmd.argc);
}

void test_parse_multiple_spaces(void) {
    console_parsed_cmd_t cmd;
    console_parse_line("set   wpm   25", &cmd);

    TEST_ASSERT_EQUAL_STRING("set", cmd.command);
    TEST_ASSERT_EQUAL_INT(2, cmd.argc);
    TEST_ASSERT_EQUAL_STRING("wpm", cmd.args[0]);
    TEST_ASSERT_EQUAL_STRING("25", cmd.args[1]);
}
