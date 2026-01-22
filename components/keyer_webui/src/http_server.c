#include "webui.h"
#include "assets.h"
#include "ws_server.h"
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
extern esp_err_t api_timeline_config_handler(httpd_req_t *req);
extern esp_err_t api_text_send_handler(httpd_req_t *req);
extern esp_err_t api_text_status_handler(httpd_req_t *req);
extern esp_err_t api_text_abort_handler(httpd_req_t *req);
extern esp_err_t api_text_pause_handler(httpd_req_t *req);
extern esp_err_t api_text_resume_handler(httpd_req_t *req);
extern esp_err_t api_text_memory_list_handler(httpd_req_t *req);
extern esp_err_t api_text_memory_set_handler(httpd_req_t *req);
extern esp_err_t api_text_play_handler(httpd_req_t *req);
extern esp_err_t api_vpn_status_handler(httpd_req_t *req);

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

    /* VPN API */
    httpd_uri_t vpn_status = {
        .uri = "/api/vpn/status",
        .method = HTTP_GET,
        .handler = api_vpn_status_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &vpn_status);

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

    /* Timeline API */
    httpd_uri_t timeline_config = {
        .uri = "/api/timeline/config",
        .method = HTTP_GET,
        .handler = api_timeline_config_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &timeline_config);

    /* WebSocket endpoint for real-time streaming */
    httpd_uri_t ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
        .is_websocket = true,
        .handle_ws_control_frames = true,
    };
    httpd_register_uri_handler(server, &ws);

    /* Text Keyer API */
    httpd_uri_t text_send = {
        .uri = "/api/text/send",
        .method = HTTP_POST,
        .handler = api_text_send_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &text_send);

    httpd_uri_t text_status = {
        .uri = "/api/text/status",
        .method = HTTP_GET,
        .handler = api_text_status_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &text_status);

    httpd_uri_t text_abort = {
        .uri = "/api/text/abort",
        .method = HTTP_POST,
        .handler = api_text_abort_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &text_abort);

    httpd_uri_t text_pause = {
        .uri = "/api/text/pause",
        .method = HTTP_POST,
        .handler = api_text_pause_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &text_pause);

    httpd_uri_t text_resume = {
        .uri = "/api/text/resume",
        .method = HTTP_POST,
        .handler = api_text_resume_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &text_resume);

    httpd_uri_t text_memory_list = {
        .uri = "/api/text/memory",
        .method = HTTP_GET,
        .handler = api_text_memory_list_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &text_memory_list);

    httpd_uri_t text_memory_set = {
        .uri = "/api/text/memory",
        .method = HTTP_POST,
        .handler = api_text_memory_set_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &text_memory_set);

    httpd_uri_t text_play = {
        .uri = "/api/text/play",
        .method = HTTP_POST,
        .handler = api_text_play_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &text_play);
}

esp_err_t webui_init(void) {
    ws_server_init();
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

    /* Set httpd handle for WebSocket session management */
    ws_server_set_httpd_handle(s_server);

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

/* WebSocket push functions */
void webui_timeline_push(const char *event_type, const char *json_data) {
    ws_broadcast_timeline(event_type, json_data);
}

void webui_decoder_push_char(char c, uint8_t wpm) {
    ESP_LOGI(TAG, "Push char '%c' wpm=%u clients=%d", c, wpm, ws_get_client_count());
    ws_broadcast_decoder_char(c, wpm);
}

void webui_decoder_push_word(void) {
    ws_broadcast_decoder_word();
}

void webui_decoder_push_pattern(const char *pattern) {
    ws_broadcast_decoder_pattern(pattern);
}

int webui_get_ws_client_count(void) {
    return ws_get_client_count();
}
