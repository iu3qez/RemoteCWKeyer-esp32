/**
 * @file api_keyer.c
 * @brief Text keyer HTTP API handlers
 */

#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include "text_keyer.h"
#include "text_memory.h"
#include <string.h>

static const char *TAG = "api_keyer";

static const char *state_to_string(text_keyer_state_t state) {
    switch (state) {
        case TEXT_KEYER_IDLE:    return "IDLE";
        case TEXT_KEYER_SENDING: return "SENDING";
        case TEXT_KEYER_PAUSED:  return "PAUSED";
        default:                 return "UNKNOWN";
    }
}

/* Helper to read POST body */
static int read_post_body(httpd_req_t *req, char *buf, size_t max_len) {
    size_t total_len = req->content_len;
    if (total_len == 0 || total_len >= max_len) {
        return -1;
    }

    size_t received = 0;
    while (received < total_len) {
        int ret = httpd_req_recv(req, buf + received, total_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return -1;
        }
        received += (size_t)ret;
    }
    buf[received] = '\0';
    return (int)received;
}

/* POST /api/text/send - Send arbitrary text */
esp_err_t api_text_send_handler(httpd_req_t *req) {
    char buf[256];
    if (read_post_body(req, buf, sizeof(buf)) < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
        return ESP_FAIL;
    }

    cJSON *json = cJSON_Parse(buf);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *text_obj = cJSON_GetObjectItem(json, "text");
    if (!cJSON_IsString(text_obj) || text_obj->valuestring == NULL) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'text' field");
        return ESP_FAIL;
    }

    int ret = text_keyer_send(text_obj->valuestring);
    cJSON_Delete(json);

    if (ret != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Keyer busy or invalid text");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Text send started");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
}

/* GET /api/text/status - Get keyer state and progress */
esp_err_t api_text_status_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON alloc failed");
        return ESP_FAIL;
    }

    text_keyer_state_t state = text_keyer_get_state();
    size_t sent = 0, total = 0;
    text_keyer_get_progress(&sent, &total);

    int progress = (total > 0) ? (int)((sent * 100) / total) : 0;

    cJSON_AddStringToObject(root, "state", state_to_string(state));
    cJSON_AddNumberToObject(root, "sent", (int)sent);
    cJSON_AddNumberToObject(root, "total", (int)total);
    cJSON_AddNumberToObject(root, "progress", progress);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON print failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json_str);
    return ret;
}

/* POST /api/text/abort - Abort transmission */
esp_err_t api_text_abort_handler(httpd_req_t *req) {
    text_keyer_abort();
    ESP_LOGI(TAG, "Text keyer aborted");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
}

/* POST /api/text/pause - Pause transmission */
esp_err_t api_text_pause_handler(httpd_req_t *req) {
    text_keyer_pause();
    ESP_LOGI(TAG, "Text keyer paused");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
}

/* POST /api/text/resume - Resume transmission */
esp_err_t api_text_resume_handler(httpd_req_t *req) {
    text_keyer_resume();
    ESP_LOGI(TAG, "Text keyer resumed");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
}

/* GET /api/text/memory - List all memory slots */
esp_err_t api_text_memory_list_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON alloc failed");
        return ESP_FAIL;
    }

    cJSON *slots = cJSON_CreateArray();
    if (slots == NULL) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON alloc failed");
        return ESP_FAIL;
    }

    for (uint8_t i = 0; i < TEXT_MEMORY_SLOTS; i++) {
        text_memory_slot_t slot_data;
        cJSON *slot_obj = cJSON_CreateObject();
        if (slot_obj == NULL) {
            continue;
        }

        cJSON_AddNumberToObject(slot_obj, "slot", i);

        if (text_memory_get(i, &slot_data) == 0) {
            cJSON_AddStringToObject(slot_obj, "text", slot_data.text);
            cJSON_AddStringToObject(slot_obj, "label", slot_data.label);
        } else {
            cJSON_AddStringToObject(slot_obj, "text", "");
            cJSON_AddStringToObject(slot_obj, "label", "");
        }

        cJSON_AddItemToArray(slots, slot_obj);
    }

    cJSON_AddItemToObject(root, "slots", slots);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON print failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json_str);
    return ret;
}

/* POST /api/text/memory - Set memory slot */
esp_err_t api_text_memory_set_handler(httpd_req_t *req) {
    char buf[256];
    if (read_post_body(req, buf, sizeof(buf)) < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
        return ESP_FAIL;
    }

    cJSON *json = cJSON_Parse(buf);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *slot_obj = cJSON_GetObjectItem(json, "slot");
    cJSON *text_obj = cJSON_GetObjectItem(json, "text");
    cJSON *label_obj = cJSON_GetObjectItem(json, "label");

    if (!cJSON_IsNumber(slot_obj)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'slot' field");
        return ESP_FAIL;
    }

    int slot = slot_obj->valueint;
    if (slot < 0 || slot >= TEXT_MEMORY_SLOTS) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid slot number");
        return ESP_FAIL;
    }

    const char *text = cJSON_IsString(text_obj) ? text_obj->valuestring : NULL;
    const char *label = cJSON_IsString(label_obj) ? label_obj->valuestring : NULL;

    int ret = text_memory_set((uint8_t)slot, text, label);
    if (ret == 0) {
        text_memory_save();
    }
    cJSON_Delete(json);

    if (ret != 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set slot");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Memory slot %d set", slot);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
}

/* POST /api/text/play - Play memory slot */
esp_err_t api_text_play_handler(httpd_req_t *req) {
    char buf[64];
    if (read_post_body(req, buf, sizeof(buf)) < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
        return ESP_FAIL;
    }

    cJSON *json = cJSON_Parse(buf);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *slot_obj = cJSON_GetObjectItem(json, "slot");
    if (!cJSON_IsNumber(slot_obj)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'slot' field");
        return ESP_FAIL;
    }

    int slot = slot_obj->valueint;
    cJSON_Delete(json);

    if (slot < 0 || slot >= TEXT_MEMORY_SLOTS) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid slot number");
        return ESP_FAIL;
    }

    text_memory_slot_t slot_data;
    if (text_memory_get((uint8_t)slot, &slot_data) != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Slot is empty");
        return ESP_FAIL;
    }

    int ret = text_keyer_send(slot_data.text);
    if (ret != 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Keyer busy");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Playing memory slot %d", slot);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
}
