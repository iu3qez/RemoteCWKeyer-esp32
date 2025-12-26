# LED Status & WiFi Connectivity Design

## Overview

Two features for the CW keyer:
1. **RGB LED Status Feedback** - 7 WS2812B LEDs showing keyer state and keying activity
2. **WiFi Connectivity** - STA mode with AP fallback for future network features

## LED Hardware & Configuration

**Component:** `keyer_led` - WS2812B RGB LED driver

**Parameters in `parameters.yaml`** (new `leds` family):

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `gpio_data` | u8 | 38 | WS2812B data pin |
| `count` | u8 | 7 | Number of LEDs |
| `brightness` | u8 | 50 | Master brightness 0-100% |
| `brightness_dim` | u8 | 10 | Dim brightness for idle state |

**LED Layout (default 7 LEDs):**

```
[0][1][2]  [3]  [4][5][6]
 └─DIT─┘  SQUEEZE └─DAH─┘
```

The layout auto-adapts: with N LEDs, center = N/2, left half = DIT, right half = DAH.

## LED States & Colors

**State Machine:**

| State | LEDs | Color | Effect |
|-------|------|-------|--------|
| `BOOT` | All | Orange (#FF8000) | Breathing (slow fade in/out) |
| `WIFI_CONNECTING` | All | Orange | Breathing (same as boot) |
| `WIFI_FAILED` | All | Red (#FF0000) | Brief flash (500ms), then → DEGRADED |
| `DEGRADED` | All | Dim yellow (#FFA000) | Steady, low brightness |
| `AP_MODE` | All | Orange/Blue | Alternating (toggle every 500ms) |
| `CONNECTED` | All | Green (#00FF00) | 3 quick flashes, then → IDLE |
| `IDLE` | All | Dim green | Steady, `brightness_dim` level |

**Keying Overlay (during IDLE/CONNECTED):**

| Event | LEDs affected | Color | Effect |
|-------|---------------|-------|--------|
| DIT down | LEDs 0,1,2 | Bright green | On while key held |
| DAH down | LEDs 4,5,6 | Bright green | On while key held |
| Squeeze | LED 3 (center) | Magenta (#FF00FF) | On while both held |

## WiFi Configuration Parameters

**New `wifi` family in `parameters.yaml`:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `enabled` | bool | false | Master WiFi on/off |
| `ssid` | string[32] | "" | Network name |
| `password` | string[64] | "" | Network password (sensitive) |
| `timeout_sec` | u16 | 30 | Connection timeout |
| `use_static_ip` | bool | false | DHCP vs static |
| `ip_address` | string[16] | "0.0.0.0" | Static IP |
| `netmask` | string[16] | "255.255.255.0" | Subnet mask |
| `gateway` | string[16] | "0.0.0.0" | Gateway |
| `dns` | string[16] | "0.0.0.0" | DNS server |

**Hardcoded AP fallback:**
- AP SSID: `"CWKeyer-XXXX"` (XXXX = last 4 hex of MAC)
- AP Password: none (open network)

## Architecture & Components

**New components:**

| Component | Location | Purpose |
|-----------|----------|---------|
| `keyer_led` | `components/keyer_led/` | WS2812B driver + state machine |
| `keyer_wifi` | `components/keyer_wifi/` | WiFi STA + AP fallback logic |

**Threading model:**

| Task | Core | Responsibility |
|------|------|----------------|
| `bg_task` (existing) | Core 1 | Calls `led_tick()` periodically |
| `bg_task` (existing) | Core 1 | WiFi connection management |

**Data flow:**

```
RT Task (Core 0)                    BG Task (Core 1)
     │                                    │
     │ atomic paddle state                │
     └──────────────────────────────────► led_update()
                                          │
     wifi_state (atomic)                  │
     ◄────────────────────────────────────┘
                                    wifi_manager()
```

**Key points:**
- LED update runs in `bg_task` (not RT-critical)
- Reads paddle state via atomics (no mutex)
- WiFi runs entirely on Core 1
- LED driver uses RMT peripheral (ESP-IDF standard for WS2812B)

## API Design

**`keyer_led` API (`led.h`):**

```c
// Initialization (called once at startup)
esp_err_t led_init(const led_config_t *config);

// State control (called by bg_task based on system state)
void led_set_state(led_state_t state);

// Keying overlay (called by bg_task, reads atomic paddle state)
void led_update_keying(bool dit, bool dah);

// Combined update (call periodically from bg_task)
void led_tick(int64_t now_us);
```

**`keyer_wifi` API (`wifi.h`):**

```c
// Initialization (called once at startup)
esp_err_t wifi_init(const wifi_config_app_t *config);

// Start connection attempt (non-blocking)
esp_err_t wifi_start(void);

// Get current state (atomic, safe from any task)
wifi_state_t wifi_get_state(void);

// States: DISABLED, CONNECTING, CONNECTED, FAILED, AP_MODE
```

**Integration in `bg_task`:**

```c
void bg_task(void *arg) {
    led_init(&led_cfg);
    wifi_init(&wifi_cfg);

    if (wifi_cfg.enabled) {
        led_set_state(LED_STATE_WIFI_CONNECTING);
        wifi_start();
    }

    for (;;) {
        // Update LED state based on WiFi
        wifi_state_t ws = wifi_get_state();
        led_set_state(wifi_to_led_state(ws));

        // Update keying overlay
        led_tick(esp_timer_get_time());

        vTaskDelay(pdMS_TO_TICKS(20));  // 50Hz LED update
    }
}
```

## Error Handling

**WiFi scenarios:**

| Scenario | Behavior |
|----------|----------|
| `wifi.enabled = false` | Skip WiFi init, LEDs go straight to IDLE (dim green) |
| SSID empty | Treat as disabled, skip connection |
| Connection timeout | Brief red flash → AP_MODE |
| WiFi disconnects later | Auto-reconnect attempt, LED stays green during retry |
| Reconnect fails 3x | Switch to AP_MODE |

**LED driver scenarios:**

| Scenario | Behavior |
|----------|----------|
| RMT init fails | Log error, continue without LEDs (non-fatal) |
| Invalid GPIO | Log error at init, continue without LEDs |
| `led_count = 0` | Disable LED feature entirely |

**FAULT philosophy:**
- WiFi/LED failures are **not** FAULTs - keyer continues operating
- These are "best effort" features on Core 1
- Only RT path issues (stream, audio, TX) trigger FAULT
