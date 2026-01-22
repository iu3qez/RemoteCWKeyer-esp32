# Codebase Analysis: WireGuard VPN Integration

**Project:** ESP32-S3 CW Keyer (RemoteCWKeyerV3)
**Analysis Date:** 2026-01-21
**Branch:** wireguard-vpn (derived from main)

## Summary

VPN should integrate as a **best-effort network service** on Core 1, following patterns from `keyer_wifi` and `keyer_webui` components.

## Key Integration Points

### 1. Network Component Structure

Existing pattern in `components/keyer_wifi/`:
- `src/wifi.c`, `include/wifi.h`
- API: `wifi_app_init()`, `wifi_app_start()`, `wifi_get_state()`
- Atomic state management

**Recommended VPN structure:**
```
components/keyer_vpn/
├── CMakeLists.txt
├── include/vpn.h
└── src/
    └── vpn.c
```

### 2. Console Command Registration

Location: `components/keyer_console/src/commands.c`

Commands registered in static array (lines 1224-1260):
```c
static const console_cmd_t s_commands[] = {
    { "help",    "List commands", NULL, cmd_help },
    { "vpn",     "VPN control",   USAGE_VPN, cmd_vpn },  // ADD
    ...
};
```

### 3. Configuration System

Pipeline:
```
parameters.yaml → scripts/gen_config_c.py → components/keyer_config/
```

Add VPN family to `parameters.yaml`:
```yaml
families:
  vpn:
    order: 9
    parameters:
      enabled: { type: bool, default: false, nvs_key: "vpn_en" }
      server_endpoint: { type: string, max_length: 64, nvs_key: "vpn_srv" }
      private_key: { type: string, max_length: 64, nvs_key: "vpn_priv", sensitive: true }
      peer_public_key: { type: string, max_length: 64, nvs_key: "vpn_peer" }
      local_ip: { type: string, max_length: 16, nvs_key: "vpn_ip" }
      keepalive_sec: { type: u16, default: 25, nvs_key: "vpn_keep" }
```

### 4. Service Initialization (main.c)

Init sequence - add VPN after WiFi, before WebUI:
```c
// 8. WiFi init (if enabled)
if (CONFIG_GET_WIFI_ENABLED()) {
    wifi_app_init(&wifi_cfg);
    wifi_app_start();
}

// 9. VPN init (ADD HERE)
if (CONFIG_GET_VPN_ENABLED()) {
    vpn_init(&vpn_cfg);
    vpn_start();  // Non-blocking
}

// 10. WebUI init
webui_init();
```

### 5. Background Task Integration (bg_task.c)

Add VPN state monitoring alongside WiFi monitoring:
```c
vpn_state_t vs = vpn_get_state();
if (vs != prev_vpn_state) {
    if (vs == VPN_STATE_CONNECTED) {
        RT_INFO(&g_bg_log_stream, now_us, "VPN tunnel established");
    }
    prev_vpn_state = vs;
}
```

### 6. Task Model

| Core | Task | Priority | Purpose |
|------|------|----------|---------|
| 0 | rt_task | MAX-1 | **HARD RT** - NO VPN impact |
| 1 | bg_task | IDLE+2 | WiFi/VPN/LED monitor |
| 1 | vpn_task | 5 | WireGuard state machine |

**Recommendation:** Dedicated VPN task for isolation and larger stack (8KB for crypto).

## Architecture Constraints

- VPN runs on Core 1 (best-effort)
- **ZERO impact** on RT path (Core 0)
- VPN failure does NOT trigger FAULT
- Local keying continues even if VPN is down
