# WebUI Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement full-featured WebUI for CW keyer with Svelte 5 frontend and ESP-IDF HTTP backend.

**Architecture:** ESP-IDF `esp_http_server` serves embedded gzip assets and REST/SSE APIs. Frontend is Svelte 5 SPA with client-side routing. SSE for real-time timeline/decoder streams.

**Tech Stack:** C (ESP-IDF), Svelte 5, TypeScript, Vite, Python (asset embedding)

---

## Phase 1: Backend Infrastructure

### Task 1: Create Component Structure

**Files:**
- Create: `components/keyer_webui/CMakeLists.txt`
- Create: `components/keyer_webui/include/webui.h`
- Create: `components/keyer_webui/src/http_server.c`

**Step 1: Create component directory structure**

```bash
mkdir -p components/keyer_webui/include
mkdir -p components/keyer_webui/src
mkdir -p components/keyer_webui/frontend
mkdir -p components/keyer_webui/scripts
```

**Step 2: Create CMakeLists.txt**

Create `components/keyer_webui/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS
        "src/http_server.c"
        "src/api_config.c"
        "src/api_keyer.c"
        "src/api_system.c"
        "src/api_decoder.c"
        "src/api_timeline.c"
        "src/sse.c"
        "src/assets.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_http_server
        json
        keyer_config
        keyer_core
        keyer_decoder
)

# Strict compiler flags
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror
    -Wno-unused-parameter
    -Wconversion -Wsign-conversion
)
```

**Step 3: Create webui.h header**

Create `components/keyer_webui/include/webui.h`:

```c
#ifndef KEYER_WEBUI_H
#define KEYER_WEBUI_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize WebUI HTTP server
 * @return ESP_OK on success
 */
esp_err_t webui_init(void);

/**
 * @brief Start HTTP server
 * @return ESP_OK on success
 */
esp_err_t webui_start(void);

/**
 * @brief Stop HTTP server
 * @return ESP_OK on success
 */
esp_err_t webui_stop(void);

/**
 * @brief Push timeline event to SSE clients
 * @param event_type Event type string
 * @param json_data JSON payload
 */
void webui_timeline_push(const char *event_type, const char *json_data);

/**
 * @brief Push decoded character to SSE clients
 * @param c Decoded character
 * @param wpm Current WPM
 */
void webui_decoder_push_char(char c, uint8_t wpm);

/**
 * @brief Push word boundary to SSE clients
 */
void webui_decoder_push_word(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_WEBUI_H */
```

**Step 4: Create minimal http_server.c stub**

Create `components/keyer_webui/src/http_server.c`:

```c
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
```

**Step 5: Create empty source stubs**

Create empty files so CMake works:

```bash
touch components/keyer_webui/src/api_config.c
touch components/keyer_webui/src/api_keyer.c
touch components/keyer_webui/src/api_system.c
touch components/keyer_webui/src/api_decoder.c
touch components/keyer_webui/src/api_timeline.c
touch components/keyer_webui/src/sse.c
touch components/keyer_webui/src/assets.c
```

Each stub file should contain:

```c
/* Stub - to be implemented */
```

**Step 6: Verify build**

```bash
idf.py build
```

Expected: Build succeeds with new component

**Step 7: Commit**

```bash
git add components/keyer_webui/
git commit -m "feat(webui): add component skeleton"
```

---

### Task 2: Asset Embedding Script

**Files:**
- Create: `components/keyer_webui/scripts/embed_assets.py`
- Create: `components/keyer_webui/include/assets.h`

**Step 1: Create assets.h header**

Create `components/keyer_webui/include/assets.h`:

```c
#ifndef KEYER_WEBUI_ASSETS_H
#define KEYER_WEBUI_ASSETS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Embedded asset descriptor
 */
typedef struct {
    const char *path;           /**< URL path (e.g., "/index.html") */
    const char *content_type;   /**< MIME type */
    const uint8_t *data;        /**< Gzip-compressed data */
    size_t length;              /**< Data length */
    bool gzipped;               /**< Always true for embedded assets */
} webui_asset_t;

/**
 * @brief Find asset by path
 * @param path URL path
 * @return Asset pointer or NULL
 */
const webui_asset_t *webui_find_asset(const char *path);

/**
 * @brief Get asset count
 * @return Number of embedded assets
 */
size_t webui_get_asset_count(void);

/**
 * @brief Get all assets
 * @return Array of assets (NULL-terminated)
 */
const webui_asset_t *webui_get_assets(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_WEBUI_ASSETS_H */
```

**Step 2: Create embed_assets.py**

Create `components/keyer_webui/scripts/embed_assets.py`:

```python
#!/usr/bin/env python3
"""
Embed web assets as C arrays with gzip compression.

Usage:
    python3 embed_assets.py <dist_dir> <output.c>

Example:
    python3 embed_assets.py frontend/dist src/assets.c
"""

import sys
import gzip
from pathlib import Path


MIME_TYPES = {
    '.html': 'text/html',
    '.js': 'application/javascript',
    '.css': 'text/css',
    '.json': 'application/json',
    '.svg': 'image/svg+xml',
    '.png': 'image/png',
    '.ico': 'image/x-icon',
}


def get_mime_type(path: Path) -> str:
    return MIME_TYPES.get(path.suffix.lower(), 'application/octet-stream')


def sanitize_name(path: str) -> str:
    """Convert path to valid C identifier."""
    return path.replace('/', '_').replace('.', '_').replace('-', '_').lstrip('_')


def bytes_to_c_array(data: bytes) -> str:
    """Convert bytes to C array initializer."""
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_values = ', '.join(f'0x{b:02x}' for b in chunk)
        lines.append(f'    {hex_values},')
    return '\n'.join(lines)


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <dist_dir> <output.c>", file=sys.stderr)
        sys.exit(1)

    dist_dir = Path(sys.argv[1])
    output_file = Path(sys.argv[2])

    if not dist_dir.exists():
        print(f"ERROR: {dist_dir} not found", file=sys.stderr)
        sys.exit(1)

    assets = []

    # Collect all files
    for file_path in sorted(dist_dir.rglob('*')):
        if file_path.is_dir():
            continue

        rel_path = '/' + str(file_path.relative_to(dist_dir))
        content = file_path.read_bytes()
        compressed = gzip.compress(content, compresslevel=9)

        assets.append({
            'path': rel_path,
            'name': sanitize_name(rel_path),
            'mime': get_mime_type(file_path),
            'data': compressed,
            'original_size': len(content),
        })

        print(f"  {rel_path}: {len(content)} -> {len(compressed)} bytes "
              f"({100*len(compressed)//len(content)}%)")

    # Generate C file
    output_lines = [
        '/* Auto-generated by embed_assets.py - DO NOT EDIT */',
        '#include "assets.h"',
        '#include <string.h>',
        '',
    ]

    # Data arrays
    for asset in assets:
        output_lines.append(f'static const uint8_t asset_{asset["name"]}[] = {{')
        output_lines.append(bytes_to_c_array(asset['data']))
        output_lines.append('};')
        output_lines.append('')

    # Asset table
    output_lines.append('static const webui_asset_t s_assets[] = {')

    # Add root alias for index.html
    index_asset = next((a for a in assets if a['path'] == '/index.html'), None)
    if index_asset:
        output_lines.append(f'    {{"/", "{index_asset["mime"]}", '
                          f'asset_{index_asset["name"]}, {len(index_asset["data"])}, true}},')

    for asset in assets:
        output_lines.append(f'    {{"{asset["path"]}", "{asset["mime"]}", '
                          f'asset_{asset["name"]}, {len(asset["data"])}, true}},')

    output_lines.append('    {NULL, NULL, NULL, 0, false},  /* Terminator */')
    output_lines.append('};')
    output_lines.append('')

    # Functions
    output_lines.extend([
        f'static const size_t s_asset_count = {len(assets) + (1 if index_asset else 0)};',
        '',
        'const webui_asset_t *webui_find_asset(const char *path) {',
        '    for (size_t i = 0; s_assets[i].path != NULL; i++) {',
        '        if (strcmp(s_assets[i].path, path) == 0) {',
        '            return &s_assets[i];',
        '        }',
        '    }',
        '    return NULL;',
        '}',
        '',
        'size_t webui_get_asset_count(void) {',
        '    return s_asset_count;',
        '}',
        '',
        'const webui_asset_t *webui_get_assets(void) {',
        '    return s_assets;',
        '}',
    ])

    output_file.write_text('\n'.join(output_lines) + '\n')
    print(f"\nGenerated {output_file} with {len(assets)} assets")


if __name__ == '__main__':
    main()
```

**Step 3: Make script executable**

```bash
chmod +x components/keyer_webui/scripts/embed_assets.py
```

**Step 4: Create placeholder assets.c**

Update `components/keyer_webui/src/assets.c`:

```c
/* Placeholder - regenerate with embed_assets.py after frontend build */
#include "assets.h"
#include <stddef.h>

static const uint8_t asset_placeholder[] = {0};

static const webui_asset_t s_assets[] = {
    {NULL, NULL, NULL, 0, false},
};

const webui_asset_t *webui_find_asset(const char *path) {
    (void)path;
    return NULL;
}

size_t webui_get_asset_count(void) {
    return 0;
}

const webui_asset_t *webui_get_assets(void) {
    return s_assets;
}
```

**Step 5: Verify build**

```bash
idf.py build
```

**Step 6: Commit**

```bash
git add components/keyer_webui/scripts/embed_assets.py
git add components/keyer_webui/include/assets.h
git add components/keyer_webui/src/assets.c
git commit -m "feat(webui): add asset embedding script"
```

---

### Task 3: Asset Serving

**Files:**
- Modify: `components/keyer_webui/src/http_server.c`

**Step 1: Add asset serving handler**

Update `components/keyer_webui/src/http_server.c`:

```c
#include "webui.h"
#include "assets.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "webui";
static httpd_handle_t s_server = NULL;

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

    /* Register /assets/* wildcard */
    httpd_uri_t assets_uri = {
        .uri = "/assets/*",
        .method = HTTP_GET,
        .handler = handle_static,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server, &assets_uri);
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
```

**Step 2: Verify build**

```bash
idf.py build
```

**Step 3: Commit**

```bash
git add components/keyer_webui/src/http_server.c
git commit -m "feat(webui): add static asset serving with SPA routing"
```

---

### Task 4: Config Schema Generation

**Files:**
- Modify: `scripts/gen_config_c.py`
- Create: `components/keyer_webui/include/config_schema.h`

**Step 1: Add JSON schema generation to gen_config_c.py**

Add this function to `scripts/gen_config_c.py` (before `main()`):

```python
def generate_config_schema_json(params, families, output_dir: Path):
    """Generate JSON schema for WebUI."""
    import json

    schema = {"parameters": []}

    for param in params:
        p = {
            "name": param['full_path'],
            "type": param['type'].lower(),
            "widget": param.get('widget', 'spinbox'),
            "description": param.get('label', {}).get('long', {}).get('en', param['name']),
        }

        if 'unit' in param:
            p['unit'] = param['unit']

        if 'range' in param:
            p['min'] = param['range'].get('min')
            p['max'] = param['range'].get('max')

        if param['type'].lower() == 'enum' and 'values' in param:
            p['values'] = [
                {"name": v['name'], "description": v.get('label', {}).get('en', v['name'])}
                for v in param['values']
            ]

        schema['parameters'].append(p)

    json_str = json.dumps(schema, indent=2)

    # Generate C header with embedded JSON
    header_content = f'''/* Auto-generated - DO NOT EDIT */
#ifndef CONFIG_SCHEMA_JSON_H
#define CONFIG_SCHEMA_JSON_H

static const char CONFIG_SCHEMA_JSON[] = R"JSON(
{json_str}
)JSON";

#endif
'''
    output_path = output_dir / 'config_schema.h'
    output_path.write_text(header_content)
    print(f"Generated {output_path}")
```

And add to `main()` after other generates:

```python
    print("Generating config_schema.h...")
    generate_config_schema_json(params, families, output_dir)
```

**Step 2: Regenerate config files**

```bash
python3 scripts/gen_config_c.py parameters.yaml components/keyer_config/include
```

**Step 3: Copy schema to webui**

```bash
cp components/keyer_config/include/config_schema.h components/keyer_webui/include/
```

**Step 4: Verify build**

```bash
idf.py build
```

**Step 5: Commit**

```bash
git add scripts/gen_config_c.py
git add components/keyer_config/include/config_schema.h
git add components/keyer_webui/include/config_schema.h
git commit -m "feat(config): generate JSON schema for WebUI"
```

---

### Task 5: Config API Endpoints

**Files:**
- Modify: `components/keyer_webui/src/api_config.c`
- Modify: `components/keyer_webui/src/http_server.c`

**Step 1: Implement api_config.c**

Create `components/keyer_webui/src/api_config.c`:

```c
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
```

**Step 2: Register config routes in http_server.c**

Add to `components/keyer_webui/src/http_server.c`, declare external handlers:

```c
/* API handlers (implemented in api_*.c) */
extern esp_err_t api_config_schema_handler(httpd_req_t *req);
extern esp_err_t api_config_get_handler(httpd_req_t *req);
extern esp_err_t api_parameter_set_handler(httpd_req_t *req);
extern esp_err_t api_config_save_handler(httpd_req_t *req);
```

Add function to register API routes:

```c
static void register_api_routes(httpd_handle_t server) {
    /* Config API */
    httpd_uri_t schema = {
        .uri = "/api/config/schema",
        .method = HTTP_GET,
        .handler = api_config_schema_handler,
    };
    httpd_register_uri_handler(server, &schema);

    httpd_uri_t config_get = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = api_config_get_handler,
    };
    httpd_register_uri_handler(server, &config_get);

    httpd_uri_t param_set = {
        .uri = "/api/parameter",
        .method = HTTP_POST,
        .handler = api_parameter_set_handler,
    };
    httpd_register_uri_handler(server, &param_set);

    httpd_uri_t config_save = {
        .uri = "/api/config/save",
        .method = HTTP_POST,
        .handler = api_config_save_handler,
    };
    httpd_register_uri_handler(server, &config_save);
}
```

Call in `webui_start()` after `register_static_routes(s_server)`:

```c
    register_api_routes(s_server);
```

**Step 3: Verify build**

```bash
idf.py build
```

**Step 4: Commit**

```bash
git add components/keyer_webui/src/api_config.c
git add components/keyer_webui/src/http_server.c
git commit -m "feat(webui): implement config API endpoints"
```

---

### Task 6: System API Endpoints

**Files:**
- Modify: `components/keyer_webui/src/api_system.c`
- Modify: `components/keyer_webui/src/http_server.c`

**Step 1: Implement api_system.c**

Create `components/keyer_webui/src/api_system.c`:

```c
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "wifi.h"

static const char *TAG = "api_system";

/* GET /api/status */
esp_err_t api_status_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();

    wifi_state_t state = wifi_get_state();
    const char *mode;
    switch (state) {
        case WIFI_STATE_CONNECTED: mode = "STA"; break;
        case WIFI_STATE_AP_MODE: mode = "AP"; break;
        case WIFI_STATE_CONNECTING: mode = "CONNECTING"; break;
        default: mode = "DISABLED"; break;
    }

    cJSON_AddStringToObject(root, "mode", mode);

    char ip[16] = "0.0.0.0";
    wifi_get_ip(ip, sizeof(ip));
    cJSON_AddStringToObject(root, "ip", ip);

    cJSON_AddBoolToObject(root, "ready",
        state == WIFI_STATE_CONNECTED || state == WIFI_STATE_AP_MODE);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json_str);
    return ret;
}

/* GET /api/system/stats */
esp_err_t api_system_stats_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();

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
            cJSON_AddNumberToObject(task, "priority", task_array[i].uxCurrentPriority);
            cJSON_AddNumberToObject(task, "stack_hwm", (int)task_array[i].usStackHighWaterMark);
            cJSON_AddItemToArray(tasks, task);
        }
        free(task_array);
    }
    cJSON_AddItemToObject(root, "tasks", tasks);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

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
```

**Step 2: Register system routes in http_server.c**

Add declarations:

```c
extern esp_err_t api_status_handler(httpd_req_t *req);
extern esp_err_t api_system_stats_handler(httpd_req_t *req);
extern esp_err_t api_system_reboot_handler(httpd_req_t *req);
```

Add to `register_api_routes()`:

```c
    /* System API */
    httpd_uri_t status = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = api_status_handler,
    };
    httpd_register_uri_handler(server, &status);

    httpd_uri_t stats = {
        .uri = "/api/system/stats",
        .method = HTTP_GET,
        .handler = api_system_stats_handler,
    };
    httpd_register_uri_handler(server, &stats);

    httpd_uri_t reboot = {
        .uri = "/api/system/reboot",
        .method = HTTP_POST,
        .handler = api_system_reboot_handler,
    };
    httpd_register_uri_handler(server, &reboot);
```

**Step 3: Verify build**

```bash
idf.py build
```

**Step 4: Commit**

```bash
git add components/keyer_webui/src/api_system.c
git add components/keyer_webui/src/http_server.c
git commit -m "feat(webui): implement system API endpoints"
```

---

### Task 7: Decoder API Endpoints

**Files:**
- Modify: `components/keyer_webui/src/api_decoder.c`
- Modify: `components/keyer_webui/src/http_server.c`

**Step 1: Implement api_decoder.c**

Create `components/keyer_webui/src/api_decoder.c`:

```c
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include "decoder.h"

static const char *TAG = "api_decoder";

/* GET /api/decoder/status */
esp_err_t api_decoder_status_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();

    cJSON_AddBoolToObject(root, "enabled", decoder_is_enabled());
    cJSON_AddNumberToObject(root, "wpm", (int)decoder_get_wpm());

    char pattern[16];
    decoder_get_current_pattern(pattern, sizeof(pattern));
    cJSON_AddStringToObject(root, "pattern", pattern);

    char text[128];
    decoder_get_text(text, sizeof(text));
    cJSON_AddStringToObject(root, "text", text);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json_str);
    return ret;
}

/* POST /api/decoder/enable - body: {"enabled": true} */
esp_err_t api_decoder_enable_handler(httpd_req_t *req) {
    char buf[64];
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

    cJSON *enabled = cJSON_GetObjectItem(json, "enabled");
    if (!cJSON_IsBool(enabled)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing enabled field");
        return ESP_FAIL;
    }

    bool enable = cJSON_IsTrue(enabled);
    cJSON_Delete(json);

    decoder_set_enabled(enable);
    ESP_LOGI(TAG, "Decoder %s", enable ? "enabled" : "disabled");

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
}
```

**Step 2: Register decoder routes in http_server.c**

Add declarations:

```c
extern esp_err_t api_decoder_status_handler(httpd_req_t *req);
extern esp_err_t api_decoder_enable_handler(httpd_req_t *req);
```

Add to `register_api_routes()`:

```c
    /* Decoder API */
    httpd_uri_t decoder_status = {
        .uri = "/api/decoder/status",
        .method = HTTP_GET,
        .handler = api_decoder_status_handler,
    };
    httpd_register_uri_handler(server, &decoder_status);

    httpd_uri_t decoder_enable = {
        .uri = "/api/decoder/enable",
        .method = HTTP_POST,
        .handler = api_decoder_enable_handler,
    };
    httpd_register_uri_handler(server, &decoder_enable);
```

**Step 3: Verify build**

```bash
idf.py build
```

**Step 4: Commit**

```bash
git add components/keyer_webui/src/api_decoder.c
git add components/keyer_webui/src/http_server.c
git commit -m "feat(webui): implement decoder API endpoints"
```

---

### Task 8: SSE Infrastructure

**Files:**
- Modify: `components/keyer_webui/src/sse.c`
- Create: `components/keyer_webui/include/sse.h`
- Modify: `components/keyer_webui/src/http_server.c`

**Step 1: Create sse.h**

Create `components/keyer_webui/include/sse.h`:

```c
#ifndef KEYER_WEBUI_SSE_H
#define KEYER_WEBUI_SSE_H

#include "esp_http_server.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SSE_MAX_CLIENTS 4

typedef enum {
    SSE_STREAM_TIMELINE,
    SSE_STREAM_DECODER,
    SSE_STREAM_COUNT
} sse_stream_t;

/**
 * @brief Initialize SSE subsystem
 */
void sse_init(void);

/**
 * @brief Register new SSE client
 * @param stream Which stream to subscribe to
 * @param req HTTP request (kept open for streaming)
 * @return ESP_OK on success
 */
esp_err_t sse_client_register(sse_stream_t stream, httpd_req_t *req);

/**
 * @brief Send event to all clients on a stream
 * @param stream Target stream
 * @param event_type Event type (e.g., "char", "paddle")
 * @param json_data JSON payload
 */
void sse_broadcast(sse_stream_t stream, const char *event_type, const char *json_data);

/**
 * @brief Send keepalive to all clients
 */
void sse_send_keepalive(void);

/**
 * @brief Get connected client count for a stream
 */
int sse_get_client_count(sse_stream_t stream);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_WEBUI_SSE_H */
```

**Step 2: Implement sse.c**

Create `components/keyer_webui/src/sse.c`:

```c
#include "sse.h"
#include "esp_log.h"
#include <string.h>
#include <sys/socket.h>

static const char *TAG = "sse";

typedef struct {
    int fd;
    bool active;
} sse_client_t;

static sse_client_t s_clients[SSE_STREAM_COUNT][SSE_MAX_CLIENTS];

void sse_init(void) {
    memset(s_clients, 0, sizeof(s_clients));
    ESP_LOGI(TAG, "SSE initialized (max %d clients per stream)", SSE_MAX_CLIENTS);
}

esp_err_t sse_client_register(sse_stream_t stream, httpd_req_t *req) {
    if (stream >= SSE_STREAM_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Find empty slot */
    int slot = -1;
    for (int i = 0; i < SSE_MAX_CLIENTS; i++) {
        if (!s_clients[stream][i].active) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        ESP_LOGW(TAG, "No free SSE slots for stream %d", stream);
        httpd_resp_send_err(req, HTTPD_503_BUSY, "Too many SSE clients");
        return ESP_ERR_NO_MEM;
    }

    /* Send SSE headers */
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    /* Send initial comment to establish connection */
    const char *init = ": SSE stream connected\n\n";
    httpd_resp_send_chunk(req, init, strlen(init));

    /* Get socket fd for later sends */
    int fd = httpd_req_to_sockfd(req);
    s_clients[stream][slot].fd = fd;
    s_clients[stream][slot].active = true;

    ESP_LOGI(TAG, "SSE client registered: stream=%d slot=%d fd=%d", stream, slot, fd);

    /* Don't close connection - return without sending final chunk */
    return ESP_OK;
}

void sse_broadcast(sse_stream_t stream, const char *event_type, const char *json_data) {
    if (stream >= SSE_STREAM_COUNT) {
        return;
    }

    char buf[256];
    int len = snprintf(buf, sizeof(buf), "event: %s\ndata: %s\n\n", event_type, json_data);
    if (len >= (int)sizeof(buf)) {
        ESP_LOGW(TAG, "SSE event truncated");
        len = sizeof(buf) - 1;
    }

    for (int i = 0; i < SSE_MAX_CLIENTS; i++) {
        if (s_clients[stream][i].active) {
            int fd = s_clients[stream][i].fd;
            int sent = send(fd, buf, (size_t)len, MSG_DONTWAIT);
            if (sent < 0) {
                ESP_LOGD(TAG, "SSE client disconnected: stream=%d slot=%d", stream, i);
                s_clients[stream][i].active = false;
            }
        }
    }
}

void sse_send_keepalive(void) {
    const char *keepalive = ": keepalive\n\n";
    size_t len = strlen(keepalive);

    for (int stream = 0; stream < SSE_STREAM_COUNT; stream++) {
        for (int i = 0; i < SSE_MAX_CLIENTS; i++) {
            if (s_clients[stream][i].active) {
                int fd = s_clients[stream][i].fd;
                int sent = send(fd, keepalive, len, MSG_DONTWAIT);
                if (sent < 0) {
                    s_clients[stream][i].active = false;
                }
            }
        }
    }
}

int sse_get_client_count(sse_stream_t stream) {
    if (stream >= SSE_STREAM_COUNT) {
        return 0;
    }

    int count = 0;
    for (int i = 0; i < SSE_MAX_CLIENTS; i++) {
        if (s_clients[stream][i].active) {
            count++;
        }
    }
    return count;
}
```

**Step 3: Update http_server.c to use SSE**

Add include and init:

```c
#include "sse.h"
```

In `webui_init()`:

```c
esp_err_t webui_init(void) {
    sse_init();
    ESP_LOGI(TAG, "WebUI initialized (%zu assets)", webui_get_asset_count());
    return ESP_OK;
}
```

Update SSE push functions:

```c
void webui_timeline_push(const char *event_type, const char *json_data) {
    sse_broadcast(SSE_STREAM_TIMELINE, event_type, json_data);
}

void webui_decoder_push_char(char c, uint8_t wpm) {
    char json[32];
    snprintf(json, sizeof(json), "{\"char\":\"%c\",\"wpm\":%u}", c, wpm);
    sse_broadcast(SSE_STREAM_DECODER, "char", json);
}

void webui_decoder_push_word(void) {
    sse_broadcast(SSE_STREAM_DECODER, "word", "{}");
}
```

**Step 4: Add SSE endpoint handlers**

Add to `api_decoder.c`:

```c
#include "sse.h"

/* GET /api/decoder/stream - SSE */
esp_err_t api_decoder_stream_handler(httpd_req_t *req) {
    return sse_client_register(SSE_STREAM_DECODER, req);
}
```

Add to `api_timeline.c`:

```c
#include "sse.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include "config.h"

/* GET /api/timeline/config */
esp_err_t api_timeline_config_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();

    uint16_t wpm = CONFIG_GET_WPM();
    cJSON_AddNumberToObject(root, "wpm", wpm);
    cJSON_AddStringToObject(root, "wpm_source", "keying_config");

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    cJSON_free(json_str);
    return ret;
}

/* GET /api/timeline/stream - SSE */
esp_err_t api_timeline_stream_handler(httpd_req_t *req) {
    return sse_client_register(SSE_STREAM_TIMELINE, req);
}
```

Register in `http_server.c`:

```c
extern esp_err_t api_decoder_stream_handler(httpd_req_t *req);
extern esp_err_t api_timeline_config_handler(httpd_req_t *req);
extern esp_err_t api_timeline_stream_handler(httpd_req_t *req);
```

Add to `register_api_routes()`:

```c
    httpd_uri_t decoder_stream = {
        .uri = "/api/decoder/stream",
        .method = HTTP_GET,
        .handler = api_decoder_stream_handler,
    };
    httpd_register_uri_handler(server, &decoder_stream);

    /* Timeline API */
    httpd_uri_t timeline_config = {
        .uri = "/api/timeline/config",
        .method = HTTP_GET,
        .handler = api_timeline_config_handler,
    };
    httpd_register_uri_handler(server, &timeline_config);

    httpd_uri_t timeline_stream = {
        .uri = "/api/timeline/stream",
        .method = HTTP_GET,
        .handler = api_timeline_stream_handler,
    };
    httpd_register_uri_handler(server, &timeline_stream);
```

**Step 5: Verify build**

```bash
idf.py build
```

**Step 6: Commit**

```bash
git add components/keyer_webui/include/sse.h
git add components/keyer_webui/src/sse.c
git add components/keyer_webui/src/api_decoder.c
git add components/keyer_webui/src/api_timeline.c
git add components/keyer_webui/src/http_server.c
git commit -m "feat(webui): implement SSE for decoder and timeline streams"
```

---

## Phase 2: Frontend

### Task 9: Frontend Scaffold

**Files:**
- Create: `components/keyer_webui/frontend/package.json`
- Create: `components/keyer_webui/frontend/vite.config.ts`
- Create: `components/keyer_webui/frontend/tsconfig.json`
- Create: `components/keyer_webui/frontend/src/main.ts`
- Create: `components/keyer_webui/frontend/src/App.svelte`
- Create: `components/keyer_webui/frontend/index.html`

**Step 1: Create package.json**

Create `components/keyer_webui/frontend/package.json`:

```json
{
  "name": "keyer-webui",
  "version": "1.0.0",
  "type": "module",
  "scripts": {
    "dev": "vite",
    "build": "vite build",
    "preview": "vite preview"
  },
  "devDependencies": {
    "@sveltejs/vite-plugin-svelte": "^4.0.0",
    "svelte": "^5.0.0",
    "typescript": "^5.0.0",
    "vite": "^6.0.0"
  }
}
```

**Step 2: Create vite.config.ts**

Create `components/keyer_webui/frontend/vite.config.ts`:

```typescript
import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';

export default defineConfig({
  plugins: [svelte()],
  build: {
    outDir: 'dist',
    assetsInlineLimit: 0,
    rollupOptions: {
      output: {
        entryFileNames: 'assets/app.js',
        chunkFileNames: 'assets/[name].js',
        assetFileNames: 'assets/[name][extname]'
      }
    }
  },
  server: {
    port: 5173,
    proxy: {
      '/api': {
        target: 'http://192.168.4.1',
        changeOrigin: true
      }
    }
  }
});
```

**Step 3: Create tsconfig.json**

Create `components/keyer_webui/frontend/tsconfig.json`:

```json
{
  "compilerOptions": {
    "target": "ESNext",
    "useDefineForClassFields": true,
    "module": "ESNext",
    "resolveJsonModule": true,
    "allowJs": true,
    "checkJs": true,
    "isolatedModules": true,
    "moduleResolution": "bundler",
    "strict": true,
    "noUnusedLocals": true,
    "noUnusedParameters": true,
    "noFallthroughCasesInSwitch": true
  },
  "include": ["src/**/*.ts", "src/**/*.svelte"]
}
```

**Step 4: Create index.html**

Create `components/keyer_webui/frontend/index.html`:

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>CW Keyer</title>
</head>
<body>
  <div id="app"></div>
  <script type="module" src="/src/main.ts"></script>
</body>
</html>
```

**Step 5: Create main.ts**

Create `components/keyer_webui/frontend/src/main.ts`:

```typescript
import App from './App.svelte';
import { mount } from 'svelte';

const app = mount(App, {
  target: document.getElementById('app')!
});

export default app;
```

**Step 6: Create App.svelte**

Create `components/keyer_webui/frontend/src/App.svelte`:

```svelte
<script lang="ts">
  import { onMount } from 'svelte';

  let currentPage = $state('/');

  onMount(() => {
    currentPage = window.location.pathname;

    window.addEventListener('popstate', () => {
      currentPage = window.location.pathname;
    });
  });

  function navigate(path: string) {
    history.pushState({}, '', path);
    currentPage = path;
  }
</script>

<div class="app">
  <nav>
    <a href="/" onclick={(e) => { e.preventDefault(); navigate('/'); }}>Home</a>
    <a href="/config" onclick={(e) => { e.preventDefault(); navigate('/config'); }}>Config</a>
    <a href="/system" onclick={(e) => { e.preventDefault(); navigate('/system'); }}>System</a>
    <a href="/keyer" onclick={(e) => { e.preventDefault(); navigate('/keyer'); }}>Keyer</a>
    <a href="/decoder" onclick={(e) => { e.preventDefault(); navigate('/decoder'); }}>Decoder</a>
    <a href="/timeline" onclick={(e) => { e.preventDefault(); navigate('/timeline'); }}>Timeline</a>
  </nav>

  <main>
    {#if currentPage === '/config'}
      <h1>Configuration</h1>
      <p>Config page - coming soon</p>
    {:else if currentPage === '/system'}
      <h1>System</h1>
      <p>System page - coming soon</p>
    {:else if currentPage === '/keyer'}
      <h1>Keyer</h1>
      <p>Keyer page - coming soon</p>
    {:else if currentPage === '/decoder'}
      <h1>Decoder</h1>
      <p>Decoder page - coming soon</p>
    {:else if currentPage === '/timeline'}
      <h1>Timeline</h1>
      <p>Timeline page - coming soon</p>
    {:else}
      <h1>CW Keyer</h1>
      <p>Welcome to the CW Keyer WebUI</p>
    {/if}
  </main>
</div>

<style>
  .app {
    font-family: system-ui, sans-serif;
    max-width: 1200px;
    margin: 0 auto;
    padding: 1rem;
  }

  nav {
    display: flex;
    gap: 1rem;
    padding: 1rem;
    background: #f0f0f0;
    border-radius: 8px;
    margin-bottom: 1rem;
  }

  nav a {
    color: #333;
    text-decoration: none;
    padding: 0.5rem 1rem;
    border-radius: 4px;
  }

  nav a:hover {
    background: #ddd;
  }

  main {
    padding: 1rem;
  }
</style>
```

**Step 7: Install dependencies and build**

```bash
cd components/keyer_webui/frontend
npm install
npm run build
cd ../../..
```

**Step 8: Generate embedded assets**

```bash
python3 components/keyer_webui/scripts/embed_assets.py \
  components/keyer_webui/frontend/dist \
  components/keyer_webui/src/assets.c
```

**Step 9: Verify ESP build**

```bash
idf.py build
```

**Step 10: Commit**

```bash
git add components/keyer_webui/frontend/
git add components/keyer_webui/src/assets.c
git commit -m "feat(webui): add Svelte 5 frontend scaffold"
```

---

### Task 10: API Client Library

**Files:**
- Create: `components/keyer_webui/frontend/src/lib/api.ts`
- Create: `components/keyer_webui/frontend/src/lib/types.ts`

**Step 1: Create types.ts**

Create `components/keyer_webui/frontend/src/lib/types.ts`:

```typescript
export interface DeviceStatus {
  mode: string;
  ip: string;
  ready: boolean;
}

export interface SystemUptime {
  hours: number;
  minutes: number;
  seconds: number;
  total_seconds: number;
}

export interface HeapInfo {
  free_bytes: number;
  minimum_free_bytes: number;
  total_bytes: number;
  largest_free_block: number;
}

export interface TaskInfo {
  name: string;
  state: string;
  priority: number;
  stack_hwm: number;
}

export interface SystemStats {
  uptime: SystemUptime;
  heap: HeapInfo;
  tasks: TaskInfo[];
}

export interface DecoderStatus {
  enabled: boolean;
  wpm: number;
  pattern: string;
  text: string;
}

export interface TimelineConfig {
  wpm: number;
  wpm_source: string;
}

export interface ParameterMeta {
  name: string;
  type: string;
  widget: string;
  min?: number;
  max?: number;
  unit?: string;
  description: string;
  values?: Array<{ name: string; description: string }>;
}

export interface ConfigSchema {
  parameters: ParameterMeta[];
}

export type ConfigValues = Record<string, Record<string, number | boolean | string>>;
```

**Step 2: Create api.ts**

Create `components/keyer_webui/frontend/src/lib/api.ts`:

```typescript
import type {
  DeviceStatus,
  SystemStats,
  DecoderStatus,
  TimelineConfig,
  ConfigSchema,
  ConfigValues
} from './types';

class ApiClient {
  private baseUrl: string;

  constructor(baseUrl = '') {
    this.baseUrl = baseUrl;
  }

  private async fetchJson<T>(url: string, options?: RequestInit): Promise<T> {
    const response = await fetch(`${this.baseUrl}${url}`, options);
    if (!response.ok) {
      throw new Error(`API error: ${response.statusText}`);
    }
    return response.json();
  }

  // Status
  async getStatus(): Promise<DeviceStatus> {
    return this.fetchJson('/api/status');
  }

  async getSystemStats(): Promise<SystemStats> {
    return this.fetchJson('/api/system/stats');
  }

  async reboot(): Promise<void> {
    await this.fetchJson('/api/system/reboot', { method: 'POST' });
  }

  // Config
  async getSchema(): Promise<ConfigSchema> {
    return this.fetchJson('/api/config/schema');
  }

  async getConfig(): Promise<ConfigValues> {
    return this.fetchJson('/api/config');
  }

  async setParameter(param: string, value: number | boolean | string): Promise<void> {
    await this.fetchJson('/api/parameter', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ param, value })
    });
  }

  async saveConfig(reboot = false): Promise<void> {
    const url = reboot ? '/api/config/save?reboot=true' : '/api/config/save';
    await this.fetchJson(url, { method: 'POST' });
  }

  // Decoder
  async getDecoderStatus(): Promise<DecoderStatus> {
    return this.fetchJson('/api/decoder/status');
  }

  async setDecoderEnabled(enabled: boolean): Promise<void> {
    await this.fetchJson('/api/decoder/enable', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ enabled })
    });
  }

  // Timeline
  async getTimelineConfig(): Promise<TimelineConfig> {
    return this.fetchJson('/api/timeline/config');
  }

  // SSE helpers
  connectDecoderStream(onChar: (char: string, wpm: number) => void, onWord: () => void): EventSource {
    const es = new EventSource(`${this.baseUrl}/api/decoder/stream`);

    es.addEventListener('char', (e) => {
      const data = JSON.parse(e.data);
      onChar(data.char, data.wpm);
    });

    es.addEventListener('word', () => {
      onWord();
    });

    return es;
  }

  connectTimelineStream(onEvent: (type: string, data: unknown) => void): EventSource {
    const es = new EventSource(`${this.baseUrl}/api/timeline/stream`);

    ['paddle', 'keying', 'decoded', 'gap'].forEach(eventType => {
      es.addEventListener(eventType, (e) => {
        onEvent(eventType, JSON.parse(e.data));
      });
    });

    return es;
  }
}

export const api = new ApiClient();
```

**Step 3: Rebuild frontend**

```bash
cd components/keyer_webui/frontend
npm run build
cd ../../..
python3 components/keyer_webui/scripts/embed_assets.py \
  components/keyer_webui/frontend/dist \
  components/keyer_webui/src/assets.c
```

**Step 4: Commit**

```bash
git add components/keyer_webui/frontend/src/lib/
git add components/keyer_webui/src/assets.c
git commit -m "feat(webui): add TypeScript API client"
```

---

### Task 11: System Page

**Files:**
- Create: `components/keyer_webui/frontend/src/pages/System.svelte`
- Modify: `components/keyer_webui/frontend/src/App.svelte`

**Step 1: Create System.svelte**

Create `components/keyer_webui/frontend/src/pages/System.svelte`:

```svelte
<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { api } from '../lib/api';
  import type { SystemStats, DeviceStatus } from '../lib/types';

  let status = $state<DeviceStatus | null>(null);
  let stats = $state<SystemStats | null>(null);
  let error = $state<string | null>(null);
  let pollInterval: number | null = null;

  async function refresh() {
    try {
      [status, stats] = await Promise.all([
        api.getStatus(),
        api.getSystemStats()
      ]);
      error = null;
    } catch (e) {
      error = e instanceof Error ? e.message : 'Unknown error';
    }
  }

  async function handleReboot() {
    if (confirm('Reboot the device?')) {
      await api.reboot();
    }
  }

  onMount(() => {
    refresh();
    pollInterval = setInterval(refresh, 2000) as unknown as number;
  });

  onDestroy(() => {
    if (pollInterval) clearInterval(pollInterval);
  });
</script>

<div class="system-page">
  <h1>System Status</h1>

  {#if error}
    <div class="error">{error}</div>
  {/if}

  {#if status}
    <section class="card">
      <h2>Network</h2>
      <dl>
        <dt>Mode</dt><dd>{status.mode}</dd>
        <dt>IP</dt><dd>{status.ip}</dd>
        <dt>Ready</dt><dd>{status.ready ? 'Yes' : 'No'}</dd>
      </dl>
    </section>
  {/if}

  {#if stats}
    <section class="card">
      <h2>Uptime</h2>
      <p>{stats.uptime.hours}h {stats.uptime.minutes}m {stats.uptime.seconds}s</p>
    </section>

    <section class="card">
      <h2>Memory</h2>
      <dl>
        <dt>Free</dt><dd>{(stats.heap.free_bytes / 1024).toFixed(1)} KB</dd>
        <dt>Min Free</dt><dd>{(stats.heap.minimum_free_bytes / 1024).toFixed(1)} KB</dd>
        <dt>Largest Block</dt><dd>{(stats.heap.largest_free_block / 1024).toFixed(1)} KB</dd>
      </dl>
    </section>

    <section class="card">
      <h2>Tasks ({stats.tasks.length})</h2>
      <table>
        <thead>
          <tr><th>Name</th><th>State</th><th>Priority</th><th>Stack HWM</th></tr>
        </thead>
        <tbody>
          {#each stats.tasks as task}
            <tr>
              <td>{task.name}</td>
              <td>{task.state}</td>
              <td>{task.priority}</td>
              <td>{task.stack_hwm}</td>
            </tr>
          {/each}
        </tbody>
      </table>
    </section>
  {/if}

  <section class="card">
    <h2>Actions</h2>
    <button onclick={handleReboot}>Reboot</button>
  </section>
</div>

<style>
  .system-page { max-width: 800px; }
  .card { background: #f5f5f5; padding: 1rem; margin: 1rem 0; border-radius: 8px; }
  .error { color: red; padding: 1rem; background: #fee; border-radius: 8px; }
  dl { display: grid; grid-template-columns: auto 1fr; gap: 0.5rem; }
  dt { font-weight: bold; }
  table { width: 100%; border-collapse: collapse; }
  th, td { padding: 0.5rem; text-align: left; border-bottom: 1px solid #ddd; }
  button { padding: 0.5rem 1rem; cursor: pointer; }
</style>
```

**Step 2: Update App.svelte**

Update `components/keyer_webui/frontend/src/App.svelte` to import and use System:

```svelte
<script lang="ts">
  import { onMount } from 'svelte';
  import System from './pages/System.svelte';

  let currentPage = $state('/');

  onMount(() => {
    currentPage = window.location.pathname;
    window.addEventListener('popstate', () => {
      currentPage = window.location.pathname;
    });
  });

  function navigate(path: string) {
    history.pushState({}, '', path);
    currentPage = path;
  }
</script>

<div class="app">
  <nav>
    <a href="/" onclick={(e) => { e.preventDefault(); navigate('/'); }}>Home</a>
    <a href="/config" onclick={(e) => { e.preventDefault(); navigate('/config'); }}>Config</a>
    <a href="/system" onclick={(e) => { e.preventDefault(); navigate('/system'); }}>System</a>
    <a href="/decoder" onclick={(e) => { e.preventDefault(); navigate('/decoder'); }}>Decoder</a>
    <a href="/timeline" onclick={(e) => { e.preventDefault(); navigate('/timeline'); }}>Timeline</a>
  </nav>

  <main>
    {#if currentPage === '/system'}
      <System />
    {:else if currentPage === '/config'}
      <h1>Configuration</h1>
      <p>Config page - coming soon</p>
    {:else if currentPage === '/decoder'}
      <h1>Decoder</h1>
      <p>Decoder page - coming soon</p>
    {:else if currentPage === '/timeline'}
      <h1>Timeline</h1>
      <p>Timeline page - coming soon</p>
    {:else}
      <h1>CW Keyer</h1>
      <p>Welcome to the CW Keyer WebUI</p>
    {/if}
  </main>
</div>

<style>
  .app {
    font-family: system-ui, sans-serif;
    max-width: 1200px;
    margin: 0 auto;
    padding: 1rem;
  }
  nav {
    display: flex;
    gap: 1rem;
    padding: 1rem;
    background: #f0f0f0;
    border-radius: 8px;
    margin-bottom: 1rem;
  }
  nav a {
    color: #333;
    text-decoration: none;
    padding: 0.5rem 1rem;
    border-radius: 4px;
  }
  nav a:hover { background: #ddd; }
  main { padding: 1rem; }
</style>
```

**Step 3: Rebuild and embed**

```bash
cd components/keyer_webui/frontend && npm run build && cd ../../..
python3 components/keyer_webui/scripts/embed_assets.py \
  components/keyer_webui/frontend/dist \
  components/keyer_webui/src/assets.c
idf.py build
```

**Step 4: Commit**

```bash
git add components/keyer_webui/frontend/src/pages/System.svelte
git add components/keyer_webui/frontend/src/App.svelte
git add components/keyer_webui/src/assets.c
git commit -m "feat(webui): add System status page"
```

---

### Task 12: Config Page

**Files:**
- Create: `components/keyer_webui/frontend/src/pages/Config.svelte`
- Modify: `components/keyer_webui/frontend/src/App.svelte`

**Step 1: Create Config.svelte**

Create `components/keyer_webui/frontend/src/pages/Config.svelte`:

```svelte
<script lang="ts">
  import { onMount } from 'svelte';
  import { api } from '../lib/api';
  import type { ConfigSchema, ConfigValues, ParameterMeta } from '../lib/types';

  let schema = $state<ConfigSchema | null>(null);
  let values = $state<ConfigValues>({});
  let error = $state<string | null>(null);
  let saving = $state(false);

  // Group parameters by family (first part of name)
  function groupByFamily(params: ParameterMeta[]): Map<string, ParameterMeta[]> {
    const groups = new Map<string, ParameterMeta[]>();
    for (const p of params) {
      const family = p.name.split('.')[0];
      if (!groups.has(family)) groups.set(family, []);
      groups.get(family)!.push(p);
    }
    return groups;
  }

  function getValue(param: ParameterMeta): number | boolean | string {
    const [family, name] = param.name.split('.');
    return values[family]?.[name] ?? 0;
  }

  async function handleChange(param: ParameterMeta, value: number | boolean | string) {
    try {
      await api.setParameter(param.name, value);
      // Update local state
      const [family, name] = param.name.split('.');
      if (!values[family]) values[family] = {};
      values[family][name] = value;
    } catch (e) {
      error = e instanceof Error ? e.message : 'Failed to set parameter';
    }
  }

  async function handleSave() {
    saving = true;
    try {
      await api.saveConfig();
      error = null;
    } catch (e) {
      error = e instanceof Error ? e.message : 'Failed to save';
    }
    saving = false;
  }

  onMount(async () => {
    try {
      [schema, values] = await Promise.all([
        api.getSchema(),
        api.getConfig()
      ]);
    } catch (e) {
      error = e instanceof Error ? e.message : 'Failed to load config';
    }
  });
</script>

<div class="config-page">
  <h1>Configuration</h1>

  {#if error}
    <div class="error">{error}</div>
  {/if}

  {#if schema}
    {#each [...groupByFamily(schema.parameters)] as [family, params]}
      <section class="card">
        <h2>{family}</h2>
        {#each params as param}
          <div class="param">
            <label for={param.name}>
              {param.name.split('.')[1]}
              {#if param.unit}<span class="unit">({param.unit})</span>{/if}
            </label>

            {#if param.type === 'bool'}
              <input
                type="checkbox"
                id={param.name}
                checked={Boolean(getValue(param))}
                onchange={(e) => handleChange(param, e.currentTarget.checked)}
              />
            {:else if param.values}
              <select
                id={param.name}
                value={getValue(param)}
                onchange={(e) => handleChange(param, Number(e.currentTarget.value))}
              >
                {#each param.values as opt, i}
                  <option value={i}>{opt.name}</option>
                {/each}
              </select>
            {:else}
              <input
                type="number"
                id={param.name}
                value={getValue(param)}
                min={param.min}
                max={param.max}
                onchange={(e) => handleChange(param, Number(e.currentTarget.value))}
              />
            {/if}

            <small>{param.description}</small>
          </div>
        {/each}
      </section>
    {/each}

    <div class="actions">
      <button onclick={handleSave} disabled={saving}>
        {saving ? 'Saving...' : 'Save to NVS'}
      </button>
    </div>
  {/if}
</div>

<style>
  .config-page { max-width: 600px; }
  .card { background: #f5f5f5; padding: 1rem; margin: 1rem 0; border-radius: 8px; }
  .error { color: red; padding: 1rem; background: #fee; border-radius: 8px; }
  .param { margin: 1rem 0; }
  .param label { display: block; font-weight: bold; margin-bottom: 0.25rem; }
  .param .unit { font-weight: normal; color: #666; }
  .param input[type="number"], .param select { width: 100%; padding: 0.5rem; }
  .param small { display: block; color: #666; margin-top: 0.25rem; }
  .actions { margin-top: 2rem; }
  button { padding: 0.75rem 1.5rem; cursor: pointer; }
  button:disabled { opacity: 0.5; }
</style>
```

**Step 2: Update App.svelte imports**

Add Config import and usage in App.svelte:

```svelte
<script lang="ts">
  import { onMount } from 'svelte';
  import System from './pages/System.svelte';
  import Config from './pages/Config.svelte';
  // ... rest same
</script>

<!-- In main section: -->
{#if currentPage === '/system'}
  <System />
{:else if currentPage === '/config'}
  <Config />
<!-- ... -->
```

**Step 3: Rebuild and embed**

```bash
cd components/keyer_webui/frontend && npm run build && cd ../../..
python3 components/keyer_webui/scripts/embed_assets.py \
  components/keyer_webui/frontend/dist \
  components/keyer_webui/src/assets.c
idf.py build
```

**Step 4: Commit**

```bash
git add components/keyer_webui/frontend/src/pages/Config.svelte
git add components/keyer_webui/frontend/src/App.svelte
git add components/keyer_webui/src/assets.c
git commit -m "feat(webui): add Config page with parameter editing"
```

---

### Task 13: Decoder Page with SSE

**Files:**
- Create: `components/keyer_webui/frontend/src/pages/Decoder.svelte`
- Modify: `components/keyer_webui/frontend/src/App.svelte`

**Step 1: Create Decoder.svelte**

Create `components/keyer_webui/frontend/src/pages/Decoder.svelte`:

```svelte
<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { api } from '../lib/api';
  import type { DecoderStatus } from '../lib/types';

  let status = $state<DecoderStatus | null>(null);
  let decodedText = $state('');
  let currentWpm = $state(0);
  let error = $state<string | null>(null);
  let eventSource: EventSource | null = null;

  async function refresh() {
    try {
      status = await api.getDecoderStatus();
      error = null;
    } catch (e) {
      error = e instanceof Error ? e.message : 'Unknown error';
    }
  }

  async function toggleEnabled() {
    if (!status) return;
    try {
      await api.setDecoderEnabled(!status.enabled);
      await refresh();
    } catch (e) {
      error = e instanceof Error ? e.message : 'Failed to toggle decoder';
    }
  }

  function connectStream() {
    eventSource = api.connectDecoderStream(
      (char, wpm) => {
        decodedText += char;
        currentWpm = wpm;
        // Keep last 500 chars
        if (decodedText.length > 500) {
          decodedText = decodedText.slice(-500);
        }
      },
      () => {
        decodedText += ' ';
      }
    );

    eventSource.onerror = () => {
      error = 'SSE connection lost';
    };
  }

  onMount(() => {
    refresh();
    connectStream();
  });

  onDestroy(() => {
    if (eventSource) {
      eventSource.close();
    }
  });
</script>

<div class="decoder-page">
  <h1>Morse Decoder</h1>

  {#if error}
    <div class="error">{error}</div>
  {/if}

  {#if status}
    <section class="card controls">
      <label>
        <input type="checkbox" checked={status.enabled} onchange={toggleEnabled} />
        Decoder Enabled
      </label>
      <span class="wpm">WPM: {currentWpm || status.wpm}</span>
    </section>
  {/if}

  <section class="card output">
    <h2>Decoded Text</h2>
    <pre>{decodedText || '(waiting for CW...)'}</pre>
    {#if status?.pattern}
      <div class="pattern">Current: {status.pattern}</div>
    {/if}
  </section>

  <button onclick={() => { decodedText = ''; }}>Clear</button>
</div>

<style>
  .decoder-page { max-width: 800px; }
  .card { background: #f5f5f5; padding: 1rem; margin: 1rem 0; border-radius: 8px; }
  .error { color: red; padding: 1rem; background: #fee; border-radius: 8px; }
  .controls { display: flex; justify-content: space-between; align-items: center; }
  .wpm { font-size: 1.2rem; font-weight: bold; }
  .output pre {
    background: #222;
    color: #0f0;
    padding: 1rem;
    font-family: monospace;
    font-size: 1.2rem;
    min-height: 200px;
    white-space: pre-wrap;
    word-wrap: break-word;
  }
  .pattern { color: #666; margin-top: 0.5rem; }
  button { padding: 0.5rem 1rem; cursor: pointer; }
</style>
```

**Step 2: Update App.svelte**

Add Decoder import and route.

**Step 3: Rebuild, embed, build**

**Step 4: Commit**

```bash
git add components/keyer_webui/frontend/src/pages/Decoder.svelte
git add components/keyer_webui/frontend/src/App.svelte
git add components/keyer_webui/src/assets.c
git commit -m "feat(webui): add Decoder page with SSE stream"
```

---

## Phase 3: Integration

### Task 14: Main Integration

**Files:**
- Modify: `main/main.c`

**Step 1: Add webui init to main.c**

Add includes:

```c
#include "webui.h"
#include "wifi.h"
```

After WiFi init, add WebUI init:

```c
    /* Initialize WiFi */
    wifi_config_app_t wifi_cfg = WIFI_CONFIG_DEFAULT;
    // ... load from g_config ...
    wifi_app_init(&wifi_cfg);
    wifi_app_start();

    /* Wait for WiFi */
    while (wifi_get_state() == WIFI_STATE_CONNECTING) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    /* Start WebUI if WiFi ready */
    if (wifi_get_state() == WIFI_STATE_CONNECTED ||
        wifi_get_state() == WIFI_STATE_AP_MODE) {
        webui_init();
        webui_start();
        ESP_LOGI(TAG, "WebUI started");
    }
```

**Step 2: Add CMake dependency**

In `main/CMakeLists.txt`, add `keyer_webui` to REQUIRES.

**Step 3: Build and test**

```bash
idf.py build
idf.py flash monitor
```

**Step 4: Commit**

```bash
git add main/main.c main/CMakeLists.txt
git commit -m "feat(main): integrate WebUI with WiFi"
```

---

### Task 15: BG Task SSE Integration

**Files:**
- Modify: `main/bg_task.c`

**Step 1: Add decoder SSE push**

Add to bg_task.c:

```c
#include "webui.h"

// In bg_task loop, after decoder_process():
    decoded_char_t last = decoder_get_last_char();
    static char prev_char = '\0';
    if (last.character != '\0' && last.character != prev_char) {
        webui_decoder_push_char(last.character, (uint8_t)decoder_get_wpm());
        if (last.character == ' ') {
            webui_decoder_push_word();
        }
        prev_char = last.character;
    }
```

**Step 2: Build and test**

```bash
idf.py build
```

**Step 3: Commit**

```bash
git add main/bg_task.c
git commit -m "feat(bg_task): push decoder events to WebUI SSE"
```

---

## Summary

**Total Tasks: 15**

**Phase 1 (Backend): Tasks 1-8**
- Component structure
- Asset embedding
- Static serving
- Config API
- System API
- Decoder API
- SSE infrastructure

**Phase 2 (Frontend): Tasks 9-13**
- Svelte scaffold
- API client
- System page
- Config page
- Decoder page

**Phase 3 (Integration): Tasks 14-15**
- Main init
- BG task SSE

---

**Plan complete and saved to `docs/plans/2025-12-26-webui-implementation.md`. Two execution options:**

**1. Subagent-Driven (this session)** - I dispatch fresh subagent per task, review between tasks, fast iteration

**2. Parallel Session (separate)** - Open new session with executing-plans, batch execution with checkpoints

**Which approach?**
