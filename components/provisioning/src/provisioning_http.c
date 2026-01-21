/**
 * @file provisioning_http.c
 * @brief HTTP server for provisioning web UI
 *
 * Serves configuration form and handles form submission.
 */

#include <string.h>
#include <stdlib.h>
#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "prov_http";

/* External functions */
extern const char *prov_get_html_form(void);
extern const char *prov_get_html_success(void);
extern int prov_wifi_scan(char *json_buf, size_t buf_size);
extern esp_err_t prov_save_config(const char *ssid, const char *password,
                                   const char *callsign, uint8_t theme);
extern void prov_schedule_reboot(uint32_t delay_ms);

static httpd_handle_t s_server = NULL;

/* Buffer for scan results JSON */
#define SCAN_JSON_BUF_SIZE  2048
static char s_scan_json[SCAN_JSON_BUF_SIZE];

/* Buffer for POST data */
#define POST_BUF_SIZE  512

/**
 * @brief URL decode a string in place
 */
static void url_decode(char *str)
{
    char *src = str;
    char *dst = str;

    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && src[1] && src[2]) {
            char hex[3] = {src[1], src[2], '\0'};
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/**
 * @brief Extract form field value from POST data
 */
static bool get_form_field(const char *data, const char *field, char *value, size_t value_size)
{
    size_t field_len = strlen(field);
    const char *p = data;

    while ((p = strstr(p, field)) != NULL) {
        /* Check if this is the start of the field (either beginning or after &) */
        if (p != data && *(p - 1) != '&') {
            p++;
            continue;
        }

        /* Check for = after field name */
        if (p[field_len] != '=') {
            p++;
            continue;
        }

        /* Found the field - extract value */
        p += field_len + 1;
        const char *end = strchr(p, '&');
        size_t len = end ? (size_t)(end - p) : strlen(p);

        if (len >= value_size) {
            len = value_size - 1;
        }

        memcpy(value, p, len);
        value[len] = '\0';
        url_decode(value);
        return true;
    }

    value[0] = '\0';
    return false;
}

/**
 * @brief Validate callsign format
 */
static bool validate_callsign(const char *callsign)
{
    size_t len = strlen(callsign);
    if (len < 3 || len > 10) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        char c = callsign[i];
        if (!((c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
              c == '/')) {
            return false;
        }
    }

    return true;
}

/**
 * @brief Handler for GET / - serve HTML form
 */
static esp_err_t handle_root(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET /");

    const char *html = prov_get_html_form();
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Handler for GET /scan - return WiFi networks JSON
 */
static esp_err_t handle_scan(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET /scan");

    prov_wifi_scan(s_scan_json, sizeof(s_scan_json));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, s_scan_json, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Handler for POST /save - save configuration
 */
static esp_err_t handle_save(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /save");

    /* Read POST data */
    char post_data[POST_BUF_SIZE] = {0};
    int received = httpd_req_recv(req, post_data, sizeof(post_data) - 1);

    if (received <= 0) {
        ESP_LOGE(TAG, "Failed to receive POST data");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data received");
        return ESP_FAIL;
    }

    post_data[received] = '\0';
    ESP_LOGI(TAG, "POST data: %s", post_data);

    /* Extract form fields */
    char ssid[33] = {0};
    char password[65] = {0};
    char callsign[13] = {0};
    char theme_str[4] = {0};

    get_form_field(post_data, "ssid", ssid, sizeof(ssid));
    get_form_field(post_data, "password", password, sizeof(password));
    get_form_field(post_data, "callsign", callsign, sizeof(callsign));
    get_form_field(post_data, "theme", theme_str, sizeof(theme_str));

    uint8_t theme = (uint8_t)atoi(theme_str);

    /* Validate SSID */
    if (ssid[0] == '\0') {
        ESP_LOGE(TAG, "SSID is required");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID is required");
        return ESP_FAIL;
    }

    /* Validate password (>= 8 chars if not empty, for WPA2) */
    size_t pass_len = strlen(password);
    if (pass_len > 0 && pass_len < 8) {
        ESP_LOGE(TAG, "Password too short");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Password must be at least 8 characters");
        return ESP_FAIL;
    }

    /* Validate callsign */
    if (!validate_callsign(callsign)) {
        ESP_LOGE(TAG, "Invalid callsign: %s", callsign);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Callsign must be 3-10 alphanumeric characters");
        return ESP_FAIL;
    }

    /* Save configuration */
    esp_err_t err = prov_save_config(ssid, password, callsign, theme);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save config");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save configuration");
        return ESP_FAIL;
    }

    /* Send success page */
    const char *html = prov_get_html_success();
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);

    /* Schedule reboot after response sent */
    prov_schedule_reboot(3500);  /* 3.5s to allow page load + countdown */

    return ESP_OK;
}

void prov_http_start(void)
{
    if (s_server != NULL) {
        ESP_LOGW(TAG, "Server already running");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 4;
    config.stack_size = 8192;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return;
    }

    /* Register handlers */
    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = handle_root,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &root);

    httpd_uri_t scan = {
        .uri = "/scan",
        .method = HTTP_GET,
        .handler = handle_scan,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &scan);

    httpd_uri_t save = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = handle_save,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &save);

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
}

void prov_http_stop(void)
{
    if (s_server == NULL) {
        return;
    }

    httpd_stop(s_server);
    s_server = NULL;
    ESP_LOGI(TAG, "HTTP server stopped");
}
