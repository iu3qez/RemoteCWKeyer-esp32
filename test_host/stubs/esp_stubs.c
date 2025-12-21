/**
 * @file esp_stubs.c
 * @brief Stub implementations for ESP-IDF APIs during host testing
 */

#include "esp_stubs.h"

/* Simulated time for testing */
static int64_t s_simulated_time_us = 0;

int64_t esp_timer_get_time(void) {
    return s_simulated_time_us;
}

void esp_timer_set_time(int64_t time_us) {
    s_simulated_time_us = time_us;
}
