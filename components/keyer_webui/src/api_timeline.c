#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include "config.h"

static const char *TAG __attribute__((unused)) = "api_timeline";

/* GET /api/timeline/config */
esp_err_t api_timeline_config_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON alloc failed");
        return ESP_FAIL;
    }

    uint16_t wpm = CONFIG_GET_WPM();
    cJSON_AddNumberToObject(root, "wpm", wpm);
    cJSON_AddStringToObject(root, "wpm_source", "keying_config");

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
