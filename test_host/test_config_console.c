/**
 * @file test_config_console.c
 * @brief Tests for config_console parameter registry
 */

#include "unity.h"
#include "config_console.h"
#include "config.h"

void test_config_find_param_wpm(void) {
    const param_descriptor_t *p = config_find_param("wpm");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("wpm", p->name);
    TEST_ASSERT_EQUAL_STRING("keyer", p->category);
    TEST_ASSERT_EQUAL(PARAM_TYPE_U16, p->type);
    TEST_ASSERT_EQUAL(5, p->min);
    TEST_ASSERT_EQUAL(100, p->max);
}

void test_config_find_param_unknown(void) {
    const param_descriptor_t *p = config_find_param("nonexistent");
    TEST_ASSERT_NULL(p);
}

void test_config_get_param_str_wpm(void) {
    config_init_defaults(&g_config);
    char buf[32];
    int ret = config_get_param_str("wpm", buf, sizeof(buf));
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL_STRING("25", buf);
}

void test_config_set_param_str_wpm(void) {
    config_init_defaults(&g_config);
    int ret = config_set_param_str("wpm", "30");
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(30, CONFIG_GET_WPM());
}

void test_config_set_param_str_out_of_range(void) {
    config_init_defaults(&g_config);
    int ret = config_set_param_str("wpm", "200");
    TEST_ASSERT_EQUAL(-4, ret);  /* CONSOLE_ERR_OUT_OF_RANGE */
}
