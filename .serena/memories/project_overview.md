# RemoteCWKeyer-esp32 — Project Overview

## Purpose
ESP32-based professional CW (Morse code) keyer with remote operation capability. Implements iambic keying, sidetone generation, and network-transparent remote CW operation.

## Tech Stack
- **Language**: Pure C (C11 standard)
- **Framework**: ESP-IDF v5.x (FreeRTOS-based)
- **Target**: ESP32-S3, ESP32-P4
- **Frontend**: Web UI (npm-based, in `components/keyer_webui/frontend/`)
- **Testing**: Unity test framework, host-based (no hardware needed)
- **Static Analysis**: PVS-Studio
- **Config Generation**: Python script (`scripts/gen_config_c.py`) from `parameters.yaml`

## Architecture Summary
Central abstraction: **KeyingStream** — a lock-free ring buffer that is the ONLY shared state.

```
Producer → KeyingStream (lock-free) → Consumers
```

- **Producers**: GPIO poll, Iambic FSM, remote CW
- **Hard RT Consumers** (Core 0): Audio/TX — must keep up or FAULT
- **Best-Effort Consumers** (Core 1): Remote forwarder, decoder, diagnostics — skip if behind

### Threading Model
| Core | Task | Priority | Purpose |
|------|------|----------|---------|
| 0 | rt_task | MAX-1 | GPIO, Iambic, Stream, Audio/TX |
| 1 | bg_task | IDLE+2 | Remote, decoder, diagnostics |
| 1 | uart_log | IDLE+1 | Log drain to UART |
| 1 | console | IDLE+1 | Serial console |

### Critical Constraints
- **100µs latency ceiling** on RT path (Core 0)
- **No malloc/free** in RT path
- **No ESP_LOGx/printf** in RT path — use `RT_*()` macros from `rt_log.h`
- **No mutexes** — `stdatomic.h` only
- **No callbacks** between components
- **FAULT philosophy**: corrupted timing is worse than silence

## Components
| Component | Purpose |
|-----------|---------|
| keyer_core | Stream, sample, consumer, fault |
| keyer_iambic | Iambic FSM (pure logic) |
| keyer_audio | Sidetone, buffer, PTT |
| keyer_logging | RT-safe logging |
| keyer_console | Serial console |
| keyer_config | Generated config (DO NOT EDIT) |
| keyer_webui | Web UI |
| keyer_hal | GPIO, I2S, ES8311 HAL |
| keyer_cwnet | CWNet remote keying protocol |
| keyer_decoder | Morse decoder |
| keyer_wifi | WiFi management |
| keyer_vpn | VPN connectivity |
| keyer_led | LED control |
| keyer_text | Text/morse table |
| keyer_usb | USB support |
| provisioning | Device provisioning |
