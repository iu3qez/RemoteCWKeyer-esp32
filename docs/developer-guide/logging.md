# RT-Safe Logging Guide

## Overview

RustRemoteCWKeyer uses a **RT-safe logging system** that never blocks the real-time path.

## Quick Start

```rust
// Get timestamp
let now_us = unsafe { esp_idf_sys::esp_timer_get_time() };

// Log at different levels
rt_error!(&RT_LOG_STREAM, now_us, "FAULT: {:?}", code);
rt_warn!(&RT_LOG_STREAM, now_us, "Lag {} samples", lag);
rt_info!(&RT_LOG_STREAM, now_us, "Key down @ {}", time);
rt_debug!(&RT_LOG_STREAM, now_us, "State: {:?}", state);
rt_trace!(&RT_LOG_STREAM, now_us, "GPIO: {:08b}", bits);
```

## Rules

### ❌ FORBIDDEN in RT Path

These functions **block** for milliseconds - never use in real-time code:

```rust
ESP_LOGI!(TAG, "...");      // ESP-IDF log: UART mutex + blocking TX
println!("...");            // Rust print: locks stdout
log::info!("...");          // Standard log: may block
printf("...");              // C printf: blocks
```

### ✅ ALLOWED in RT Path

RT-safe macros complete in ~100-200ns, never block:

```rust
rt_info!(&stream, timestamp, "Key down @ {}", time);
rt_warn!(&stream, timestamp, "Lag {} samples", lag);
rt_error!(&stream, timestamp, "FAULT: {:?}", fault);
rt_debug!(&stream, timestamp, "State: {:?}", state);
rt_trace!(&stream, timestamp, "GPIO: {:08b}", bits);
```

## Log Levels

| Level | Macro | Use Case |
|-------|-------|----------|
| **Error** | `rt_error!()` | FAULT conditions, critical errors |
| **Warn** | `rt_warn!()` | Lag warnings, dropped samples |
| **Info** | `rt_info!()` | Normal events (key up/down, config) |
| **Debug** | `rt_debug!()` | Detailed debugging (state transitions) |
| **Trace** | `rt_trace!()` | Verbose tracing (every sample, GPIO) |

## Architecture

```text
┌────────────────────────────────────────┐
│  RT Thread (Core 0)                    │
│  rt_info!(...) ────▶ RT_LOG_STREAM    │
│                      (256 entries)      │
└──────────────────────┬─────────────────┘
                       │
                       │ Lock-free SPMC
                       │
┌──────────────────────▼─────────────────┐
│  Best-Effort Threads (Core 1)          │
│  rt_warn!(...) ────▶ BG_LOG_STREAM    │
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

- **Latency**: < 200 nanoseconds per `rt_log!()` call
- **Never blocks**: If buffer full → message dropped, counter incremented
- **No allocation**: Static buffers, zero heap usage
- **Lock-free**: Atomic operations only

## Dropped Messages

When log buffer fills up:
- Message dropped atomically
- Dropped counter incremented
- Report every 10 seconds on UART
- **This is normal** under heavy load - better drop than block RT

## Usage Examples

### Basic Logging

```rust
use rust_remote_cw_keyer::{RT_LOG_STREAM, rt_info, rt_error};

fn my_rt_function() {
    let now = unsafe { esp_idf_sys::esp_timer_get_time() };

    rt_info!(&RT_LOG_STREAM, now, "Starting keyer, WPM={}", 25);

    // ... RT processing ...

    if error {
        rt_error!(&RT_LOG_STREAM, now, "FAULT detected: {:?}", fault_code);
    }
}
```

### Formatting

Supports all Rust format specifiers:

```rust
rt_debug!(&RT_LOG_STREAM, now, "State: {:?}, GPIO: {:08b}, Time: {}",
    state, gpio_bits, elapsed_us);
```

**Limit**: Messages truncated at 120 characters.

## Performance

Typical benchmarks:
- `rt_info!()` push: ~150ns
- `rt_error!()` push: ~180ns
- UART drain: 1-2ms per message (background, non-critical)

## FAQ

**Q: Which stream should I use?**
A: Core 0 (RT thread) → `RT_LOG_STREAM`, Core 1 (background) → `BG_LOG_STREAM`

**Q: What happens if buffer fills?**
A: Message dropped, counter incremented. See periodic report on UART.

**Q: Can I filter log levels at runtime?**
A: Yes, via `debug_logging` parameter in CONFIG (parameters.yaml).

**Q: Are timestamps precise?**
A: Yes, microsecond precision from `esp_timer_get_time()`.

**Q: How do I see the logs?**
A: Connect serial monitor at 115200 baud. Format: `[timestamp_us] LEVEL: message`
