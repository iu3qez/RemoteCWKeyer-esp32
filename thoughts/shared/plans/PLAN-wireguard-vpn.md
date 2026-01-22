# Implementation Plan: WireGuard VPN Support

Generated: 2026-01-21

## Goal

Add WireGuard VPN support to the ESP32-S3 CW keyer, enabling secure remote access to the device over untrusted networks. The VPN will run on Core 1 as a best-effort network service with zero impact on the hard RT keying path on Core 0.

## Research Summary

Based on prior research:
- **Library**: `trombik/esp_wireguard` from ESP Component Registry
- **Memory footprint**: ~20-30KB heap (acceptable for ESP32-S3)
- **Critical dependency**: NTP time sync MUST succeed before WireGuard handshake
- **Limitations**: Single peer only, avoid PSK (memory overhead), IPv4 only
- **ESP-IDF compatibility**: Works with ESP-IDF 5.x

## Existing Codebase Analysis

### Patterns to Follow

| Pattern | Source | Apply To |
|---------|--------|----------|
| Component structure | `keyer_wifi/` | `keyer_vpn/` |
| State machine with atomic | `wifi.c` (wifi_state_t) | `vpn.c` (vpn_state_t) |
| Config family | `parameters.yaml` wifi family | New vpn family |
| Console commands | `commands.c` | VPN status/control |
| BG task monitoring | `bg_task.c` wifi_get_state() | vpn_get_state() |
| Initialization order | `main.c` after WiFi, before WebUI | Same slot |

### Key Integration Points

1. **parameters.yaml** - Add `vpn` family after `wifi` (order: 8)
2. **main.c** - Initialize after WiFi connected, before WebUI
3. **bg_task.c** - Monitor state for LED feedback, reconnection logic
4. **commands.c** - Add `vpn` command with status/connect/disconnect
5. **WebUI** - Add VPN panel to Config.svelte or dedicated page

---

## Implementation Phases

### Phase 1: Configuration Schema (Complexity: Low)

**Files to modify:**
- `/home/sf/src/RustRemoteCWKeyer/parameters.yaml` - Add vpn family

**Steps:**

1. Add `vpn` family to parameters.yaml after `wifi` (order: 8):
```yaml
vpn:
  order: 8
  icon: "shield"
  label:
    en: "VPN"
  description:
    en: "WireGuard VPN tunnel"
  aliases: [wg]

  parameters:
    enabled:
      type: bool
      default: false
      nvs_key: "vpn_en"
      runtime_change: reboot
      priority: 60

    server_endpoint:
      type: string
      max_length: 64
      default: ""
      nvs_key: "vpn_ep"
      runtime_change: reboot
      priority: 61

    server_port:
      type: u16
      default: 51820
      range: [1, 65535]
      nvs_key: "vpn_port"
      runtime_change: reboot
      priority: 62

    server_public_key:
      type: string
      max_length: 48  # Base64 WireGuard key + null
      default: ""
      nvs_key: "vpn_spub"
      runtime_change: reboot
      sensitive: true
      priority: 63

    client_private_key:
      type: string
      max_length: 48
      default: ""
      nvs_key: "vpn_cpriv"
      runtime_change: reboot
      sensitive: true
      priority: 64

    client_address:
      type: string
      max_length: 18  # "255.255.255.255/32" + null
      default: "10.0.0.2/24"
      nvs_key: "vpn_addr"
      runtime_change: reboot
      priority: 65

    allowed_ips:
      type: string
      max_length: 64
      default: "0.0.0.0/0"
      nvs_key: "vpn_aips"
      runtime_change: reboot
      priority: 66

    persistent_keepalive:
      type: u16
      default: 25
      range: [0, 3600]
      nvs_key: "vpn_ka"
      runtime_change: reboot
      priority: 67
```

2. Run config generation:
```bash
python scripts/gen_config_c.py
```

**Acceptance criteria:**
- [ ] `vpn` family appears in generated config.h
- [ ] NVS keys are properly generated
- [ ] Console `show vpn.*` works after build

---

### Phase 2: VPN Component Skeleton (Complexity: Medium)

**Files to create:**
- `/home/sf/src/RustRemoteCWKeyer/components/keyer_vpn/CMakeLists.txt`
- `/home/sf/src/RustRemoteCWKeyer/components/keyer_vpn/include/vpn.h`
- `/home/sf/src/RustRemoteCWKeyer/components/keyer_vpn/src/vpn.c`
- `/home/sf/src/RustRemoteCWKeyer/components/keyer_vpn/idf_component.yml`

**Steps:**

1. Create `CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "src/vpn.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_netif esp_event nvs_flash esp_timer
    PRIV_REQUIRES esp_wireguard
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Werror -Wconversion -Wshadow
)
```

2. Create `idf_component.yml` for ESP Component Registry dependency:
```yaml
dependencies:
  trombik/esp_wireguard: "^0.4.0"
```

3. Create `vpn.h` header following wifi.h pattern:
```c
typedef enum {
    VPN_STATE_DISABLED = 0,
    VPN_STATE_WAITING_WIFI,
    VPN_STATE_WAITING_TIME,
    VPN_STATE_CONNECTING,
    VPN_STATE_CONNECTED,
    VPN_STATE_FAILED,
} vpn_state_t;

typedef struct {
    bool enabled;
    char server_endpoint[65];
    uint16_t server_port;
    char server_public_key[49];
    char client_private_key[49];
    char client_address[19];
    char allowed_ips[65];
    uint16_t persistent_keepalive;
} vpn_config_app_t;

esp_err_t vpn_app_init(const vpn_config_app_t *config);
esp_err_t vpn_app_start(void);
void vpn_app_stop(void);
vpn_state_t vpn_get_state(void);
bool vpn_is_connected(void);
const char *vpn_get_state_str(vpn_state_t state);
```

4. Create `vpn.c` with state machine (detailed implementation in Phase 3)

**Acceptance criteria:**
- [ ] Component compiles with strict warnings
- [ ] esp_wireguard dependency resolves
- [ ] Header is includable from main.c

---

### Phase 3: NTP Time Sync (Complexity: Medium-High)

**Risk area**: WireGuard handshake WILL FAIL without valid system time.

**Files to modify:**
- `/home/sf/src/RustRemoteCWKeyer/components/keyer_vpn/src/vpn.c`

**Steps:**

1. Add SNTP initialization (one-shot, not persistent):
```c
#include "esp_sntp.h"

static bool wait_for_time_sync(uint32_t timeout_ms) {
    ESP_LOGI(TAG, "Starting NTP time sync...");

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    time_t now = 0;
    struct tm timeinfo = {0};
    uint32_t elapsed = 0;

    while (timeinfo.tm_year < (2020 - 1900) && elapsed < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(100));
        elapsed += 100;
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    esp_sntp_stop();

    if (timeinfo.tm_year >= (2020 - 1900)) {
        ESP_LOGI(TAG, "Time synced: %d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        return true;
    }

    ESP_LOGW(TAG, "Time sync failed after %lums", (unsigned long)elapsed);
    return false;
}
```

2. Call time sync before WireGuard connect:
```c
if (!wait_for_time_sync(10000)) {  /* 10s timeout */
    atomic_store(&s_vpn.state, VPN_STATE_FAILED);
    return;
}
```

**Acceptance criteria:**
- [ ] Time sync completes within 10 seconds on normal network
- [ ] Graceful failure if NTP unreachable
- [ ] VPN state transitions to WAITING_TIME during sync

---

### Phase 4: WireGuard Integration (Complexity: High)

**Files to modify:**
- `/home/sf/src/RustRemoteCWKeyer/components/keyer_vpn/src/vpn.c`

**Steps:**

1. Initialize WireGuard context:
```c
#include "esp_wireguard.h"

static wireguard_config_t s_wg_config;
static wireguard_ctx_t s_wg_ctx;

static esp_err_t setup_wireguard(void) {
    memset(&s_wg_config, 0, sizeof(s_wg_config));

    s_wg_config.private_key = s_vpn.config.client_private_key;
    s_wg_config.listen_port = 0;  /* Random local port */
    s_wg_config.public_key = s_vpn.config.server_public_key;
    s_wg_config.allowed_ip = "0.0.0.0";
    s_wg_config.allowed_ip_mask = "0.0.0.0";
    s_wg_config.endpoint = s_vpn.config.server_endpoint;
    s_wg_config.port = s_vpn.config.server_port;
    s_wg_config.persistent_keepalive = s_vpn.config.persistent_keepalive;

    return esp_wireguard_init(&s_wg_config, &s_wg_ctx);
}
```

2. Create netif for tunnel:
```c
static esp_netif_t *create_vpn_netif(void) {
    esp_netif_inherent_config_t base_cfg = ESP_NETIF_INHERENT_DEFAULT_PPP();
    base_cfg.if_key = "VPN";
    base_cfg.if_desc = "WireGuard VPN";

    esp_netif_config_t cfg = {
        .base = &base_cfg,
        .driver = NULL,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_PPP,
    };

    return esp_netif_new(&cfg);
}
```

3. Connection task (pinned to Core 1):
```c
static void vpn_connection_task(void *arg) {
    /* Wait for WiFi */
    while (!wifi_is_connected()) {
        atomic_store(&s_vpn.state, VPN_STATE_WAITING_WIFI);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    /* Sync time */
    atomic_store(&s_vpn.state, VPN_STATE_WAITING_TIME);
    if (!wait_for_time_sync(10000)) {
        atomic_store(&s_vpn.state, VPN_STATE_FAILED);
        goto cleanup;
    }

    /* Connect */
    atomic_store(&s_vpn.state, VPN_STATE_CONNECTING);
    esp_err_t ret = esp_wireguard_connect(&s_wg_ctx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WireGuard connect failed: %s", esp_err_to_name(ret));
        atomic_store(&s_vpn.state, VPN_STATE_FAILED);
        goto cleanup;
    }

    atomic_store(&s_vpn.state, VPN_STATE_CONNECTED);
    ESP_LOGI(TAG, "VPN connected");

cleanup:
    vTaskDelete(NULL);
}
```

**Acceptance criteria:**
- [ ] WireGuard handshake succeeds with valid keys
- [ ] Tunnel interface receives IP from allowed_ips
- [ ] Ping through tunnel works
- [ ] Graceful failure on invalid keys

---

### Phase 5: Main Integration (Complexity: Low)

**Files to modify:**
- `/home/sf/src/RustRemoteCWKeyer/main/main.c`
- `/home/sf/src/RustRemoteCWKeyer/main/bg_task.c`

**Steps:**

1. Add VPN init to main.c after WiFi, before WebUI:
```c
#include "vpn.h"

/* In app_main(), after wifi_app_start() */
if (atomic_load_explicit(&g_config.vpn.enabled, memory_order_relaxed)) {
    ESP_LOGI(TAG, "VPN enabled, initializing...");
    vpn_config_app_t vpn_cfg = {
        .enabled = true,
        .server_port = atomic_load_explicit(&g_config.vpn.server_port, memory_order_relaxed),
        .persistent_keepalive = atomic_load_explicit(&g_config.vpn.persistent_keepalive, memory_order_relaxed),
    };
    strncpy(vpn_cfg.server_endpoint, g_config.vpn.server_endpoint, sizeof(vpn_cfg.server_endpoint) - 1);
    strncpy(vpn_cfg.server_public_key, g_config.vpn.server_public_key, sizeof(vpn_cfg.server_public_key) - 1);
    strncpy(vpn_cfg.client_private_key, g_config.vpn.client_private_key, sizeof(vpn_cfg.client_private_key) - 1);
    strncpy(vpn_cfg.client_address, g_config.vpn.client_address, sizeof(vpn_cfg.client_address) - 1);
    strncpy(vpn_cfg.allowed_ips, g_config.vpn.allowed_ips, sizeof(vpn_cfg.allowed_ips) - 1);

    ret = vpn_app_init(&vpn_cfg);
    if (ret == ESP_OK) {
        vpn_app_start();
    } else {
        ESP_LOGE(TAG, "VPN init failed: %s", esp_err_to_name(ret));
    }
}
```

2. Add VPN state monitoring to bg_task.c:
```c
#include "vpn.h"

/* In bg_task main loop */
static vpn_state_t prev_vpn_state = VPN_STATE_DISABLED;
vpn_state_t vs = vpn_get_state();
if (vs != prev_vpn_state) {
    RT_INFO(&g_bg_log_stream, now_us, "VPN state: %s", vpn_get_state_str(vs));
    /* Could add LED state for VPN, or overlay on existing states */
    prev_vpn_state = vs;
}
```

**Acceptance criteria:**
- [ ] VPN auto-starts when enabled and WiFi connected
- [ ] State changes logged
- [ ] No impact on RT task timing

---

### Phase 6: Console Commands (Complexity: Low)

**Files to modify:**
- `/home/sf/src/RustRemoteCWKeyer/components/keyer_console/src/commands.c`

**Steps:**

1. Add VPN command handler:
```c
static console_error_t cmd_vpn(const console_parsed_cmd_t *cmd) {
    if (cmd->argc == 0) {
        /* Show status */
        vpn_state_t state = vpn_get_state();
        printf("VPN: %s\r\n", vpn_get_state_str(state));
        if (state == VPN_STATE_CONNECTED) {
            printf("Tunnel IP: %s\r\n", g_config.vpn.client_address);
            printf("Endpoint: %s:%u\r\n",
                   g_config.vpn.server_endpoint,
                   atomic_load_explicit(&g_config.vpn.server_port, memory_order_relaxed));
        }
        return CONSOLE_OK;
    }

    const char *arg = cmd->args[0];

    if (strcmp(arg, "connect") == 0) {
        vpn_app_start();
        printf("VPN connecting...\r\n");
        return CONSOLE_OK;
    }

    if (strcmp(arg, "disconnect") == 0) {
        vpn_app_stop();
        printf("VPN disconnected\r\n");
        return CONSOLE_OK;
    }

    return CONSOLE_ERR_INVALID_VALUE;
}

static const char USAGE_VPN[] =
    "  vpn               Show VPN status\r\n"
    "  vpn connect       Start VPN connection\r\n"
    "  vpn disconnect    Stop VPN\r\n"
    "\r\n"
    "Configure with: set vpn.<param> <value>";
```

2. Add to command registry:
```c
{ "vpn",  "WireGuard VPN control",  USAGE_VPN,  cmd_vpn },
```

**Acceptance criteria:**
- [ ] `vpn` shows current state
- [ ] `vpn connect` initiates connection
- [ ] `vpn disconnect` cleanly tears down tunnel
- [ ] `help vpn` shows usage

---

### Phase 7: WebUI Integration (Complexity: Medium)

**Files to modify:**
- `/home/sf/src/RustRemoteCWKeyer/components/keyer_webui/frontend/src/pages/Config.svelte`

**Alternative:** Create `/home/sf/src/RustRemoteCWKeyer/components/keyer_webui/frontend/src/pages/VPN.svelte`

**Steps:**

1. VPN parameters will automatically appear in Config.svelte under the VPN tab (due to schema-driven rendering).

2. For enhanced UX, add a dedicated VPN status indicator to App.svelte or System.svelte:
```svelte
<script lang="ts">
  let vpnState = $state<string>('disabled');

  async function refreshVpnStatus() {
    // Call a new API endpoint: GET /api/vpn/status
    const res = await fetch('/api/vpn/status');
    const data = await res.json();
    vpnState = data.state;
  }
</script>

<div class="vpn-indicator" class:connected={vpnState === 'connected'}>
  <span class="vpn-icon">[VPN]</span>
  <span class="vpn-status">{vpnState.toUpperCase()}</span>
</div>
```

3. Add API endpoint for VPN status:
- Create `/home/sf/src/RustRemoteCWKeyer/components/keyer_webui/src/api_vpn.c`
- Register in http_server.c

**Acceptance criteria:**
- [ ] VPN family appears in Config tabs
- [ ] Sensitive fields (keys) use password widget
- [ ] Status indicator shows current state
- [ ] Reboot required notice for config changes

---

## Testing Strategy

### Unit Tests (Host)

Create `/home/sf/src/RustRemoteCWKeyer/test_host/test_vpn_config.c`:
- Test config parsing
- Test state machine transitions
- Mock WireGuard library calls

### Integration Tests (Device)

1. **Basic connectivity**:
   - Set up local WireGuard server (e.g., Docker)
   - Configure keyer with test keys
   - Verify handshake and ping

2. **Failure modes**:
   - Invalid server endpoint (should fail gracefully)
   - Invalid keys (should fail with clear error)
   - No WiFi (should stay in WAITING_WIFI)
   - No NTP (should timeout and fail)

3. **Reconnection**:
   - WiFi disconnect/reconnect
   - Server restart
   - Network flap

4. **RT path isolation**:
   - Monitor RT task timing during VPN operations
   - Verify no jitter increase during handshake

### Manual Testing Checklist

- [ ] Fresh boot with VPN disabled works normally
- [ ] Enable VPN, save, reboot - tunnel comes up
- [ ] Keys can be entered via console
- [ ] Keys can be entered via WebUI
- [ ] Disconnect/reconnect works
- [ ] RT keying works during VPN operations
- [ ] LED states reflect VPN status (if implemented)

---

## Risks and Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| NTP timeout in poor networks | Medium | VPN won't connect | Increase timeout, add retry, cache last known time |
| Memory pressure during handshake | Low | System instability | Monitor heap watermarks, test with stressed memory |
| Stack overflow in VPN task | Medium | Crash | Start with 6KB stack, measure with uxTaskGetStackHighWaterMark() |
| WiFi reconnect race | Medium | VPN stuck in bad state | Add WiFi event handler for disconnect, trigger VPN restart |
| Key storage security | Low | Keys exposed | Keys in NVS (flash encryption available), sensitive widget hides display |

### Stack Size Recommendation

```c
/* VPN connection task - needs room for crypto operations */
xTaskCreatePinnedToCore(
    vpn_connection_task,
    "vpn_conn",
    6144,  /* 6KB - WireGuard crypto is stack-hungry */
    NULL,
    5,     /* Same priority as WiFi connection task */
    NULL,
    1      /* Core 1 only */
);
```

---

## Estimated Complexity

| Phase | Effort | Risk | Priority |
|-------|--------|------|----------|
| 1. Config Schema | 1 hour | Low | P0 |
| 2. Component Skeleton | 2 hours | Low | P0 |
| 3. NTP Time Sync | 2 hours | Medium | P0 |
| 4. WireGuard Integration | 4 hours | High | P0 |
| 5. Main Integration | 1 hour | Low | P0 |
| 6. Console Commands | 1 hour | Low | P1 |
| 7. WebUI Integration | 2 hours | Low | P2 |

**Total estimated effort**: 13 hours

**Dependencies**:
- Phases 1-5 are sequential (P0 critical path)
- Phase 6 can start after Phase 5
- Phase 7 can start after Phase 1 (config schema)

---

## Files Summary

**New files:**
- `components/keyer_vpn/CMakeLists.txt`
- `components/keyer_vpn/idf_component.yml`
- `components/keyer_vpn/include/vpn.h`
- `components/keyer_vpn/src/vpn.c`
- `components/keyer_webui/src/api_vpn.c` (optional)
- `test_host/test_vpn_config.c`

**Modified files:**
- `parameters.yaml` - Add vpn family
- `main/main.c` - VPN init
- `main/bg_task.c` - VPN state monitoring
- `components/keyer_console/src/commands.c` - vpn command
- `components/keyer_webui/src/http_server.c` - API registration (optional)
- `components/keyer_webui/frontend/src/pages/Config.svelte` or new VPN.svelte

**Generated files (auto):**
- `components/keyer_config/include/config.h`
- `components/keyer_config/src/config.c`
- `components/keyer_config/include/config_nvs.h`
- `components/keyer_config/src/config_nvs.c`
- `components/keyer_config/include/config_console.h`
- `components/keyer_config/src/config_console.c`
- `components/keyer_webui/src/config_schema.h`
