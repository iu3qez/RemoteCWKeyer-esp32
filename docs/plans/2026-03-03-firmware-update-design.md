# Firmware Update via Web UI — Design Document

**Date:** 2026-03-03
**Status:** Approved

## Overview

Add firmware update capabilities to the Web UI: OTA upload from browser, OTA download from remote URL, and UF2/ROM bootloader reboot for USB flashing. Includes automatic rollback if the new firmware fails to bring up the network stack.

## Requirements

1. **OTA upload** — user uploads .bin file from the Web UI
2. **OTA from URL** — user provides a URL, device downloads and flashes
3. **UF2 reboot** — button to reboot into ROM bootloader for USB update
4. **Validation** — ESP-IDF standard header check (magic byte, chip ID, size)
5. **Rollback** — automatic rollback if network stack (STA or AP) doesn't come up within configurable timeout
6. **Transport** — HTTP and HTTPS without restrictions (user's choice)

## Architecture

### Approach

New `api_firmware.c` inside `keyer_webui/src/` following the existing `api_*.c` pattern. No new component — OTA logic lives in the HTTP handlers. Frontend page `Firmware.svelte`.

### API Endpoints

| Method | Endpoint | Purpose |
|--------|----------|---------|
| `GET` | `/api/firmware/status` | Current version, active partition, OTA state, rollback available |
| `POST` | `/api/firmware/upload` | Chunked upload of .bin, writes to inactive OTA partition |
| `POST` | `/api/firmware/url` | Body: `{"url": "..."}`, downloads and flashes |
| `POST` | `/api/firmware/rollback` | Revert to previous partition |
| `POST` | `/api/firmware/uf2` | Reboot to ROM bootloader (USB download mode) |
| `POST` | `/api/firmware/confirm` | Manually call `esp_ota_mark_app_valid_and_cancel_rollback()` |

### Backend (`api_firmware.c`)

**State management:** Single `s_ota` struct protected by FreeRTOS mutex (all access on Core 1, no RT path impact):

```c
typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_UPLOADING,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_FLASHING,
    OTA_STATE_DONE,
    OTA_STATE_ERROR,
} ota_state_t;

static struct {
    SemaphoreHandle_t   mutex;
    ota_state_t         state;
    int                 progress;       // 0-100
    size_t              bytes_written;
    size_t              total_size;
    char                error_msg[128];
    esp_ota_handle_t    ota_handle;
    const esp_partition_t *target_partition;
} s_ota;
```

**Upload flow:**
1. Handler receives body chunk-by-chunk via `httpd_req_recv()`
2. First chunk: `esp_ota_begin()` on inactive partition
3. Each chunk: `esp_ota_write()`, update progress
4. Final: `esp_ota_end()`, `esp_ota_set_boot_partition()`
5. Respond with success, frontend triggers reboot

**URL download flow:**
1. Handler parses URL from JSON body
2. Spawns FreeRTOS task on Core 1
3. Task uses `esp_http_client` to download chunk-by-chunk
4. Same OTA write logic as upload
5. Progress available via `GET /api/firmware/status` (polling)

**Concurrency:** If `s_ota.state != OTA_STATE_IDLE`, reject with HTTP 409 Conflict.

### Frontend (`Firmware.svelte`)

Three sections:

1. **Version info** — firmware version, build date, active partition, rollback available
2. **Update methods:**
   - Drag-and-drop / file picker for .bin upload
   - URL text field + "Download & Flash" button
   - "Reboot to USB mode" button with confirmation dialog
3. **Progress & actions:**
   - Progress bar during upload/download (polling `/api/firmware/status` every 500ms)
   - Status messages: idle → uploading → flashing → rebooting
   - "Rollback" button (visible only when rollback is available) with confirmation dialog

Style: CRT green phosphor theme consistent with the rest of the UI.

### Rollback Mechanism

**Trigger:** Network stack not active (neither STA with IP nor AP started) within configurable timeout.

**Flow at boot after OTA:**
```
main.c boot
  → esp_ota_get_state_info(running_partition)
  → If state == ESP_OTA_IMG_PENDING_VERIFY:
      → Start timer (ota_confirm_timeout_s, default 60s)
      → Monitor: WiFi STA got IP  OR  WiFi AP started
      → If network active before timeout:
          → esp_ota_mark_app_valid_and_cancel_rollback()
          → ESP_LOGI("OTA confirmed — network active")
      → If timeout expires:
          → Do NOT call mark_valid
          → ESP_LOGW("OTA not confirmed — rollback on next reboot")
          → esp_restart()  // triggers bootloader rollback
```

**New parameter in `parameters.yaml`:**
```yaml
ota_confirm_timeout_s:
  type: uint16
  default: 60
  min: 10
  max: 300
  group: system
  label: "OTA confirm timeout (seconds)"
  description: "Max time to confirm OTA update before automatic rollback"
```

### ESP-IDF Dependencies

- `esp_ota_ops` — OTA partition management
- `esp_http_client` — HTTP/HTTPS download for URL-based OTA
- `esp_app_format` — `esp_app_desc_t` for version info
- `app_update` — rollback utilities

### sdkconfig Changes

```
CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y
```

## Files to Create/Modify

| File | Action |
|------|--------|
| `components/keyer_webui/src/api_firmware.c` | **Create** — OTA handlers |
| `components/keyer_webui/frontend/src/pages/Firmware.svelte` | **Create** — UI page |
| `components/keyer_webui/frontend/src/App.svelte` | **Modify** — add Firmware route + import |
| `components/keyer_webui/src/http_server.c` | **Modify** — register `/api/firmware/*` endpoints |
| `components/keyer_webui/CMakeLists.txt` | **Modify** — add ESP-IDF deps |
| `main/main.c` | **Modify** — OTA confirmation logic at boot |
| `parameters.yaml` | **Modify** — add `ota_confirm_timeout_s` |
| `sdkconfig.defaults` | **Modify** — allow HTTP for OTA |

## Non-Goals

- Secure Boot / firmware signing (can be added later)
- GitHub release listing / auto-update check
- Delta/incremental updates
