#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include "decoder.h"

static const char *TAG = "api_decoder";

/* GET /api/decoder/status */
esp_err_t api_decoder_status_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON alloc failed");
        return ESP_FAIL;
    }

    cJSON_AddBoolToObject(root, "enabled", decoder_is_enabled());
    cJSON_AddNumberToObject(root, "wpm", (int)decoder_get_wpm());

    char pattern[16];
    decoder_get_current_pattern(pattern, sizeof(pattern));
    cJSON_AddStringToObject(root, "pattern", pattern);

    char text[128];
    decoder_get_text(text, sizeof(text));
    cJSON_AddStringToObject(root, "text", text);

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

/* POST /api/decoder/enable - body: {"enabled": true} */
esp_err_t api_decoder_enable_handler(httpd_req_t *req) {
    char buf[64];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *enabled = cJSON_GetObjectItem(json, "enabled");
    if (!cJSON_IsBool(enabled)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing enabled field");
        return ESP_FAIL;
    }

    bool enable = cJSON_IsTrue(enabled);
    cJSON_Delete(json);

    decoder_set_enabled(enable);
    ESP_LOGI(TAG, "Decoder %s", enable ? "enabled" : "disabled");

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
}
