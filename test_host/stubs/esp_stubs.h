/**
 * @file esp_stubs.h
 * @brief Stub definitions for ESP-IDF APIs during host testing
 */

#ifndef ESP_STUBS_H
#define ESP_STUBS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* esp_timer stub */
int64_t esp_timer_get_time(void);
void esp_timer_set_time(int64_t time_us);

/* esp_log stubs - map to printf for host */
#define ESP_LOGE(tag, fmt, ...) printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) printf("[D][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...) printf("[V][%s] " fmt "\n", tag, ##__VA_ARGS__)

/* IRAM_ATTR - no-op on host */
#define IRAM_ATTR

/* EXT_RAM_BSS_ATTR - no-op on host */
#define EXT_RAM_BSS_ATTR

#endif /* ESP_STUBS_H */
