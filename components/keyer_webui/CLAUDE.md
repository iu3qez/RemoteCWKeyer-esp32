<!-- BEGIN treecode (auto) — do not edit inside this block -->
# keyer_webui — HTTP + WebSocket backend for the web UI (Core 1)

Responsibility: Owns the on-device HTTP server that serves the embedded single-page app
and the JSON REST API, plus a WebSocket endpoint for real-time streaming. It is a
user-facing interface running on Core 1 — it reads/writes config and queries subsystems,
but must never sit on or block the hard RT path. Rendering/state lives in frontend/.

Key abstractions:
- `webui_init()` / `webui_start()` / `webui_stop()` — lifecycle (esp_http_server).
- REST handlers in api_*.c: `/api/config`, `/api/config/schema`, `/api/parameter`,
  `/api/config/save`, `/api/status`, `/api/system/{stats,reboot}`, `/api/decoder/*`,
  `/api/timeline/config`, `/api/text/*` (send/status/abort/pause/resume/memory/play),
  `/api/vpn/status`.
- Static/SPA serving (http_server.c): exact-asset match else SPA_ROUTES → index.html.
- ws_server.c: single `/ws` endpoint (max 8 clients); `ws_broadcast*` push decoder,
  pattern, and timeline events. `webui_*_push` / `webui_timeline_push` are the producer
  entry points called from other subsystems.

Depends on: esp_http_server, esp_timer, keyer_config, keyer_core, keyer_cwnet,
keyer_decoder, keyer_wifi, keyer_vpn, keyer_text (REQUIRES).

Used by: main.c / bg_task.c (start server); decoder and timeline subsystems call the
webui_*_push broadcast entry points.

External deps of note: cJSON comes from espressif/cjson (declared in idf_component.yml),
NOT the old ESP-IDF `json` component — it was removed in v6, so do NOT add `json` to
REQUIRES. Handlers `#include "cJSON.h"` and build JSON with cJSON_Create*/AddItem*.

Conventions: Built strict — -Wall -Wextra -Werror -Wconversion -Wsign-conversion. The
config schema is served as a precompiled CONFIG_SCHEMA_JSON string (no runtime build).

Gotchas: The frontend is built at CMake time (npm install && vite build) and embedded
into the generated src/assets.c via scripts/embed_assets.py — assets are gzipped and
served from flash, there is no filesystem. New client routes must be added to both the
Svelte router and the SPA_ROUTES list in http_server.c or a deep-link 404s.
<!-- END treecode (auto) -->
