#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include "vpn.h"
#include <string.h>

/* GET /api/vpn/status */
esp_err_t api_vpn_status_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON alloc failed");
        return ESP_FAIL;
    }

    vpn_state_t state = vpn_get_state();
    cJSON_AddStringToObject(root, "state", vpn_state_str(state));
    cJSON_AddBoolToObject(root, "connected", state == VPN_STATE_CONNECTED);

    vpn_stats_t stats;
    if (vpn_get_stats(&stats)) {
        cJSON *stats_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(stats_obj, "handshakes", stats.handshakes);
        cJSON_AddNumberToObject(stats_obj, "last_handshake_us", (double)stats.last_handshake_us);
        cJSON_AddItemToObject(root, "stats", stats_obj);
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
