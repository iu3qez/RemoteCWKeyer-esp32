# RT-Safe Logging Guide

## Overview

RustRemoteCWKeyer uses a **RT-safe logging system** that never blocks the real-time path.

## Quick Start

```c
#include "rt_log.h"
#include "esp_timer.h"

// Get timestamp
int64_t now_us = esp_timer_get_time();

// Log at different levels
RT_ERROR(&g_rt_log_stream, now_us, "FAULT: %s", fault_code_str(code));
RT_WARN(&g_rt_log_stream, now_us, "Lag %u samples", lag);
RT_INFO(&g_rt_log_stream, now_us, "Key down @ %lld", time);
RT_DEBUG(&g_rt_log_stream, now_us, "State: %d", state);
RT_TRACE(&g_rt_log_stream, now_us, "GPIO: 0x%02x", bits);
```

## Rules

### ❌ FORBIDDEN in RT Path

These functions **block** for milliseconds - never use in real-time code:

```c
ESP_LOGI(TAG, "...");       // ESP-IDF log: UART mutex + blocking TX
printf("...");              // C printf: blocks
puts("...");                // C puts: blocks
fprintf(stderr, "...");     // C fprintf: blocks
```

### ✅ ALLOWED in RT Path

RT-safe macros complete in ~100-200ns, never block:

```c
int64_t now_us = esp_timer_get_time();
RT_INFO(&g_rt_log_stream, now_us, "Key down @ %lld", time);
RT_WARN(&g_rt_log_stream, now_us, "Lag %u samples", lag);
RT_ERROR(&g_rt_log_stream, now_us, "FAULT: %s", fault_code_str(code));
RT_DEBUG(&g_rt_log_stream, now_us, "State: %d", state);
RT_TRACE(&g_rt_log_stream, now_us, "GPIO: 0x%02x", bits);
```

## Log Levels

| Level | Macro | Use Case |
|-------|-------|----------|
| **Error** | `RT_ERROR()` | FAULT conditions, critical errors |
| **Warn** | `RT_WARN()` | Lag warnings, dropped samples |
| **Info** | `RT_INFO()` | Normal events (key up/down, config) |
| **Debug** | `RT_DEBUG()` | Detailed debugging (state transitions) |
| **Trace** | `RT_TRACE()` | Verbose tracing (every sample, GPIO) |

## Architecture

```text
┌────────────────────────────────────────┐
│  RT Thread (Core 0)                    │
│  RT_INFO(...) ────▶ g_rt_log_stream   │
│                      (256 entries)      │
└──────────────────────┬─────────────────┘
                       │
                       │ Lock-free SPMC
                       │
┌──────────────────────▼─────────────────┐
│  Best-Effort Threads (Core 1)          │
│  RT_WARN(...) ────▶ g_bg_log_stream   │
│                      (256 entries)      │
└──────────────────────┬─────────────────┘
                       │
                       ▼
              ┌────────────────┐
              │ UART Consumer  │
              │ (Core 1, low   │
              │  priority)     │
              └────────┬───────┘
                       │
                       ▼
                  Serial Output
```

## Guarantees

- **Latency**: < 200 nanoseconds per `RT_INFO()` call
- **Never blocks**: If buffer full → message dropped, counter incremented
- **No allocation**: Static buffers, zero heap usage
- **Lock-free**: C11 `stdatomic.h` operations only

## Dropped Messages

When log buffer fills up:
- Message dropped atomically
- Dropped counter incremented
- Report every 10 seconds on UART
- **This is normal** under heavy load - better drop than block RT

## Usage Examples

### Basic Logging

```c
#include "rt_log.h"
#include "esp_timer.h"

void my_rt_function(void) {
    int64_t now = esp_timer_get_time();

    RT_INFO(&g_rt_log_stream, now, "Starting keyer, WPM=%u", 25);

    // ... RT processing ...

    if (error) {
        RT_ERROR(&g_rt_log_stream, now, "FAULT detected: %s",
                 fault_code_str(fault_code));
    }
}
```

### Formatting

Supports all printf format specifiers:

```c
RT_DEBUG(&g_rt_log_stream, now, "State: %d, GPIO: 0x%02x, Time: %lld",
    state, gpio_bits, elapsed_us);
```

**Limit**: Messages truncated at 120 characters.

## Performance

Typical benchmarks:
- `RT_INFO()` push: ~150ns
- `RT_ERROR()` push: ~180ns
- UART drain: 1-2ms per message (background, non-critical)

## FAQ

**Q: Which stream should I use?**
A: Core 0 (RT thread) → `g_rt_log_stream`, Core 1 (background) → `g_bg_log_stream`

**Q: What happens if buffer fills?**
A: Message dropped, counter incremented. See periodic report on UART.

**Q: Can I filter log levels at runtime?**
A: Yes, via `debug_logging` parameter in CONFIG (parameters.yaml).

**Q: Are timestamps precise?**
A: Yes, microsecond precision from `esp_timer_get_time()`.

**Q: How do I see the logs?**
A: Connect serial monitor at 115200 baud. Format: `[timestamp_us] LEVEL: message`
