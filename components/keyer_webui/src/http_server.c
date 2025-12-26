#include "webui.h"
#include "assets.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "webui";
static httpd_handle_t s_server = NULL;

/* API handlers (implemented in api_*.c) */
extern esp_err_t api_config_schema_handler(httpd_req_t *req);
extern esp_err_t api_config_get_handler(httpd_req_t *req);
extern esp_err_t api_parameter_set_handler(httpd_req_t *req);
extern esp_err_t api_config_save_handler(httpd_req_t *req);
extern esp_err_t api_status_handler(httpd_req_t *req);
extern esp_err_t api_system_stats_handler(httpd_req_t *req);
extern esp_err_t api_system_reboot_handler(httpd_req_t *req);
extern esp_err_t api_decoder_status_handler(httpd_req_t *req);
extern esp_err_t api_decoder_enable_handler(httpd_req_t *req);

/* SPA routes that should serve index.html */
static const char *SPA_ROUTES[] = {
    "/",
    "/config",
    "/system",
    "/keyer",
    "/decoder",
    "/timeline",
    "/firmware",
    NULL
};

static bool is_spa_route(const char *uri) {
    for (int i = 0; SPA_ROUTES[i] != NULL; i++) {
        if (strcmp(uri, SPA_ROUTES[i]) == 0) {
            return true;
        }
    }
    return false;
}

static esp_err_t serve_asset(httpd_req_t *req, const webui_asset_t *asset) {
    httpd_resp_set_type(req, asset->content_type);
    if (asset->gzipped) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    return httpd_resp_send(req, (const char *)asset->data, (ssize_t)asset->length);
}

static esp_err_t handle_static(httpd_req_t *req) {
    const char *uri = req->uri;

    /* Try exact match first */
    const webui_asset_t *asset = webui_find_asset(uri);
    if (asset != NULL) {
        return serve_asset(req, asset);
    }

    /* SPA routes serve index.html */
    if (is_spa_route(uri)) {
        asset = webui_find_asset("/index.html");
        if (asset != NULL) {
            return serve_asset(req, asset);
        }
    }

    /* 404 */
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    return ESP_OK;
}

static void register_static_routes(httpd_handle_t server) {
    /* Register SPA routes */
    for (int i = 0; SPA_ROUTES[i] != NULL; i++) {
        httpd_uri_t uri = {
            .uri = SPA_ROUTES[i],
            .method = HTTP_GET,
            .handler = handle_static,
            .user_ctx = NULL,
        };
        httpd_register_uri_handler(server, &uri);
    }

    /* Register wildcard for /assets/ paths */
    httpd_uri_t assets_uri = {
        .uri = "/assets/*",
        .method = HTTP_GET,
        .handler = handle_static,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &assets_uri);
}

static void register_api_routes(httpd_handle_t server) {
    /* Config API */
    httpd_uri_t schema = {
        .uri = "/api/config/schema",
        .method = HTTP_GET,
        .handler = api_config_schema_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &schema);

    httpd_uri_t config_get = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = api_config_get_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &config_get);

    httpd_uri_t param_set = {
        .uri = "/api/parameter",
        .method = HTTP_POST,
        .handler = api_parameter_set_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &param_set);

    httpd_uri_t config_save = {
        .uri = "/api/config/save",
        .method = HTTP_POST,
        .handler = api_config_save_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &config_save);

    /* System API */
    httpd_uri_t status = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = api_status_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &status);

    httpd_uri_t stats = {
        .uri = "/api/system/stats",
        .method = HTTP_GET,
        .handler = api_system_stats_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &stats);

    httpd_uri_t reboot = {
        .uri = "/api/system/reboot",
        .method = HTTP_POST,
        .handler = api_system_reboot_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &reboot);

    /* Decoder API */
    httpd_uri_t decoder_status = {
        .uri = "/api/decoder/status",
        .method = HTTP_GET,
        .handler = api_decoder_status_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &decoder_status);

    httpd_uri_t decoder_enable = {
        .uri = "/api/decoder/enable",
        .method = HTTP_POST,
        .handler = api_decoder_enable_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &decoder_enable);
}

esp_err_t webui_init(void) {
    ESP_LOGI(TAG, "WebUI initialized (%zu assets)", webui_get_asset_count());
    return ESP_OK;
}

esp_err_t webui_start(void) {
    if (s_server != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 32;
    config.stack_size = 8192;
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    register_static_routes(s_server);
    register_api_routes(s_server);

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

/* SSE stubs - implemented in sse.c */
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
