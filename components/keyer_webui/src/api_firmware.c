/**
 * @file api_firmware.c
 * @brief Firmware management HTTP API handlers (OTA, UF2, rollback)
 */

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "usb_uf2.h"
#include <assert.h>
#include <string.h>

static const char *TAG = "api_firmware";

/* OTA state machine */
typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_UPLOADING,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_DONE,
    OTA_STATE_ERROR,
} ota_state_t;

static struct {
    SemaphoreHandle_t mutex;
    ota_state_t       state;
    int               progress;       /* 0..100 */
    size_t            bytes_written;
    size_t            total_size;
    char              error[128];
    char              url[256];
} s_ota;

void api_firmware_init(void) {
    s_ota.mutex = xSemaphoreCreateMutex();
    assert(s_ota.mutex != NULL);
}

static void ota_set_error(const char *msg) {
    xSemaphoreTake(s_ota.mutex, portMAX_DELAY);
    s_ota.state = OTA_STATE_ERROR;
    strncpy(s_ota.error, msg, sizeof(s_ota.error) - 1);
    s_ota.error[sizeof(s_ota.error) - 1] = '\0';
    xSemaphoreGive(s_ota.mutex);
}

static const char *ota_state_to_string(ota_state_t state) {
    switch (state) {
        case OTA_STATE_IDLE:        return "IDLE";
        case OTA_STATE_UPLOADING:   return "UPLOADING";
        case OTA_STATE_DOWNLOADING: return "DOWNLOADING";
        case OTA_STATE_DONE:        return "DONE";
        case OTA_STATE_ERROR:       return "ERROR";
        default:                    return "UNKNOWN";
    }
}

/* GET /api/firmware/status */
esp_err_t api_firmware_status_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON alloc failed");
        return ESP_FAIL;
    }

    /* App description */
    const esp_app_desc_t *desc = esp_app_get_description();
    cJSON_AddStringToObject(root, "version", desc->version);
    cJSON_AddStringToObject(root, "date", desc->date);
    cJSON_AddStringToObject(root, "time", desc->time);
    cJSON_AddStringToObject(root, "idf_ver", desc->idf_ver);

    /* Partition info */
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running != NULL) {
        cJSON_AddStringToObject(root, "running_partition", running->label);
    }

    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (next != NULL) {
        cJSON_AddStringToObject(root, "next_partition", next->label);
    }

    /* Rollback info */
    cJSON_AddBoolToObject(root, "rollback_available",
                          esp_ota_check_rollback_is_possible());

    /* Check if running partition is pending verification */
    bool pending_verify = false;
    if (running != NULL) {
        esp_ota_img_states_t ota_state;
        if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
            pending_verify = (ota_state == ESP_OTA_IMG_PENDING_VERIFY);
        }
    }
    cJSON_AddBoolToObject(root, "pending_verify", pending_verify);

    /* OTA progress */
    xSemaphoreTake(s_ota.mutex, portMAX_DELAY);
    cJSON_AddStringToObject(root, "ota_state", ota_state_to_string(s_ota.state));
    cJSON_AddNumberToObject(root, "ota_progress", s_ota.progress);
    cJSON_AddNumberToObject(root, "ota_bytes_written", (double)s_ota.bytes_written);
    cJSON_AddNumberToObject(root, "ota_total_size", (double)s_ota.total_size);
    if (s_ota.state == OTA_STATE_ERROR) {
        cJSON_AddStringToObject(root, "ota_error", s_ota.error);
    }
    xSemaphoreGive(s_ota.mutex);

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

/* POST /api/firmware/upload — chunked binary OTA upload */
esp_err_t api_firmware_upload_handler(httpd_req_t *req) {
    if (req->content_len == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing Content-Length");
        return ESP_FAIL;
    }

    /* Reject if already busy */
    xSemaphoreTake(s_ota.mutex, portMAX_DELAY);
    if (s_ota.state == OTA_STATE_UPLOADING || s_ota.state == OTA_STATE_DOWNLOADING) {
        xSemaphoreGive(s_ota.mutex);
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "OTA already in progress", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    s_ota.state = OTA_STATE_UPLOADING;
    s_ota.progress = 0;
    s_ota.bytes_written = 0;
    s_ota.total_size = req->content_len;
    s_ota.error[0] = '\0';
    xSemaphoreGive(s_ota.mutex);

    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
    if (update == NULL) {
        ota_set_error("No OTA partition found");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        ota_set_error(esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    char buf[1024];
    size_t received = 0;
    size_t total = req->content_len;

    while (received < total) {
        size_t to_read = total - received;
        if (to_read > sizeof(buf)) {
            to_read = sizeof(buf);
        }

        int ret = httpd_req_recv(req, buf, to_read);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "Upload recv failed: %d", ret);
            esp_ota_abort(ota_handle);
            ota_set_error("Upload receive failed");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }

        err = esp_ota_write(ota_handle, buf, (size_t)ret);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            ota_set_error(esp_err_to_name(err));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return ESP_FAIL;
        }

        received += (size_t)ret;

        xSemaphoreTake(s_ota.mutex, portMAX_DELAY);
        s_ota.bytes_written = received;
        s_ota.progress = (total > 0) ? (int)((received * 100) / total) : 0;
        xSemaphoreGive(s_ota.mutex);
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        ota_set_error(esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA validation failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        ota_set_error(esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        return ESP_FAIL;
    }

    xSemaphoreTake(s_ota.mutex, portMAX_DELAY);
    s_ota.state = OTA_STATE_DONE;
    s_ota.progress = 100;
    xSemaphoreGive(s_ota.mutex);

    ESP_LOGI(TAG, "OTA upload complete, %zu bytes written to %s", received, update->label);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
}

/* Background task for URL-based OTA download */
static void ota_download_task(void *arg) {
    (void)arg;

    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
    if (update == NULL) {
        ota_set_error("No OTA partition found");
        vTaskDelete(NULL);
        return;
    }

    xSemaphoreTake(s_ota.mutex, portMAX_DELAY);
    char url[sizeof(s_ota.url)];
    strncpy(url, s_ota.url, sizeof(url));
    url[sizeof(url) - 1] = '\0';
    xSemaphoreGive(s_ota.mutex);

    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = 10000,
        .buffer_size = 1024,
        .skip_cert_common_name_check = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        ota_set_error("HTTP client init failed");
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        ota_set_error("HTTP connection failed");
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }

    int64_t content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "HTTP status %d", status);
        ota_set_error("HTTP download failed (non-200)");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }

    size_t total = (content_length > 0) ? (size_t)content_length : 0;

    xSemaphoreTake(s_ota.mutex, portMAX_DELAY);
    s_ota.total_size = total;
    xSemaphoreGive(s_ota.mutex);

    esp_ota_handle_t ota_handle;
    err = esp_ota_begin(update, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        ota_set_error(esp_err_to_name(err));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }

    char buf[1024];
    size_t received = 0;
    bool failed = false;
    int zero_count = 0;

    for (;;) {
        int read_len = esp_http_client_read(client, buf, (int)sizeof(buf));
        if (read_len < 0) {
            ESP_LOGE(TAG, "HTTP read failed");
            esp_ota_abort(ota_handle);
            ota_set_error("HTTP read failed");
            failed = true;
            break;
        }
        if (read_len == 0) {
            /* Check if we've received all data or stream ended */
            if (esp_http_client_is_complete_data_received(client)) {
                break;
            }
            zero_count++;
            if (zero_count > 100) {  /* 10s stall */
                ESP_LOGE(TAG, "Download stalled");
                esp_ota_abort(ota_handle);
                ota_set_error("Download stalled");
                failed = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        zero_count = 0;

        err = esp_ota_write(ota_handle, buf, (size_t)read_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            ota_set_error(esp_err_to_name(err));
            failed = true;
            break;
        }

        received += (size_t)read_len;

        xSemaphoreTake(s_ota.mutex, portMAX_DELAY);
        s_ota.bytes_written = received;
        s_ota.progress = (total > 0) ? (int)((received * 100) / total) : 0;
        xSemaphoreGive(s_ota.mutex);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (!failed) {
        err = esp_ota_end(ota_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
            ota_set_error(esp_err_to_name(err));
            vTaskDelete(NULL);
            return;
        }

        err = esp_ota_set_boot_partition(update);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
            ota_set_error(esp_err_to_name(err));
            vTaskDelete(NULL);
            return;
        }

        xSemaphoreTake(s_ota.mutex, portMAX_DELAY);
        s_ota.state = OTA_STATE_DONE;
        s_ota.progress = 100;
        xSemaphoreGive(s_ota.mutex);

        ESP_LOGI(TAG, "OTA download complete, %zu bytes from %s", received, url);
    }

    vTaskDelete(NULL);
}

/* Helper to read POST body (same pattern as api_keyer.c) */
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

/* POST /api/firmware/url — start OTA download from URL */
esp_err_t api_firmware_url_handler(httpd_req_t *req) {
    char body[320];
    if (read_post_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
        return ESP_FAIL;
    }

    cJSON *json = cJSON_Parse(body);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *url_obj = cJSON_GetObjectItem(json, "url");
    if (!cJSON_IsString(url_obj) || url_obj->valuestring == NULL) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'url' field");
        return ESP_FAIL;
    }

    if (strlen(url_obj->valuestring) >= sizeof(s_ota.url)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "URL too long");
        return ESP_FAIL;
    }
    if (strncmp(url_obj->valuestring, "http://", 7) != 0 &&
        strncmp(url_obj->valuestring, "https://", 8) != 0) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "URL must use http:// or https://");
        return ESP_FAIL;
    }

    /* Reject if already busy */
    xSemaphoreTake(s_ota.mutex, portMAX_DELAY);
    if (s_ota.state == OTA_STATE_UPLOADING || s_ota.state == OTA_STATE_DOWNLOADING) {
        xSemaphoreGive(s_ota.mutex);
        cJSON_Delete(json);
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "OTA already in progress", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    s_ota.state = OTA_STATE_DOWNLOADING;
    s_ota.progress = 0;
    s_ota.bytes_written = 0;
    s_ota.total_size = 0;
    s_ota.error[0] = '\0';
    strncpy(s_ota.url, url_obj->valuestring, sizeof(s_ota.url) - 1);
    s_ota.url[sizeof(s_ota.url) - 1] = '\0';
    xSemaphoreGive(s_ota.mutex);

    cJSON_Delete(json);

    /* Spawn download task on Core 1 */
    BaseType_t ret = xTaskCreatePinnedToCore(
        ota_download_task, "ota_dl", 8192, NULL,
        tskIDLE_PRIORITY + 3, NULL, 1);

    if (ret != pdPASS) {
        ota_set_error("Failed to create download task");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Task creation failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA download started from URL");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
}

/* POST /api/firmware/rollback */
esp_err_t api_firmware_rollback_handler(httpd_req_t *req) {
    if (!esp_ota_check_rollback_is_possible()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Rollback not available");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Rollback requested");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);

    esp_ota_mark_app_invalid_rollback_and_reboot();

    return ESP_OK;  /* Never reached */
}

/* POST /api/firmware/uf2 */
esp_err_t api_firmware_uf2_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "UF2 mode requested");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);

    vTaskDelay(pdMS_TO_TICKS(500));
    usb_uf2_enter();

    return ESP_OK;  /* Never reached */
}

/* POST /api/firmware/confirm */
esp_err_t api_firmware_confirm_handler(httpd_req_t *req) {
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to confirm app: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Confirm failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "App confirmed as valid");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
}
