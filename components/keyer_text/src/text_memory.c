/**
 * @file text_memory.c
 * @brief Memory slots implementation (NVS persistence)
 */

#include "text_memory.h"
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef CONFIG_IDF_TARGET
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
static const char *TAG = "text_mem";
#define NVS_NAMESPACE "text_mem"
#endif

/* ============================================================================
 * Module State
 * ============================================================================ */

static text_memory_slot_t s_slots[TEXT_MEMORY_SLOTS];
static bool s_initialized = false;

/* Default messages */
static const text_memory_slot_t DEFAULT_SLOTS[] = {
    { .text = "CQ CQ CQ DE IU3QEZ IU3QEZ K", .label = "CQ" },
    { .text = "73 TU DE IU3QEZ <SK>", .label = "73" },
    { .text = "UR RST 599 599", .label = "RST" },
    { .text = "QTH THIENE THIENE", .label = "QTH" },
    { .text = "", .label = "" },
    { .text = "", .label = "" },
    { .text = "", .label = "" },
    { .text = "", .label = "" },
};

/* ============================================================================
 * NVS Helpers
 * ============================================================================ */

#ifdef CONFIG_IDF_TARGET
static void load_from_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved slots, using defaults");
        return;
    }

    char key[16];
    for (uint8_t i = 0; i < TEXT_MEMORY_SLOTS; i++) {
        size_t len = TEXT_MEMORY_MAX_LEN;
        snprintf(key, sizeof(key), "slot%d_text", i);
        if (nvs_get_str(handle, key, s_slots[i].text, &len) != ESP_OK) {
            continue;  /* Keep default */
        }

        len = TEXT_MEMORY_LABEL_LEN;
        snprintf(key, sizeof(key), "slot%d_label", i);
        nvs_get_str(handle, key, s_slots[i].label, &len);
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Loaded slots from NVS");
}

static int save_to_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return -1;
    }

    char key[16];
    for (uint8_t i = 0; i < TEXT_MEMORY_SLOTS; i++) {
        snprintf(key, sizeof(key), "slot%d_text", i);
        nvs_set_str(handle, key, s_slots[i].text);

        snprintf(key, sizeof(key), "slot%d_label", i);
        nvs_set_str(handle, key, s_slots[i].label);
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        return -1;
    }

    ESP_LOGI(TAG, "Saved slots to NVS");
    return 0;
}
#else
/* Host stubs */
static void load_from_nvs(void) {}
static int save_to_nvs(void) { return 0; }
#endif

/* ============================================================================
 * Public API
 * ============================================================================ */

int text_memory_init(void) {
    /* Load defaults first */
    memcpy(s_slots, DEFAULT_SLOTS, sizeof(s_slots));

    /* Override with NVS if available */
    load_from_nvs();

    s_initialized = true;
    return 0;
}

int text_memory_get(uint8_t slot, text_memory_slot_t *out) {
    if (slot >= TEXT_MEMORY_SLOTS) {
        return -1;
    }

    if (!s_initialized) {
        text_memory_init();
    }

    if (s_slots[slot].text[0] == '\0') {
        return -1;  /* Empty */
    }

    if (out != NULL) {
        *out = s_slots[slot];
    }
    return 0;
}

int text_memory_set(uint8_t slot, const char *text, const char *label) {
    if (slot >= TEXT_MEMORY_SLOTS) {
        return -1;
    }

    if (!s_initialized) {
        text_memory_init();
    }

    if (text == NULL) {
        s_slots[slot].text[0] = '\0';
    } else {
        strncpy(s_slots[slot].text, text, TEXT_MEMORY_MAX_LEN - 1);
        s_slots[slot].text[TEXT_MEMORY_MAX_LEN - 1] = '\0';
    }

    if (label != NULL) {
        strncpy(s_slots[slot].label, label, TEXT_MEMORY_LABEL_LEN - 1);
        s_slots[slot].label[TEXT_MEMORY_LABEL_LEN - 1] = '\0';
    }

    return save_to_nvs();
}

int text_memory_clear(uint8_t slot) {
    return text_memory_set(slot, NULL, "");
}

int text_memory_set_label(uint8_t slot, const char *label) {
    if (slot >= TEXT_MEMORY_SLOTS || label == NULL) {
        return -1;
    }

    if (!s_initialized) {
        text_memory_init();
    }

    strncpy(s_slots[slot].label, label, TEXT_MEMORY_LABEL_LEN - 1);
    s_slots[slot].label[TEXT_MEMORY_LABEL_LEN - 1] = '\0';

    return save_to_nvs();
}

int text_memory_save(void) {
    return save_to_nvs();
}

bool text_memory_is_set(uint8_t slot) {
    if (slot >= TEXT_MEMORY_SLOTS || !s_initialized) {
        return false;
    }
    return s_slots[slot].text[0] != '\0';
}
