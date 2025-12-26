# WebUI Design

## Overview

WebUI completa per il CW keyer ESP32, con parità funzionale rispetto al riferimento in `tmp/webui/`.

**Stack:**
- Frontend: Svelte 5, compilato e embedded nel firmware
- Backend: `esp_http_server` (thread pool default), API REST + SSE
- Comunicazione: HTTP polling per config/status, SSE per timeline/decoder

## Architettura

### Componente: `components/keyer_webui/`

```
keyer_webui/
├── CMakeLists.txt
├── include/
│   └── webui.h                 # webui_init(), webui_start(), webui_stop()
├── src/
│   ├── http_server.c           # Server lifecycle, routing, asset serving
│   ├── api_config.c            # Config schema/values/save
│   ├── api_keyer.c             # Text send, memory messages, abort
│   ├── api_system.c            # Stats, status, reboot
│   ├── api_decoder.c           # Decoder status + SSE stream
│   ├── api_timeline.c          # Timeline events + SSE stream
│   └── assets_generated.c      # [GENERATED] Embedded HTML/JS/CSS
├── frontend/                   # Svelte 5 source
│   ├── src/
│   ├── package.json
│   └── vite.config.ts
└── scripts/
    └── embed_assets.py         # Build frontend → C arrays
```

### Dipendenze

```
keyer_webui
    ├── keyer_config      (schema, get/set values, NVS save)
    ├── keyer_core        (stream, consumer for timeline)
    ├── keyer_decoder     (status, enable/disable)
    ├── keyer_text        (send, memories, abort, status)
    └── keyer_logging     (opzionale, per debug)
```

## API Endpoints

### Config API

| Method | Endpoint | Descrizione |
|--------|----------|-------------|
| GET | `/api/config/schema` | Schema JSON parametri (generato a build time) |
| GET | `/api/config` | Valori attuali tutti i parametri |
| POST | `/api/parameter` | Modifica singolo parametro `{param, value}` |
| POST | `/api/config/save` | Salva in NVS (opzionale `?reboot=true`) |

### Keyer API

| Method | Endpoint | Descrizione |
|--------|----------|-------------|
| GET | `/api/keyer/status` | Stato: idle/sending, progress, wpm |
| POST | `/api/keyer/send` | Invia testo `{text, wpm}` |
| POST | `/api/keyer/message` | Invia memory slot `{message: 1-8}` |
| POST | `/api/keyer/abort` | Interrompi trasmissione |

### System API

| Method | Endpoint | Descrizione |
|--------|----------|-------------|
| GET | `/api/status` | Stato WiFi: mode, IP, ready |
| GET | `/api/system/stats` | Uptime, heap, task list |
| POST | `/api/system/reboot` | Riavvia dispositivo |

### Decoder API

| Method | Endpoint | Descrizione |
|--------|----------|-------------|
| GET | `/api/decoder/status` | Stato: enabled, wpm, text buffer |
| POST | `/api/decoder/enable` | Abilita/disabilita `{enabled: bool}` |
| GET | `/api/decoder/stream` | **SSE** - stream caratteri decodificati |

### Timeline API

| Method | Endpoint | Descrizione |
|--------|----------|-------------|
| GET | `/api/timeline/config` | WPM corrente e sorgente |
| GET | `/api/timeline/stream` | **SSE** - stream eventi keying real-time |

### Asset Serving

| Method | Endpoint | Descrizione |
|--------|----------|-------------|
| GET | `/` | `index.html` (SPA entry point) |
| GET | `/*` | Asset statici (JS, CSS) con Content-Type corretto |

## Server-Sent Events (SSE)

### Protocollo

Formato standard HTTP con `Content-Type: text/event-stream`:

```
event: <event-type>
data: <json-payload>

```

(Doppio newline separa gli eventi)

### Timeline SSE (`/api/timeline/stream`)

```
event: paddle
data: {"ts":1234567890,"paddle":0,"state":1}

event: keying
data: {"ts":1234567890,"element":0,"state":1}

event: decoded
data: {"ts":1234567890,"char":"E"}

event: gap
data: {"ts":1234567890,"type":1}
```

Dove:
- `ts`: timestamp in microsecondi
- `paddle`: 0=dit, 1=dah
- `element`: 0=dit, 1=dah
- `state`: 1=down, 0=up
- `type`: 0=element, 1=char, 2=word

**Heartbeat** (ogni 5s se nessun evento):
```
: keepalive

```

### Decoder SSE (`/api/decoder/stream`)

```
event: char
data: {"char":"E","wpm":25}

event: word
data: {}

event: status
data: {"enabled":true,"wpm":25}
```

### Backend Implementation

```c
typedef struct {
    httpd_req_t *req;
    int fd;
    bool active;
} sse_client_t;

#define MAX_SSE_CLIENTS 4

static sse_client_t timeline_clients[MAX_SSE_CLIENTS];
static sse_client_t decoder_clients[MAX_SSE_CLIENTS];
```

## Build System

### Pipeline

```
frontend/src/*.svelte
        ↓
   npm run build (Vite)
        ↓
frontend/dist/
├── index.html
├── assets/
│   ├── app.js
│   └── app.css
        ↓
   scripts/embed_assets.py
        ↓
src/assets_generated.c
```

### Vite Config

```typescript
export default defineConfig({
  build: {
    outDir: 'dist',
    assetsInlineLimit: 0,
    rollupOptions: {
      output: {
        entryFileNames: 'assets/app.js',
        chunkFileNames: 'assets/[name].js',
        assetFileNames: 'assets/[name].[ext]'
      }
    }
  }
});
```

### Generated Assets

`assets_generated.c`:

```c
#include "assets_generated.h"

static const uint8_t asset_index_html[] = { 0x1f, 0x8b, ... };
static const size_t asset_index_html_len = 1234;

static const uint8_t asset_app_js[] = { 0x1f, 0x8b, ... };
static const size_t asset_app_js_len = 5678;

static const uint8_t asset_app_css[] = { 0x1f, 0x8b, ... };
static const size_t asset_app_css_len = 890;

const webui_asset_t webui_assets[] = {
    {"/", "text/html", asset_index_html, asset_index_html_len, true},
    {"/index.html", "text/html", asset_index_html, asset_index_html_len, true},
    {"/assets/app.js", "application/javascript", asset_app_js, asset_app_js_len, true},
    {"/assets/app.css", "text/css", asset_app_css, asset_app_css_len, true},
    {NULL, NULL, NULL, 0, false}
};
```

`assets_generated.h`:

```c
typedef struct {
    const char *path;
    const char *content_type;
    const uint8_t *data;
    size_t length;
    bool gzipped;
} webui_asset_t;

extern const webui_asset_t webui_assets[];
extern const size_t webui_assets_count;
```

### Serving with Gzip

```c
static esp_err_t serve_asset(httpd_req_t *req, const webui_asset_t *asset) {
    httpd_resp_set_type(req, asset->content_type);
    if (asset->gzipped) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    return httpd_resp_send(req, (const char *)asset->data, asset->length);
}
```

## SPA Routing

### Server-Side Fallback

```c
static const char *spa_routes[] = {
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
    for (int i = 0; spa_routes[i] != NULL; i++) {
        if (strcmp(uri, spa_routes[i]) == 0) {
            return true;
        }
    }
    return false;
}

static esp_err_t handle_spa_route(httpd_req_t *req) {
    const webui_asset_t *index = webui_find_asset("/index.html");
    if (index == NULL) {
        return httpd_resp_send_404(req);
    }
    return serve_asset(req, index);
}
```

### Frontend Routing (Svelte)

```svelte
<script lang="ts">
  import { onMount } from 'svelte';

  let currentPage = '/';

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

{#if currentPage === '/config'}
  <Config />
{:else if currentPage === '/keyer'}
  <Keyer />
{:else}
  <Home />
{/if}
```

## Integration

### Public API (`webui.h`)

```c
#ifndef WEBUI_H
#define WEBUI_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t webui_init(void);
esp_err_t webui_start(void);
esp_err_t webui_stop(void);

void webui_timeline_push_event(const char *event_type, const char *json);
void webui_decoder_push_char(char c, uint8_t wpm);
void webui_decoder_push_word(void);

#endif
```

### Init Sequence

```c
void app_main(void) {
    // 1. Early init
    nvs_flash_init();
    config_init();
    rt_log_init();

    // 2. Hardware init
    hal_gpio_init();
    hal_audio_init();

    // 3. RT task (Core 0)
    xTaskCreatePinnedToCore(rt_task, "rt_task", 4096, NULL,
                            configMAX_PRIORITIES - 1, NULL, 0);

    // 4. WiFi init
    wifi_init();

    // 5. WebUI init (after WiFi)
    webui_init();
    webui_start();

    // 6. BG task (Core 1)
    xTaskCreatePinnedToCore(bg_task, "bg_task", 8192, NULL,
                            tskIDLE_PRIORITY + 2, NULL, 1);

    // 7. Console
    console_init();
}
```

### BG Task Integration

```c
#include "webui.h"

void bg_task(void *arg) {
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        text_keyer_tick(&g_text_keyer);
        decoder_tick(&g_decoder);

        // Push decoder events to SSE
        char decoded_char;
        while (decoder_get_char(&g_decoder, &decoded_char)) {
            webui_decoder_push_char(decoded_char, decoder_get_wpm(&g_decoder));
        }
        if (decoder_word_boundary(&g_decoder)) {
            webui_decoder_push_word();
        }

        // Push timeline events to SSE
        stream_sample_t sample;
        while (consumer_try_read(&g_timeline_consumer, &sample)) {
            char json[64];
            format_timeline_event(&sample, json, sizeof(json));
            webui_timeline_push_event(sample_event_type(&sample), json);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
    }
}
```

### Component CMakeLists.txt

```cmake
idf_component_register(
    SRCS
        "src/http_server.c"
        "src/api_config.c"
        "src/api_keyer.c"
        "src/api_system.c"
        "src/api_decoder.c"
        "src/api_timeline.c"
        "src/assets_generated.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_http_server
        json
        keyer_config
        keyer_core
        keyer_decoder
        keyer_text
)
```
