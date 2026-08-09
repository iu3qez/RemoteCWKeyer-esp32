# Firmware Update Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add OTA firmware update (upload + URL download), UF2 reboot, and automatic rollback to the Web UI.

**Architecture:** New `api_firmware.c` in `keyer_webui/src/` following the existing `api_*.c` handler pattern. OTA state protected by FreeRTOS mutex. Frontend `Firmware.svelte` page with polling-based progress. Rollback confirmation in `main.c` based on network stack activity.

**Tech Stack:** ESP-IDF `esp_ota_ops`, `esp_http_client`, Svelte 5 with TypeScript, cJSON

**Design doc:** `docs/plans/2026-03-03-firmware-update-design.md`

---

### Task 1: Add `ota_confirm_timeout_s` parameter to `parameters.yaml`

**Files:**
- Modify: `parameters.yaml:506-600` (system family)

**Step 1: Add the parameter**

In `parameters.yaml`, after the `ui_theme` parameter (line 600), add:

```yaml
      ota_confirm_timeout_s:
        type: u16
        default: 60
        range: [10, 300]
        nvs_key: "ota_timeout"
        runtime_change: reboot
        priority: 33
        gui:
          label_short:
            en: "OTA Timeout"
            it: "Timeout OTA"
          label_long:
            en: "OTA Confirm Timeout"
            it: "Timeout Conferma OTA"
          description:
            en: "Seconds to wait for network before rolling back OTA update"
            it: "Secondi di attesa per la rete prima di annullare aggiornamento OTA"
          widget: slider
          widget_config:
            step: 5
            unit: "s"
          advanced: true
```

**Step 2: Regenerate config code**

Run: `python scripts/gen_config_c.py parameters.yaml components/keyer_config`
Expected: Generated files updated with new `ota_confirm_timeout_s` field in system family struct

**Step 3: Verify build**

Run: `source /home/sf/esp/esp-idf/export.sh && idf.py build`
Expected: Build succeeds with new config parameter accessible via `CONFIG_GET_OTA_CONFIRM_TIMEOUT_S()`

**Step 4: Commit**

```
feat(config): add ota_confirm_timeout_s parameter
```

---

### Task 2: Add ESP-IDF dependencies to `keyer_webui`

**Files:**
- Modify: `components/keyer_webui/CMakeLists.txt:14-23`
- Modify: `sdkconfig.defaults`

**Step 1: Add REQUIRES to CMakeLists.txt**

Add to the REQUIRES list (after `keyer_text`, line 23):

```cmake
        esp_ota_ops
        esp_http_client
        app_update
        keyer_usb
```

`keyer_usb` is needed to call `usb_uf2_enter()`.

**Step 2: Add sdkconfig defaults**

Append to `sdkconfig.defaults`:

```
# OTA: allow HTTP (not just HTTPS) for local/dev firmware servers
CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y
```

**Step 3: Verify build**

Run: `idf.py build`
Expected: Build succeeds (no new source files yet, just dependency wiring)

**Step 4: Commit**

```
build(webui): add OTA and USB dependencies for firmware update
```

---

### Task 3: Create `api_firmware.c` — status and UF2 handlers

**Files:**
- Create: `components/keyer_webui/src/api_firmware.c`
- Modify: `components/keyer_webui/CMakeLists.txt:2-11` (add to SRCS)
- Modify: `components/keyer_webui/src/http_server.c:105-274` (register routes)

**Step 1: Create api_firmware.c with state struct and status handler**

Create `components/keyer_webui/src/api_firmware.c`:

```c
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_http_client.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "usb_uf2.h"
#include "keyer_config.h"

#include <string.h>

static const char *TAG = "api_firmware";

typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_UPLOADING,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_DONE,
    OTA_STATE_ERROR,
} ota_state_t;

static const char *ota_state_str(ota_state_t s) {
    switch (s) {
        case OTA_STATE_IDLE:        return "IDLE";
        case OTA_STATE_UPLOADING:   return "UPLOADING";
        case OTA_STATE_DOWNLOADING: return "DOWNLOADING";
        case OTA_STATE_DONE:        return "DONE";
        case OTA_STATE_ERROR:       return "ERROR";
        default:                    return "UNKNOWN";
    }
}

static struct {
    SemaphoreHandle_t       mutex;
    ota_state_t             state;
    int                     progress;
    size_t                  bytes_written;
    size_t                  total_size;
    char                    error_msg[128];
    esp_ota_handle_t        ota_handle;
    const esp_partition_t  *target_partition;
} s_ota;

static void ota_init_once(void) {
    if (s_ota.mutex == NULL) {
        s_ota.mutex = xSemaphoreCreateMutex();
        assert(s_ota.mutex != NULL);
    }
}

static void ota_set_error(const char *msg) {
    xSemaphoreTake(s_ota.mutex, portMAX_DELAY);
    s_ota.state = OTA_STATE_ERROR;
    strncpy(s_ota.error_msg, msg, sizeof(s_ota.error_msg) - 1);
    s_ota.error_msg[sizeof(s_ota.error_msg) - 1] = '\0';
    xSemaphoreGive(s_ota.mutex);
}

/* GET /api/firmware/status */
esp_err_t api_firmware_status_handler(httpd_req_t *req) {
    ota_init_once();

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next    = esp_ota_get_next_update_partition(NULL);
    const esp_app_desc_t  *app     = esp_app_get_description();

    /* Check if rollback is possible */
    bool rollback_available = esp_ota_check_rollback_is_possible();

    /* Check if we're pending verification */
    esp_ota_img_states_t ota_state;
    bool pending_verify = false;
    if (esp_ota_get_state_info(running, &ota_state) == ESP_OK) {
        pending_verify = (ota_state == ESP_OTA_IMG_PENDING_VERIFY);
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON alloc failed");
        return ESP_FAIL;
    }

    cJSON_AddStringToObject(root, "version", app->version);
    cJSON_AddStringToObject(root, "date", app->date);
    cJSON_AddStringToObject(root, "time", app->time);
    cJSON_AddStringToObject(root, "idf_ver", app->idf_ver);
    cJSON_AddStringToObject(root, "running_partition", running ? running->label : "?");
    cJSON_AddStringToObject(root, "next_partition", next ? next->label : "?");
    cJSON_AddBoolToObject(root, "rollback_available", rollback_available);
    cJSON_AddBoolToObject(root, "pending_verify", pending_verify);

    xSemaphoreTake(s_ota.mutex, portMAX_DELAY);
    cJSON_AddStringToObject(root, "ota_state", ota_state_str(s_ota.state));
    cJSON_AddNumberToObject(root, "ota_progress", s_ota.progress);
    cJSON_AddNumberToObject(root, "ota_bytes_written", (double)s_ota.bytes_written);
    cJSON_AddNumberToObject(root, "ota_total_size", (double)s_ota.total_size);
    if (s_ota.state == OTA_STATE_ERROR) {
        cJSON_AddStringToObject(root, "ota_error", s_ota.error_msg);
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

/* POST /api/firmware/uf2 */
esp_err_t api_firmware_uf2_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "UF2 reboot requested");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);

    vTaskDelay(pdMS_TO_TICKS(500));
    usb_uf2_enter();  /* Does not return */

    return ESP_OK;
}

/* POST /api/firmware/rollback */
esp_err_t api_firmware_rollback_handler(httpd_req_t *req) {
    if (!esp_ota_check_rollback_is_possible()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Rollback not available");
        return ESP_FAIL;
    }

    ESP_LOGW(TAG, "Rollback requested");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_ota_mark_app_invalid_rollback_and_reboot();

    return ESP_OK;  /* Never reached */
}

/* POST /api/firmware/confirm */
esp_err_t api_firmware_confirm_handler(httpd_req_t *req) {
    esp_err_t err = esp_ota_mark_app_valid_and_cancel_rollback();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Confirm failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA confirmed manually");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
```

**Step 2: Add to CMakeLists.txt SRCS**

Add `"src/api_firmware.c"` to the SRCS list after `"src/api_vpn.c"` (line 10).

**Step 3: Declare handlers in http_server.c and register routes**

In `http_server.c`, add extern declarations near the top (after existing extern declarations, before `register_api_routes`):

```c
/* Firmware API (api_firmware.c) */
extern esp_err_t api_firmware_status_handler(httpd_req_t *req);
extern esp_err_t api_firmware_upload_handler(httpd_req_t *req);
extern esp_err_t api_firmware_url_handler(httpd_req_t *req);
extern esp_err_t api_firmware_rollback_handler(httpd_req_t *req);
extern esp_err_t api_firmware_uf2_handler(httpd_req_t *req);
extern esp_err_t api_firmware_confirm_handler(httpd_req_t *req);
```

In `register_api_routes()`, before the closing `}`, add:

```c
    /* Firmware API */
    httpd_uri_t fw_status = {
        .uri = "/api/firmware/status",
        .method = HTTP_GET,
        .handler = api_firmware_status_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &fw_status);

    httpd_uri_t fw_upload = {
        .uri = "/api/firmware/upload",
        .method = HTTP_POST,
        .handler = api_firmware_upload_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &fw_upload);

    httpd_uri_t fw_url = {
        .uri = "/api/firmware/url",
        .method = HTTP_POST,
        .handler = api_firmware_url_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &fw_url);

    httpd_uri_t fw_rollback = {
        .uri = "/api/firmware/rollback",
        .method = HTTP_POST,
        .handler = api_firmware_rollback_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &fw_rollback);

    httpd_uri_t fw_uf2 = {
        .uri = "/api/firmware/uf2",
        .method = HTTP_POST,
        .handler = api_firmware_uf2_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &fw_uf2);

    httpd_uri_t fw_confirm = {
        .uri = "/api/firmware/confirm",
        .method = HTTP_POST,
        .handler = api_firmware_confirm_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &fw_confirm);
```

**Step 4: Verify build**

Run: `idf.py build`
Expected: Linker error for `api_firmware_upload_handler` and `api_firmware_url_handler` (not yet implemented). Add stubs:

```c
/* Stubs — implemented in Task 4 */
esp_err_t api_firmware_upload_handler(httpd_req_t *req) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Not implemented");
    return ESP_FAIL;
}

esp_err_t api_firmware_url_handler(httpd_req_t *req) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Not implemented");
    return ESP_FAIL;
}
```

Run: `idf.py build`
Expected: Build succeeds

**Step 5: Commit**

```
feat(webui): add firmware status, rollback, UF2, and confirm API handlers
```

---

### Task 4: Implement OTA upload handler

**Files:**
- Modify: `components/keyer_webui/src/api_firmware.c`

**Step 1: Replace upload stub with chunked OTA write**

Replace the `api_firmware_upload_handler` stub with:

```c
/* POST /api/firmware/upload — chunked binary upload */
esp_err_t api_firmware_upload_handler(httpd_req_t *req) {
    ota_init_once();

    /* Check not already in progress */
    xSemaphoreTake(s_ota.mutex, portMAX_DELAY);
    if (s_ota.state == OTA_STATE_UPLOADING || s_ota.state == OTA_STATE_DOWNLOADING) {
        xSemaphoreGive(s_ota.mutex);
        httpd_resp_send_err(req, HTTPD_409_CONFLICT, "OTA already in progress");
        return ESP_FAIL;
    }
    s_ota.state = OTA_STATE_UPLOADING;
    s_ota.progress = 0;
    s_ota.bytes_written = 0;
    s_ota.total_size = (size_t)req->content_len;
    s_ota.error_msg[0] = '\0';
    xSemaphoreGive(s_ota.mutex);

    ESP_LOGI(TAG, "OTA upload started, size=%zu", s_ota.total_size);

    /* Find target partition */
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (target == NULL) {
        ota_set_error("No OTA partition found");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }

    esp_ota_handle_t handle;
    esp_err_t err = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        ota_set_error(esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    xSemaphoreTake(s_ota.mutex, portMAX_DELAY);
    s_ota.ota_handle = handle;
    s_ota.target_partition = target;
    xSemaphoreGive(s_ota.mutex);

    /* Receive and write chunks */
    char buf[1024];
    size_t remaining = (size_t)req->content_len;

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, sizeof(buf) < remaining ? sizeof(buf) : remaining);
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;  /* Retry on timeout */
            }
            ESP_LOGE(TAG, "Upload recv error: %d", recv_len);
            esp_ota_abort(handle);
            ota_set_error("Upload connection lost");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload failed");
            return ESP_FAIL;
        }

        err = esp_ota_write(handle, buf, (size_t)recv_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(handle);
            ota_set_error(esp_err_to_name(err));
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return ESP_FAIL;
        }

        remaining -= (size_t)recv_len;

        xSemaphoreTake(s_ota.mutex, portMAX_DELAY);
        s_ota.bytes_written += (size_t)recv_len;
        if (s_ota.total_size > 0) {
            s_ota.progress = (int)(s_ota.bytes_written * 100 / s_ota.total_size);
        }
        xSemaphoreGive(s_ota.mutex);
    }

    /* Finalize */
    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        ota_set_error(esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA validation failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(target);
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

    ESP_LOGI(TAG, "OTA upload complete, partition=%s", target->label);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
```

**Step 2: Verify build**

Run: `idf.py build`
Expected: Build succeeds

**Step 3: Commit**

```
feat(webui): implement OTA upload handler with chunked write
```

---

### Task 5: Implement OTA URL download handler

**Files:**
- Modify: `components/keyer_webui/src/api_firmware.c`

**Step 1: Add download task and URL handler**

Replace the `api_firmware_url_handler` stub. Add a static task function and the handler:

```c
/* URL to download — stored for the task */
static char s_download_url[512];

static void ota_download_task(void *arg) {
    (void)arg;

    ESP_LOGI(TAG, "OTA download from: %s", s_download_url);

    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (target == NULL) {
        ota_set_error("No OTA partition found");
        vTaskDelete(NULL);
        return;
    }

    esp_ota_handle_t handle;
    esp_err_t err = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (err != ESP_OK) {
        ota_set_error(esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    xSemaphoreTake(s_ota.mutex, portMAX_DELAY);
    s_ota.ota_handle = handle;
    s_ota.target_partition = target;
    xSemaphoreGive(s_ota.mutex);

    esp_http_client_config_t http_cfg = {
        .url = s_download_url,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (client == NULL) {
        esp_ota_abort(handle);
        ota_set_error("HTTP client init failed");
        vTaskDelete(NULL);
        return;
    }

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        esp_ota_abort(handle);
        ota_set_error(esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    int content_length = esp_http_client_fetch_headers(client);

    xSemaphoreTake(s_ota.mutex, portMAX_DELAY);
    s_ota.total_size = content_length > 0 ? (size_t)content_length : 0;
    xSemaphoreGive(s_ota.mutex);

    char buf[1024];
    bool success = true;

    while (true) {
        int read_len = esp_http_client_read(client, buf, sizeof(buf));
        if (read_len < 0) {
            ESP_LOGE(TAG, "HTTP read error");
            esp_ota_abort(handle);
            ota_set_error("Download read error");
            success = false;
            break;
        }
        if (read_len == 0) {
            /* Check if connection closed or transfer complete */
            if (esp_http_client_is_complete_data_received(client)) {
                break;
            }
            /* Timeout or incomplete — retry briefly */
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        err = esp_ota_write(handle, buf, (size_t)read_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA write failed: %s", esp_err_to_name(err));
            esp_ota_abort(handle);
            ota_set_error(esp_err_to_name(err));
            success = false;
            break;
        }

        xSemaphoreTake(s_ota.mutex, portMAX_DELAY);
        s_ota.bytes_written += (size_t)read_len;
        if (s_ota.total_size > 0) {
            s_ota.progress = (int)(s_ota.bytes_written * 100 / s_ota.total_size);
        }
        xSemaphoreGive(s_ota.mutex);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (success) {
        err = esp_ota_end(handle);
        if (err != ESP_OK) {
            ota_set_error(esp_err_to_name(err));
            vTaskDelete(NULL);
            return;
        }

        err = esp_ota_set_boot_partition(target);
        if (err != ESP_OK) {
            ota_set_error(esp_err_to_name(err));
            vTaskDelete(NULL);
            return;
        }

        xSemaphoreTake(s_ota.mutex, portMAX_DELAY);
        s_ota.state = OTA_STATE_DONE;
        s_ota.progress = 100;
        xSemaphoreGive(s_ota.mutex);

        ESP_LOGI(TAG, "OTA download complete, partition=%s", target->label);
    }

    vTaskDelete(NULL);
}

/* POST /api/firmware/url — download from remote URL */
esp_err_t api_firmware_url_handler(httpd_req_t *req) {
    ota_init_once();

    /* Check not already in progress */
    xSemaphoreTake(s_ota.mutex, portMAX_DELAY);
    if (s_ota.state == OTA_STATE_UPLOADING || s_ota.state == OTA_STATE_DOWNLOADING) {
        xSemaphoreGive(s_ota.mutex);
        httpd_resp_send_err(req, HTTPD_409_CONFLICT, "OTA already in progress");
        return ESP_FAIL;
    }
    xSemaphoreGive(s_ota.mutex);

    /* Read JSON body */
    char body[600];
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    body[len] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    const cJSON *url_item = cJSON_GetObjectItem(root, "url");
    if (!cJSON_IsString(url_item) || url_item->valuestring[0] == '\0') {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'url' field");
        return ESP_FAIL;
    }

    strncpy(s_download_url, url_item->valuestring, sizeof(s_download_url) - 1);
    s_download_url[sizeof(s_download_url) - 1] = '\0';
    cJSON_Delete(root);

    /* Set state and spawn task */
    xSemaphoreTake(s_ota.mutex, portMAX_DELAY);
    s_ota.state = OTA_STATE_DOWNLOADING;
    s_ota.progress = 0;
    s_ota.bytes_written = 0;
    s_ota.total_size = 0;
    s_ota.error_msg[0] = '\0';
    xSemaphoreGive(s_ota.mutex);

    BaseType_t ret = xTaskCreatePinnedToCore(
        ota_download_task,
        "ota_dl",
        8192,
        NULL,
        tskIDLE_PRIORITY + 3,
        NULL,
        1  /* Core 1 */
    );

    if (ret != pdPASS) {
        ota_set_error("Failed to create download task");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Task creation failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA download started: %s", s_download_url);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
```

**Step 2: Verify build**

Run: `idf.py build`
Expected: Build succeeds

**Step 3: Commit**

```
feat(webui): implement OTA download from URL with background task
```

---

### Task 6: Add OTA rollback confirmation to `main.c`

**Files:**
- Modify: `main/main.c:64-304`

**Step 1: Add OTA confirmation logic after WiFi init**

Add includes at the top of `main.c` (if not already present):

```c
#include "esp_ota_ops.h"
#include "wifi.h"
```

After the WiFi initialization block (around line 200, after `wifi_app_start()` call and the else blocks), add:

```c
    /* ===== OTA ROLLBACK CHECK ===== */
    {
        const esp_partition_t *running = esp_ota_get_running_partition();
        esp_ota_img_states_t ota_state;
        if (esp_ota_get_state_info(running, &ota_state) == ESP_OK &&
            ota_state == ESP_OTA_IMG_PENDING_VERIFY) {

            uint16_t timeout_s = CONFIG_GET_OTA_CONFIRM_TIMEOUT_S();
            ESP_LOGW(TAG, "OTA pending verify — waiting %us for network", timeout_s);

            bool confirmed = false;
            for (uint16_t i = 0; i < timeout_s; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                wifi_state_t ws = wifi_get_state();
                if (ws == WIFI_STATE_CONNECTED || ws == WIFI_STATE_AP_MODE) {
                    esp_ota_mark_app_valid_and_cancel_rollback();
                    ESP_LOGI(TAG, "OTA confirmed — network active (%s)",
                             ws == WIFI_STATE_CONNECTED ? "STA" : "AP");
                    confirmed = true;
                    break;
                }
            }

            if (!confirmed) {
                ESP_LOGE(TAG, "OTA not confirmed after %us — rebooting for rollback", timeout_s);
                esp_restart();
            }
        }
    }
```

**Step 2: Verify build**

Run: `idf.py build`
Expected: Build succeeds

**Step 3: Commit**

```
feat(main): add OTA rollback confirmation based on network activity
```

---

### Task 7: Add TypeScript types and API methods for firmware

**Files:**
- Modify: `components/keyer_webui/frontend/src/lib/types.ts`
- Modify: `components/keyer_webui/frontend/src/lib/api.ts`

**Step 1: Add FirmwareStatus type to types.ts**

Append to `types.ts`:

```typescript
export interface FirmwareStatus {
  version: string;
  date: string;
  time: string;
  idf_ver: string;
  running_partition: string;
  next_partition: string;
  rollback_available: boolean;
  pending_verify: boolean;
  ota_state: 'IDLE' | 'UPLOADING' | 'DOWNLOADING' | 'DONE' | 'ERROR';
  ota_progress: number;
  ota_bytes_written: number;
  ota_total_size: number;
  ota_error?: string;
}
```

**Step 2: Add firmware API methods to api.ts**

Add to the `ApiClient` class in `api.ts`:

```typescript
  // Firmware
  async getFirmwareStatus(): Promise<FirmwareStatus> {
    return this.fetchJson('/api/firmware/status');
  }

  async uploadFirmware(file: File, onProgress?: (pct: number) => void): Promise<void> {
    const xhr = new XMLHttpRequest();
    return new Promise((resolve, reject) => {
      xhr.open('POST', `${this.baseUrl}/api/firmware/upload`);
      xhr.setRequestHeader('Content-Type', 'application/octet-stream');

      xhr.upload.onprogress = (e) => {
        if (e.lengthComputable && onProgress) {
          onProgress(Math.round(e.loaded / e.total * 100));
        }
      };

      xhr.onload = () => {
        if (xhr.status >= 200 && xhr.status < 300) {
          resolve();
        } else {
          reject(new Error(`Upload failed: ${xhr.statusText}`));
        }
      };

      xhr.onerror = () => reject(new Error('Upload connection failed'));
      xhr.send(file);
    });
  }

  async downloadFirmwareFromUrl(url: string): Promise<void> {
    await this.fetchJson('/api/firmware/url', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ url })
    });
  }

  async firmwareRollback(): Promise<void> {
    await this.fetchJson('/api/firmware/rollback', { method: 'POST' });
  }

  async firmwareUf2Reboot(): Promise<void> {
    await this.fetchJson('/api/firmware/uf2', { method: 'POST' });
  }

  async firmwareConfirm(): Promise<void> {
    await this.fetchJson('/api/firmware/confirm', { method: 'POST' });
  }
```

**Step 3: Verify frontend build**

Run: `cd components/keyer_webui/frontend && npm run build`
Expected: Build succeeds (no Firmware.svelte yet, but types and API compile)

**Step 4: Commit**

```
feat(webui): add firmware TypeScript types and API client methods
```

---

### Task 8: Create `Firmware.svelte` page

**Files:**
- Create: `components/keyer_webui/frontend/src/pages/Firmware.svelte`
- Modify: `components/keyer_webui/frontend/src/App.svelte:1-47`

**Step 1: Create Firmware.svelte**

Create `components/keyer_webui/frontend/src/pages/Firmware.svelte`:

```svelte
<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { api } from '../lib/api';
  import type { FirmwareStatus } from '../lib/types';

  let fw = $state<FirmwareStatus | null>(null);
  let error = $state<string | null>(null);
  let uploadProgress = $state(0);
  let downloadUrl = $state('');
  let selectedFile = $state<File | null>(null);
  let pollInterval: number | null = null;
  let uploading = $state(false);

  function formatBytes(bytes: number): string {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / (1024 * 1024)).toFixed(2) + ' MB';
  }

  async function refresh() {
    try {
      fw = await api.getFirmwareStatus();
      error = null;
    } catch (e) {
      error = e instanceof Error ? e.message : 'Unknown error';
    }
  }

  function handleFileSelect(e: Event) {
    const input = e.target as HTMLInputElement;
    if (input.files && input.files.length > 0) {
      selectedFile = input.files[0];
    }
  }

  function handleDrop(e: DragEvent) {
    e.preventDefault();
    if (e.dataTransfer?.files && e.dataTransfer.files.length > 0) {
      selectedFile = e.dataTransfer.files[0];
    }
  }

  function handleDragOver(e: DragEvent) {
    e.preventDefault();
  }

  async function startUpload() {
    if (!selectedFile) return;
    if (!confirm(`>>> FLASH FIRMWARE <<<\n\nUpload ${selectedFile.name} (${formatBytes(selectedFile.size)})?\nDevice will reboot after flashing.`)) return;

    uploading = true;
    uploadProgress = 0;
    error = null;

    try {
      await api.uploadFirmware(selectedFile, (pct) => { uploadProgress = pct; });
      await refresh();
      if (confirm('Firmware uploaded successfully. Reboot now?')) {
        await api.reboot();
      }
    } catch (e) {
      error = e instanceof Error ? e.message : 'Upload failed';
    } finally {
      uploading = false;
    }
  }

  async function startDownload() {
    if (!downloadUrl.trim()) return;
    if (!confirm(`>>> DOWNLOAD & FLASH <<<\n\nDownload from:\n${downloadUrl}\n\nContinue?`)) return;

    error = null;
    try {
      await api.downloadFirmwareFromUrl(downloadUrl);
      /* Polling will show progress */
    } catch (e) {
      error = e instanceof Error ? e.message : 'Download failed';
    }
  }

  async function handleRollback() {
    if (!confirm('>>> ROLLBACK FIRMWARE <<<\n\nRevert to previous firmware version?\nDevice will reboot.')) return;
    try {
      await api.firmwareRollback();
    } catch (e) {
      error = e instanceof Error ? e.message : 'Rollback failed';
    }
  }

  async function handleUf2() {
    if (!confirm('>>> USB BOOT MODE <<<\n\nDevice will reboot into ROM bootloader.\nIt will only be accessible via USB until next power cycle.')) return;
    try {
      await api.firmwareUf2Reboot();
    } catch (e) {
      error = e instanceof Error ? e.message : 'UF2 reboot failed';
    }
  }

  async function handleConfirm() {
    try {
      await api.firmwareConfirm();
      await refresh();
    } catch (e) {
      error = e instanceof Error ? e.message : 'Confirm failed';
    }
  }

  onMount(() => {
    refresh();
    pollInterval = setInterval(refresh, 1000) as unknown as number;
  });

  onDestroy(() => {
    if (pollInterval) clearInterval(pollInterval);
  });

  let isOtaBusy = $derived(fw?.ota_state === 'UPLOADING' || fw?.ota_state === 'DOWNLOADING');
  let showProgress = $derived(fw?.ota_state !== 'IDLE');
</script>

<div class="firmware-page">
  <div class="page-header">
    <h1>/// FIRMWARE UPDATE</h1>
  </div>

  {#if error}
    <div class="error-banner">[ERROR] {error}</div>
  {/if}

  <!-- Version Info -->
  {#if fw}
    <section class="panel">
      <h2>> CURRENT FIRMWARE</h2>
      <div class="info-grid">
        <div class="info-row">
          <span class="info-label">Version</span>
          <span class="info-value">{fw.version}</span>
        </div>
        <div class="info-row">
          <span class="info-label">Build Date</span>
          <span class="info-value">{fw.date} {fw.time}</span>
        </div>
        <div class="info-row">
          <span class="info-label">IDF Version</span>
          <span class="info-value">{fw.idf_ver}</span>
        </div>
        <div class="info-row">
          <span class="info-label">Active Partition</span>
          <span class="info-value">{fw.running_partition}</span>
        </div>
        <div class="info-row">
          <span class="info-label">Next Partition</span>
          <span class="info-value">{fw.next_partition}</span>
        </div>
        <div class="info-row">
          <span class="info-label">Rollback</span>
          <span class="info-value" class:text-ok={fw.rollback_available} class:text-dim={!fw.rollback_available}>
            {fw.rollback_available ? 'AVAILABLE' : 'NOT AVAILABLE'}
          </span>
        </div>
        {#if fw.pending_verify}
          <div class="info-row warning">
            <span class="info-label">Status</span>
            <span class="info-value text-warn">PENDING VERIFICATION</span>
          </div>
        {/if}
      </div>

      {#if fw.pending_verify}
        <button onclick={handleConfirm}>Confirm Firmware</button>
      {/if}
      {#if fw.rollback_available}
        <button class="danger" onclick={handleRollback}>Rollback</button>
      {/if}
    </section>
  {/if}

  <!-- OTA Progress -->
  {#if showProgress && fw}
    <section class="panel">
      <h2>> OTA STATUS</h2>
      <div class="info-row">
        <span class="info-label">State</span>
        <span class="info-value">{fw.ota_state}</span>
      </div>
      {#if isOtaBusy}
        <div class="progress-container">
          <div class="progress-bar" style="width: {fw.ota_progress}%"></div>
          <span class="progress-text">{fw.ota_progress}%</span>
        </div>
        <div class="info-row">
          <span class="info-label">Written</span>
          <span class="info-value">
            {formatBytes(fw.ota_bytes_written)}
            {#if fw.ota_total_size > 0} / {formatBytes(fw.ota_total_size)}{/if}
          </span>
        </div>
      {/if}
      {#if fw.ota_state === 'ERROR'}
        <div class="error-banner">{fw.ota_error}</div>
      {/if}
      {#if fw.ota_state === 'DONE'}
        <div class="success-banner">Firmware written successfully. Reboot to apply.</div>
        <button onclick={() => api.reboot()}>Reboot Now</button>
      {/if}
    </section>
  {/if}

  <!-- Upload -->
  <section class="panel">
    <h2>> UPLOAD FIRMWARE (.bin)</h2>
    <div
      class="drop-zone"
      class:has-file={selectedFile}
      ondrop={handleDrop}
      ondragover={handleDragOver}
    >
      {#if selectedFile}
        <span class="file-info">{selectedFile.name} ({formatBytes(selectedFile.size)})</span>
      {:else}
        <span class="drop-text">Drop .bin file here or click to select</span>
      {/if}
      <input type="file" accept=".bin" onchange={handleFileSelect} />
    </div>
    {#if uploading}
      <div class="progress-container">
        <div class="progress-bar" style="width: {uploadProgress}%"></div>
        <span class="progress-text">{uploadProgress}% uploading...</span>
      </div>
    {:else}
      <button onclick={startUpload} disabled={!selectedFile || isOtaBusy}>Flash Firmware</button>
    {/if}
  </section>

  <!-- URL Download -->
  <section class="panel">
    <h2>> DOWNLOAD FROM URL</h2>
    <div class="url-input">
      <input
        type="text"
        bind:value={downloadUrl}
        placeholder="http://example.com/firmware.bin"
        disabled={isOtaBusy}
      />
      <button onclick={startDownload} disabled={!downloadUrl.trim() || isOtaBusy}>Download & Flash</button>
    </div>
  </section>

  <!-- USB/UF2 -->
  <section class="panel">
    <h2>> USB BOOT MODE</h2>
    <p class="panel-desc">Reboot into ROM bootloader for USB firmware upload (esptool/UF2).</p>
    <button class="danger" onclick={handleUf2} disabled={isOtaBusy}>Reboot to USB Mode</button>
  </section>
</div>

<style>
  .firmware-page {
    display: flex;
    flex-direction: column;
    gap: 1.5rem;
  }

  .page-header h1 {
    color: var(--text-primary);
    font-size: 1.1rem;
    font-weight: 600;
  }

  .panel {
    background: var(--bg-card);
    border: 1px solid var(--border-dim);
    padding: 1.25rem;
    display: flex;
    flex-direction: column;
    gap: 0.75rem;
  }

  .panel h2 {
    color: var(--text-primary);
    font-size: 0.9rem;
    font-weight: 600;
    margin-bottom: 0.25rem;
  }

  .panel-desc {
    color: var(--text-dim);
    font-size: 0.85rem;
  }

  .info-grid {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
  }

  .info-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 0.25rem 0;
    font-size: 0.85rem;
  }

  .info-label {
    color: var(--text-dim);
  }

  .info-value {
    color: var(--text-primary);
    font-weight: 500;
  }

  .text-ok { color: var(--accent-green); }
  .text-dim { color: var(--text-dim); }
  .text-warn { color: var(--accent-amber); }

  .error-banner {
    background: rgba(255, 71, 87, 0.1);
    border: 1px solid var(--accent-red);
    color: var(--accent-red);
    padding: 0.5rem 0.75rem;
    font-size: 0.85rem;
  }

  .success-banner {
    background: rgba(0, 255, 65, 0.1);
    border: 1px solid var(--accent-green);
    color: var(--accent-green);
    padding: 0.5rem 0.75rem;
    font-size: 0.85rem;
  }

  .warning .info-value {
    animation: blink-warn 1s step-end infinite;
  }

  @keyframes blink-warn {
    0%, 50% { opacity: 1; }
    51%, 100% { opacity: 0.4; }
  }

  .progress-container {
    position: relative;
    height: 24px;
    background: var(--bg-primary);
    border: 1px solid var(--border-dim);
    overflow: hidden;
  }

  .progress-bar {
    height: 100%;
    background: var(--accent-green);
    opacity: 0.3;
    transition: width 0.3s ease;
  }

  .progress-text {
    position: absolute;
    top: 50%;
    left: 50%;
    transform: translate(-50%, -50%);
    font-size: 0.8rem;
    color: var(--text-primary);
    font-weight: 600;
  }

  .drop-zone {
    border: 2px dashed var(--border-dim);
    padding: 2rem;
    text-align: center;
    cursor: pointer;
    position: relative;
    transition: all 0.15s ease;
  }

  .drop-zone:hover {
    border-color: var(--text-primary);
    background: var(--bg-hover);
  }

  .drop-zone.has-file {
    border-color: var(--accent-green);
    border-style: solid;
  }

  .drop-zone input[type="file"] {
    position: absolute;
    inset: 0;
    opacity: 0;
    cursor: pointer;
  }

  .drop-text {
    color: var(--text-dim);
    font-size: 0.9rem;
  }

  .file-info {
    color: var(--text-primary);
    font-size: 0.9rem;
    font-weight: 500;
  }

  .url-input {
    display: flex;
    gap: 0.75rem;
  }

  .url-input input {
    flex: 1;
  }

  button:disabled {
    opacity: 0.4;
    cursor: not-allowed;
  }

  button:disabled:hover {
    background: var(--bg-tertiary);
    color: var(--text-primary);
    box-shadow: none;
  }
</style>
```

**Step 2: Add route to App.svelte**

In `App.svelte`, add the import (after line 8):

```typescript
  import Firmware from './pages/Firmware.svelte';
```

Add to `navItems` array (after Timeline, line 46):

```typescript
    { path: '/firmware', label: 'FIRMWARE', key: 'F7' },
```

Add the route in the `{#if}` chain (after Timeline block, before `{:else}`):

```svelte
    {:else if currentPage === '/firmware'}
      <Firmware />
```

**Step 3: Verify frontend build**

Run: `cd components/keyer_webui/frontend && npm run build`
Expected: Build succeeds

**Step 4: Full project build**

Run: `idf.py build`
Expected: Build succeeds (assets.c regenerated with Firmware page)

**Step 5: Commit**

```
feat(webui): add Firmware page with upload, URL download, rollback, and UF2 reboot
```

---

### Task 9: Final verification and cleanup

**Files:**
- Modify: `.claude/feature-status.md`
- Modify: `.claude/code-quality.md`

**Step 1: Full build**

Run: `source /home/sf/esp/esp-idf/export.sh && idf.py build`
Expected: Clean build with no warnings

**Step 2: Run host tests (to check nothing is broken)**

Run: `cd test_host && cmake -B build && cmake --build build && ./build/test_runner`
Expected: All existing tests pass

**Step 3: Update feature-status.md**

Update the OTA firmware update row from "UF2 stub" to "Implemented: upload, URL download, UF2 reboot, auto-rollback"

**Step 4: Update code-quality.md**

Remove the `usb_uf2.c` stub note (it's still a stub for UF2 protocol but now properly integrated via the WebUI).

**Step 5: Commit**

```
docs: update feature status for firmware update implementation
```
