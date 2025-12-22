# USB Multi-CDC Architecture Design

**Date:** 2024-12-22
**Status:** Implemented
**Author:** Brainstorming session with Claude

## Overview

Redesign USB architecture to use TinyUSB with multiple CDC endpoints, replacing the current USB Serial JTAG approach. This fixes the console echo bug and provides a scalable architecture for future features.

## Problem Statement

Current implementation uses `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` with `getchar()` for console input. This causes:
- **Echo delay**: Characters only appear after pressing Enter (stdio line buffering)
- **Single port limitation**: Cannot separate console from logs
- **No expansion path**: Cannot add Winkeyer protocol

## Architecture

### USB Endpoints

| CDC | Function | Implementation | Status |
|-----|----------|----------------|--------|
| CDC0 | Console | Interactive commands, immediate echo | Active |
| CDC1 | Log | RT-safe ring buffer drain | Active |
| CDC2 | Winkeyer3 | K1EL WK3 protocol emulation | Stub |

### Data Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                         USB Host (PC)                           │
├──────────────────┬──────────────────┬──────────────────────────┤
│   /dev/ttyACM0   │   /dev/ttyACM1   │      /dev/ttyACM2        │
│   (COM3 Win)     │   (COM4 Win)     │      (COM5 Win)          │
└────────┬─────────┴────────┬─────────┴────────────┬─────────────┘
         │                  │                      │
         ▼                  ▼                      ▼
┌─────────────────┐ ┌─────────────────┐ ┌─────────────────────────┐
│  CDC0: Console  │ │  CDC1: Log      │ │  CDC2: Winkeyer3 (stub) │
│                 │ │                 │ │                         │
│ RX callback     │ │ Drain task      │ │ TODO: WK3 parser        │
│ Echo immediato  │ │ Filtri tag/lvl  │ │                         │
│ Tab completion  │ │                 │ │                         │
└────────┬────────┘ └────────┬────────┘ └─────────────────────────┘
         │                   │
         ▼                   │
┌─────────────────┐          │
│ Console Parser  │          │
│ Commands exec   │          │
└─────────────────┘          │
                             │
         ┌───────────────────┘
         ▼
┌─────────────────────────────────────────┐
│           RT Log Ring Buffer            │
│              (lock-free)                │
└─────────────────────────────────────────┘
         ▲
         │
┌────────┴────────┐
│  RT_INFO()      │
│  RT_WARN()      │  ← Core 0 RT path
│  RT_ERROR()     │
└─────────────────┘
```

## Component Structure

```
components/keyer_usb/
├── CMakeLists.txt
├── idf_component.yml          # Dependency: esp_tinyuf2
├── include/
│   ├── usb_cdc.h              # TinyUSB init, CDC management
│   ├── usb_console.h          # CDC0 console handler
│   ├── usb_log.h              # CDC1 log drain + filters
│   ├── usb_winkeyer.h         # CDC2 stub (future)
│   └── usb_uf2.h              # UF2 bootloader command
└── src/
    ├── usb_cdc.c              # USB descriptor, callbacks
    ├── usb_console.c          # RX callback, echo, completion
    ├── usb_log.c              # Drain task, tag/level filters
    ├── usb_winkeyer.c         # Stub implementation
    └── usb_uf2.c              # esp_restart_from_tinyuf2()
```

## CDC0: Console Implementation

### Immediate Echo (Fixes Original Bug)

```c
static void console_rx_callback(int itf, cdcacm_event_t *event) {
    uint8_t buf[64];
    size_t len = 0;

    tinyusb_cdcacm_read(itf, buf, sizeof(buf), &len);

    for (size_t i = 0; i < len; i++) {
        char c = (char)buf[i];

        // Immediate echo before processing
        echo_char(itf, c);

        // Push to console state machine
        console_push_char(c);
    }
    tinyusb_cdcacm_write_flush(itf);
}

static void echo_char(int itf, char c) {
    if (c >= 0x20 && c <= 0x7E) {
        // Printable character
        tinyusb_cdcacm_write(itf, (uint8_t*)&c, 1);
    } else if (c == '\r' || c == '\n') {
        // Enter - echo CRLF
        tinyusb_cdcacm_write(itf, (uint8_t*)"\r\n", 2);
    } else if (c == 0x08 || c == 0x7F) {
        // Backspace or DEL - destructive backspace
        tinyusb_cdcacm_write(itf, (uint8_t*)"\b \b", 3);
    }
    // Ctrl+C, Ctrl+U handled without echo
}
```

### Cross-Platform Compatibility

| Issue | Windows | Linux | Solution |
|-------|---------|-------|----------|
| Line ending | `\r\n` | `\n` | Always send `\r\n` |
| Backspace | `0x08` or `0x7F` | `0x7F` | Handle both |
| CDC driver | usbser.sys (auto) | cdc_acm (auto) | Standard USB CDC-ACM |
| Port order | Non-deterministic | Stable | Use unique USB serial number |

## CDC1: Log Implementation

### RT-Safe Drain

```c
void usb_log_drain_task(void *arg) {
    rt_log_entry_t entry;
    char line[128];

    for (;;) {
        while (rt_log_pop(&g_rt_log_stream, &entry)) {

            if (!log_filter_pass(&entry)) {
                continue;
            }

            int len = snprintf(line, sizeof(line),
                "[%lld] %s: %s\r\n",
                entry.timestamp_us,
                entry.tag,
                entry.message);

            tinyusb_cdcacm_write(CDC_LOG_ITF, (uint8_t*)line, len);
        }
        tinyusb_cdcacm_write_flush(CDC_LOG_ITF);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

### Console Commands for Log Filtering

Two command styles supported with tab completion:

```
# ESP-IDF style
> log level IAMBIC DEBUG
> log level * WARN

# Compact style
> log IAMBIC=D
> log *=W

# Tab completion
> log <TAB>           # Shows available tags
> log level <TAB>     # Shows: ERROR WARN INFO DEBUG VERBOSE
```

## UF2 Bootloader

### Using esp_tinyuf2 Component

```yaml
# idf_component.yml
dependencies:
  esp_tinyuf2: "*"
```

### Software Entry (No Button Combination)

```c
// Console command
static int cmd_uf2(int argc, char **argv) {
    printf("Entering UF2 mode...\r\n");
    tinyusb_cdcacm_write_flush(CDC_CONSOLE_ITF);
    vTaskDelay(pdMS_TO_TICKS(100));  // Flush to host

    esp_restart_from_tinyuf2();  // Official API
    return 0;  // Never reached
}

// Future: HTTP API
// POST /api/system/uf2
```

### Initialization

```c
void app_main(void) {
    // ... other init ...

    tinyuf2_ota_config_t ota_config = DEFAULT_TINYUF2_OTA_CONFIG();
    ota_config.complete_cb = uf2_update_complete_cb;
    esp_tinyuf2_install(&ota_config, NULL);
}
```

## Configuration Files

### sdkconfig.defaults (Replace Current)

Key changes from `tmp/sdkconfig.defaults`:

```
# TinyUSB CDC
CONFIG_TINYUSB_CDC_ENABLED=y
CONFIG_TINYUSB_CDC_COUNT=2          # Increase to 3 when WK3 ready
CONFIG_TINYUSB_CDC_RX_BUFSIZE=1024
CONFIG_TINYUSB_CDC_TX_BUFSIZE=4096

# Disable USB Serial JTAG (conflicts with TinyUSB)
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=n
CONFIG_ESP_CONSOLE_UART_DEFAULT=y
CONFIG_ESP_CONSOLE_SECONDARY_NONE=y

# Line endings
CONFIG_NEWLIB_STDOUT_LINE_ENDING_CRLF=y
```

### partitions.csv (From tmp/)

```
nvs,        data, nvs,      0x9000,   96K,
otadata,    data, ota,      ,         8K,
phy_init,   data, phy,      ,         4K,
coredump,   data, coredump, ,         64K,
factory,    app,  factory,  ,         2M,      # UF2 bootloader
ota_0,      app,  ota_0,    ,         3500K,
ota_1,      app,  ota_1,    ,         3500K,
spiffs,     data, spiffs,   ,         7000K,
```

## CDC2: Winkeyer3 Stub

Reserved for future Winkeyer3 protocol implementation.

```c
// usb_winkeyer.h
#pragma once
#include "esp_err.h"

esp_err_t usb_winkeyer_init(void);
bool usb_winkeyer_is_enabled(void);

// usb_winkeyer.c
#include "usb_winkeyer.h"

esp_err_t usb_winkeyer_init(void) {
    // TODO: Implement Winkeyer3 protocol on CDC2
    // - Admin commands (0x00)
    // - Speed (0x02), Sidetone (0x03), Weight (0x04)
    // - PTT timing (0x05)
    // - Clear buffer (0x0A)
    // - Message slots (0x1x)
    // - Status responses
    return ESP_OK;
}

bool usb_winkeyer_is_enabled(void) {
    return false;
}
```

## Migration Steps

1. **Copy configuration files**
   - `tmp/sdkconfig.defaults` → `sdkconfig.defaults`
   - `tmp/partitions.csv` → `partitions.csv`
   - Update `CONFIG_TINYUSB_CDC_COUNT=2`

2. **Create `keyer_usb` component**
   - Implement `usb_cdc.c` with TinyUSB init
   - Implement `usb_console.c` with echo callback
   - Implement `usb_log.c` with drain task
   - Add stubs for `usb_winkeyer.c` and `usb_uf2.c`

3. **Modify `keyer_console`**
   - Remove `getchar()` based task
   - Add `console_push_char()` interface
   - Keep command parsing logic

4. **Update `main/`**
   - Call `usb_cdc_init()` early in `app_main()`
   - Register console commands (`uf2`, `log`)

5. **Test**
   - Windows: PuTTY, TeraTerm, Windows Terminal
   - Linux: minicom, picocom, screen
   - Verify immediate echo
   - Verify log filtering

## Future Expansion

When implementing Winkeyer3:
1. Change `CONFIG_TINYUSB_CDC_COUNT=3`
2. Implement WK3 protocol parser in `usb_winkeyer.c`
3. Integrate with `keying_stream_t` for TX
4. Test with N1MM+, fldigi, other contest loggers

## Implementation Notes (2024-12-22)

### esp_tinyuf2 Conflict

During implementation, we discovered that `esp_tinyusb` and `esp_tinyuf2` cannot be used together:
- `esp_tinyusb` uses `espressif__tinyusb` managed component
- `esp_tinyuf2` uses `leeebo__tinyusb_src` (older TinyUSB version)
- Linking fails with multiple definition errors for TinyUSB symbols

**Workaround:** UF2 bootloader entry currently uses a simple `esp_restart()`. The user can then enter USB download mode manually (hold BOOT button during reset) for firmware updates via esptool.

**Future:** Investigate if esp_tinyuf2 can be configured to share TinyUSB with esp_tinyusb, or use ESP-IDF's built-in USB DFU class instead.

### API Changes

The esp_tinyusb API uses different function names than documented in early examples:
- `tinyusb_cdcacm_write_queue()` instead of `tinyusb_cdcacm_write()`
- Interface type is `tinyusb_cdcacm_itf_t` with values `TINYUSB_CDC_ACM_0`, `TINYUSB_CDC_ACM_1`
- Callback signature is `void(*tusb_cdcacm_callback_t)(int itf, cdcacm_event_t *event)`

## References

- [ESP TinyUF2 Documentation](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_device/esp_tinyuf2.html)
- [TinyUSB CDC-ACM](https://docs.tinyusb.org/en/latest/reference/device.html#cdc)
- [ESP-IDF USB Device](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/usb_device.html)
