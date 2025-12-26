#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

static const char *TAG = "api_system";

/* GET /api/status */
esp_err_t api_status_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON alloc failed");
        return ESP_FAIL;
    }

    /* TODO: Integrate with wifi module when available */
    cJSON_AddStringToObject(root, "mode", "UNKNOWN");
    cJSON_AddStringToObject(root, "ip", "0.0.0.0");
    cJSON_AddBoolToObject(root, "ready", false);

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

/* GET /api/system/stats */
esp_err_t api_system_stats_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON alloc failed");
        return ESP_FAIL;
    }

    /* Uptime */
    int64_t uptime_us = esp_timer_get_time();
    int64_t uptime_sec = uptime_us / 1000000;
    cJSON *uptime = cJSON_CreateObject();
    cJSON_AddNumberToObject(uptime, "hours", (int)(uptime_sec / 3600));
    cJSON_AddNumberToObject(uptime, "minutes", (int)((uptime_sec % 3600) / 60));
    cJSON_AddNumberToObject(uptime, "seconds", (int)(uptime_sec % 60));
    cJSON_AddNumberToObject(uptime, "total_seconds", (int)uptime_sec);
    cJSON_AddItemToObject(root, "uptime", uptime);

    /* Heap */
    cJSON *heap = cJSON_CreateObject();
    multi_heap_info_t heap_info;
    heap_caps_get_info(&heap_info, MALLOC_CAP_DEFAULT);
    cJSON_AddNumberToObject(heap, "free_bytes", (int)heap_info.total_free_bytes);
    cJSON_AddNumberToObject(heap, "minimum_free_bytes", (int)heap_info.minimum_free_bytes);
    cJSON_AddNumberToObject(heap, "total_bytes",
        (int)(heap_info.total_free_bytes + heap_info.total_allocated_bytes));
    cJSON_AddNumberToObject(heap, "largest_free_block", (int)heap_info.largest_free_block);
    cJSON_AddItemToObject(root, "heap", heap);

    /* Tasks */
    cJSON *tasks = cJSON_CreateArray();
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    TaskStatus_t *task_array = malloc(task_count * sizeof(TaskStatus_t));
    if (task_array != NULL) {
        uint32_t total_runtime;
        task_count = uxTaskGetSystemState(task_array, task_count, &total_runtime);
        for (UBaseType_t i = 0; i < task_count; i++) {
            cJSON *task = cJSON_CreateObject();
            cJSON_AddStringToObject(task, "name", task_array[i].pcTaskName);
            const char *state_str;
            switch (task_array[i].eCurrentState) {
                case eRunning: state_str = "Running"; break;
                case eReady: state_str = "Ready"; break;
                case eBlocked: state_str = "Blocked"; break;
                case eSuspended: state_str = "Suspended"; break;
                default: state_str = "Unknown"; break;
            }
            cJSON_AddStringToObject(task, "state", state_str);
            cJSON_AddNumberToObject(task, "priority", (int)task_array[i].uxCurrentPriority);
            cJSON_AddNumberToObject(task, "stack_hwm", (int)task_array[i].usStackHighWaterMark);
            cJSON_AddItemToArray(tasks, task);
        }
        free(task_array);
    }
    cJSON_AddItemToObject(root, "tasks", tasks);

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

/* POST /api/system/reboot */
esp_err_t api_system_reboot_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Reboot requested");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();

    return ESP_OK;  /* Never reached */
}
