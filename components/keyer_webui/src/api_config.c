#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include "config.h"
#include "config_console.h"
#include "config_nvs.h"
#include "config_schema.h"
#include <string.h>

static const char *TAG = "api_config";

/* GET /api/config/schema */
esp_err_t api_config_schema_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, CONFIG_SCHEMA_JSON, HTTPD_RESP_USE_STRLEN);
}

/* GET /api/config */
esp_err_t api_config_get_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON alloc failed");
        return ESP_FAIL;
    }

    /* Iterate all parameters and add to JSON */
    for (int i = 0; i < CONSOLE_PARAM_COUNT; i++) {
        const param_descriptor_t *p = &CONSOLE_PARAMS[i];
        param_value_t val = p->get_fn();

        /* Create nested structure: family.param */
        cJSON *family_obj = cJSON_GetObjectItem(root, p->family);
        if (family_obj == NULL) {
            family_obj = cJSON_CreateObject();
            cJSON_AddItemToObject(root, p->family, family_obj);
        }

        switch (p->type) {
            case PARAM_TYPE_BOOL:
                cJSON_AddBoolToObject(family_obj, p->name, val.b);
                break;
            case PARAM_TYPE_U8:
                cJSON_AddNumberToObject(family_obj, p->name, val.u8);
                break;
            case PARAM_TYPE_U16:
                cJSON_AddNumberToObject(family_obj, p->name, val.u16);
                break;
            case PARAM_TYPE_U32:
                cJSON_AddNumberToObject(family_obj, p->name, val.u32);
                break;
            case PARAM_TYPE_ENUM:
                cJSON_AddNumberToObject(family_obj, p->name, val.u8);
                break;
        }
    }

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

/* POST /api/parameter - body: {"param": "keyer.wpm", "value": 25} */
esp_err_t api_parameter_set_handler(httpd_req_t *req) {
    char buf[256];
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

    cJSON *param_item = cJSON_GetObjectItem(json, "param");
    cJSON *value_item = cJSON_GetObjectItem(json, "value");

    if (!cJSON_IsString(param_item) || value_item == NULL) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing param or value");
        return ESP_FAIL;
    }

    const char *param_name = param_item->valuestring;
    char value_str[64];

    if (cJSON_IsBool(value_item)) {
        snprintf(value_str, sizeof(value_str), "%s",
                 cJSON_IsTrue(value_item) ? "true" : "false");
    } else if (cJSON_IsNumber(value_item)) {
        snprintf(value_str, sizeof(value_str), "%d", value_item->valueint);
    } else if (cJSON_IsString(value_item)) {
        snprintf(value_str, sizeof(value_str), "%s", value_item->valuestring);
    } else {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid value type");
        return ESP_FAIL;
    }

    cJSON_Delete(json);

    int result = config_set_param_str(param_name, value_str);
    if (result != 0) {
        ESP_LOGW(TAG, "Failed to set %s = %s", param_name, value_str);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid parameter");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Set %s = %s", param_name, value_str);

    /* Return success response */
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"success\":true,\"requires_reset\":false}", HTTPD_RESP_USE_STRLEN);
}

/* POST /api/config/save */
esp_err_t api_config_save_handler(httpd_req_t *req) {
    /* Check for reboot query param */
    char query[32] = {0};
    bool do_reboot = false;

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[8];
        if (httpd_query_key_value(query, "reboot", param, sizeof(param)) == ESP_OK) {
            do_reboot = (strcmp(param, "true") == 0);
        }
    }

    int saved = config_save_to_nvs();
    if (saved < 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS save failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Saved %d parameters to NVS", saved);

    httpd_resp_set_type(req, "application/json");
    char response[64];
    snprintf(response, sizeof(response), "{\"success\":true,\"saved\":%d}", saved);
    esp_err_t ret = httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);

    if (do_reboot) {
        ESP_LOGI(TAG, "Rebooting in 500ms...");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }

    return ret;
}
