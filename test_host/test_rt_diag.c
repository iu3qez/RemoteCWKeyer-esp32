/**
 * @file test_rt_diag.c
 * @brief Tests for RT diagnostic logging macros
 */

#include "unity.h"
#include "rt_log.h"

void test_diag_disabled_by_default(void) {
    TEST_ASSERT_FALSE(atomic_load(&g_rt_diag_enabled));
}

void test_diag_enable_disable(void) {
    /* Start disabled */
    atomic_store(&g_rt_diag_enabled, false);
    TEST_ASSERT_FALSE(atomic_load(&g_rt_diag_enabled));

    /* Enable */
    atomic_store(&g_rt_diag_enabled, true);
    TEST_ASSERT_TRUE(atomic_load(&g_rt_diag_enabled));

    /* Disable */
    atomic_store(&g_rt_diag_enabled, false);
    TEST_ASSERT_FALSE(atomic_load(&g_rt_diag_enabled));
}

void test_diag_macro_does_not_crash_when_disabled(void) {
    atomic_store(&g_rt_diag_enabled, false);

    /* These should not crash even with null stream when disabled */
    int64_t ts = 12345;
    RT_DIAG_INFO(&g_rt_log_stream, ts, "test %d", 42);
    RT_DIAG_DEBUG(&g_rt_log_stream, ts, "debug");
    RT_DIAG_WARN(&g_rt_log_stream, ts, "warn");

    /* If we got here, test passes */
    TEST_PASS();
}

void test_diag_macro_logs_when_enabled(void) {
    log_stream_init(&g_rt_log_stream);
    atomic_store(&g_rt_diag_enabled, true);

    int64_t ts = 99999;
    RT_DIAG_INFO(&g_rt_log_stream, ts, "hello %s", "world");

    /* Verify entry was logged */
    log_entry_t entry;
    TEST_ASSERT_TRUE(log_stream_drain(&g_rt_log_stream, &entry));
    TEST_ASSERT_EQUAL(LOG_LEVEL_INFO, entry.level);
    TEST_ASSERT_EQUAL(ts, entry.timestamp_us);
    TEST_ASSERT_EQUAL_STRING("hello world", entry.msg);

    /* Clean up */
    atomic_store(&g_rt_diag_enabled, false);
}
