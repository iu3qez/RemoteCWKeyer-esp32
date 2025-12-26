#include "webui.h"
#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "webui";
static httpd_handle_t s_server = NULL;

esp_err_t webui_init(void) {
    ESP_LOGI(TAG, "WebUI initialized");
    return ESP_OK;
}

esp_err_t webui_start(void) {
    if (s_server != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 32;
    config.stack_size = 8192;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    return ESP_OK;
}

esp_err_t webui_stop(void) {
    if (s_server == NULL) {
        return ESP_OK;
    }

    httpd_stop(s_server);
    s_server = NULL;
    ESP_LOGI(TAG, "HTTP server stopped");
    return ESP_OK;
}

void webui_timeline_push(const char *event_type, const char *json_data) {
    (void)event_type;
    (void)json_data;
}

void webui_decoder_push_char(char c, uint8_t wpm) {
    (void)c;
    (void)wpm;
}

void webui_decoder_push_word(void) {
}
