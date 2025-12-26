# USB Multi-CDC Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace USB Serial JTAG with TinyUSB multi-CDC for console echo fix and log separation.

**Architecture:** CDC0 for console (callback-driven with immediate echo), CDC1 for RT-safe log drain, CDC2 stub for future Winkeyer3. Uses esp_tinyuf2 component for UF2 firmware updates via console command.

**Tech Stack:** ESP-IDF 5.x, TinyUSB, esp_tinyuf2, Unity (host tests)

---

## Task 1: Update Configuration Files

**Files:**
- Replace: `sdkconfig.defaults`
- Replace: `partitions.csv`

**Step 1: Backup current files**

```bash
cp sdkconfig.defaults sdkconfig.defaults.bak
cp partitions.csv partitions.csv.bak
```

**Step 2: Copy new sdkconfig.defaults from tmp**

```bash
cp tmp/sdkconfig.defaults sdkconfig.defaults
```

**Step 3: Update CDC count to 2**

Edit `sdkconfig.defaults` - change line 21:
```
CONFIG_TINYUSB_CDC_COUNT=2
```

**Step 4: Copy new partitions.csv from tmp**

```bash
cp tmp/partitions.csv partitions.csv
```

**Step 5: Delete old sdkconfig to force regeneration**

```bash
rm -f sdkconfig
```

**Step 6: Commit**

```bash
git add sdkconfig.defaults partitions.csv
git commit -m "chore: update sdkconfig for TinyUSB multi-CDC"
```

---

## Task 2: Create keyer_usb Component Structure

**Files:**
- Create: `components/keyer_usb/CMakeLists.txt`
- Create: `components/keyer_usb/idf_component.yml`
- Create: `components/keyer_usb/include/usb_cdc.h`

**Step 1: Create component directory**

```bash
mkdir -p components/keyer_usb/include
mkdir -p components/keyer_usb/src
```

**Step 2: Create CMakeLists.txt**

Create `components/keyer_usb/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS
        "src/usb_cdc.c"
        "src/usb_console.c"
        "src/usb_log.c"
        "src/usb_winkeyer.c"
        "src/usb_uf2.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        tinyusb
        esp_tinyuf2
        keyer_console
        keyer_logging
    PRIV_REQUIRES
        freertos
        esp_timer
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-parameter
)
```

**Step 3: Create idf_component.yml**

Create `components/keyer_usb/idf_component.yml`:

```yaml
description: "TinyUSB multi-CDC for console, log, and Winkeyer"
version: "1.0.0"
dependencies:
  espressif/esp_tinyuf2: "^1.0.0"
```

**Step 4: Create usb_cdc.h**

Create `components/keyer_usb/include/usb_cdc.h`:

```c
/**
 * @file usb_cdc.h
 * @brief TinyUSB multi-CDC initialization and management
 *
 * CDC0: Console (interactive commands with immediate echo)
 * CDC1: Log (RT-safe ring buffer drain)
 * CDC2: Winkeyer3 (stub for future)
 */

#ifndef KEYER_USB_CDC_H
#define KEYER_USB_CDC_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** CDC interface numbers */
#define CDC_ITF_CONSOLE  0
#define CDC_ITF_LOG      1
#define CDC_ITF_WINKEYER 2  /* Future */

/** USB identification */
#define USB_VID          0x303A  /* Espressif */
#define USB_PID          0x8002  /* Custom device */
#define USB_SERIAL       "KEYER-IU3QEZ-001"

/**
 * @brief Initialize TinyUSB with multi-CDC
 *
 * Initializes TinyUSB, registers CDC callbacks, starts USB task.
 * Must be called early in app_main(), before console_task.
 *
 * @return ESP_OK on success
 */
esp_err_t usb_cdc_init(void);

/**
 * @brief Write to specific CDC interface
 *
 * @param itf Interface number (CDC_ITF_*)
 * @param data Data buffer
 * @param len Data length
 * @return Number of bytes written
 */
size_t usb_cdc_write(uint8_t itf, const uint8_t *data, size_t len);

/**
 * @brief Flush CDC interface TX buffer
 *
 * @param itf Interface number
 */
void usb_cdc_flush(uint8_t itf);

/**
 * @brief Check if CDC interface is connected
 *
 * @param itf Interface number
 * @return true if connected
 */
bool usb_cdc_connected(uint8_t itf);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_USB_CDC_H */
```

**Step 5: Commit**

```bash
git add components/keyer_usb/
git commit -m "feat(usb): add keyer_usb component structure"
```

---

## Task 3: Implement USB Console Header

**Files:**
- Create: `components/keyer_usb/include/usb_console.h`

**Step 1: Create usb_console.h**

Create `components/keyer_usb/include/usb_console.h`:

```c
/**
 * @file usb_console.h
 * @brief CDC0 console with immediate echo
 *
 * Uses TinyUSB callback for character-by-character processing
 * with immediate echo before pushing to console state machine.
 */

#ifndef KEYER_USB_CONSOLE_H
#define KEYER_USB_CONSOLE_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize USB console on CDC0
 *
 * Registers RX callback for immediate echo and character processing.
 *
 * @return ESP_OK on success
 */
esp_err_t usb_console_init(void);

/**
 * @brief Print string to console (CDC0)
 *
 * @param str String to print
 */
void usb_console_print(const char *str);

/**
 * @brief Print formatted string to console (CDC0)
 *
 * @param fmt Format string
 * @param ... Arguments
 */
void usb_console_printf(const char *fmt, ...);

/**
 * @brief Print prompt to console
 */
void usb_console_prompt(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_USB_CONSOLE_H */
```

**Step 2: Commit**

```bash
git add components/keyer_usb/include/usb_console.h
git commit -m "feat(usb): add usb_console.h header"
```

---

## Task 4: Implement USB Log Header

**Files:**
- Create: `components/keyer_usb/include/usb_log.h`

**Step 1: Create usb_log.h**

Create `components/keyer_usb/include/usb_log.h`:

```c
/**
 * @file usb_log.h
 * @brief CDC1 log drain with filtering
 *
 * Drains RT-safe log ring buffer to CDC1.
 * Supports tag/level filtering via console commands.
 */

#ifndef KEYER_USB_LOG_H
#define KEYER_USB_LOG_H

#include "esp_err.h"
#include "rt_log.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize USB log on CDC1
 *
 * @return ESP_OK on success
 */
esp_err_t usb_log_init(void);

/**
 * @brief USB log drain task
 *
 * Drains RT and BG log streams to CDC1.
 * Runs on Core 1, low priority.
 *
 * @param arg Unused
 */
void usb_log_task(void *arg);

/**
 * @brief Set global log level filter
 *
 * @param level Minimum level to output
 */
void usb_log_set_level(log_level_t level);

/**
 * @brief Set log level for specific tag
 *
 * @param tag Tag name (max 15 chars)
 * @param level Minimum level for this tag
 * @return ESP_OK on success, ESP_ERR_NO_MEM if too many tags
 */
esp_err_t usb_log_set_tag_level(const char *tag, log_level_t level);

/**
 * @brief Get current global log level
 *
 * @return Current level
 */
log_level_t usb_log_get_level(void);

/**
 * @brief Clear all tag-specific filters
 */
void usb_log_clear_filters(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_USB_LOG_H */
```

**Step 2: Commit**

```bash
git add components/keyer_usb/include/usb_log.h
git commit -m "feat(usb): add usb_log.h header"
```

---

## Task 5: Implement USB Winkeyer Stub Header

**Files:**
- Create: `components/keyer_usb/include/usb_winkeyer.h`

**Step 1: Create usb_winkeyer.h**

Create `components/keyer_usb/include/usb_winkeyer.h`:

```c
/**
 * @file usb_winkeyer.h
 * @brief CDC2 Winkeyer3 emulation (stub)
 *
 * Future implementation for K1EL Winkeyer3 protocol.
 */

#ifndef KEYER_USB_WINKEYER_H
#define KEYER_USB_WINKEYER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize Winkeyer3 on CDC2 (stub)
 *
 * @return ESP_OK (always, stub implementation)
 */
esp_err_t usb_winkeyer_init(void);

/**
 * @brief Check if Winkeyer is enabled
 *
 * @return false (stub, not implemented)
 */
bool usb_winkeyer_is_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_USB_WINKEYER_H */
```

**Step 2: Commit**

```bash
git add components/keyer_usb/include/usb_winkeyer.h
git commit -m "feat(usb): add usb_winkeyer.h stub header"
```

---

## Task 6: Implement USB UF2 Header

**Files:**
- Create: `components/keyer_usb/include/usb_uf2.h`

**Step 1: Create usb_uf2.h**

Create `components/keyer_usb/include/usb_uf2.h`:

```c
/**
 * @file usb_uf2.h
 * @brief UF2 bootloader entry via command
 *
 * Uses esp_tinyuf2 component for USB firmware updates.
 */

#ifndef KEYER_USB_UF2_H
#define KEYER_USB_UF2_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize UF2 support
 *
 * Installs esp_tinyuf2 OTA handler.
 *
 * @return ESP_OK on success
 */
esp_err_t usb_uf2_init(void);

/**
 * @brief Enter UF2 bootloader mode
 *
 * Restarts device into UF2 mode for firmware update.
 * This function does not return.
 */
void usb_uf2_enter(void);

#ifdef __cplusplus
}
#endif

#endif /* KEYER_USB_UF2_H */
```

**Step 2: Commit**

```bash
git add components/keyer_usb/include/usb_uf2.h
git commit -m "feat(usb): add usb_uf2.h header"
```

---

## Task 7: Implement usb_cdc.c

**Files:**
- Create: `components/keyer_usb/src/usb_cdc.c`

**Step 1: Create usb_cdc.c**

Create `components/keyer_usb/src/usb_cdc.c`:

```c
/**
 * @file usb_cdc.c
 * @brief TinyUSB multi-CDC initialization
 */

#include "usb_cdc.h"
#include "usb_console.h"
#include "usb_log.h"
#include "usb_winkeyer.h"
#include "usb_uf2.h"

#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "esp_log.h"

static const char *TAG = "usb_cdc";

esp_err_t usb_cdc_init(void) {
    ESP_LOGI(TAG, "Initializing TinyUSB multi-CDC");

    /* TinyUSB configuration */
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,  /* Use default */
        .string_descriptor = NULL,  /* Use default */
        .external_phy = false,
        .configuration_descriptor = NULL,  /* Use default */
    };

    esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TinyUSB driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Initialize CDC-ACM for each interface */
    tinyusb_config_cdcacm_t acm_cfg = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .rx_unread_buf_sz = 256,
        .callback_rx = NULL,  /* Set by usb_console_init */
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL,
    };

    /* CDC0: Console */
    acm_cfg.cdc_port = TINYUSB_CDC_ACM_0;
    ret = tusb_cdc_acm_init(&acm_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CDC0 init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* CDC1: Log */
    acm_cfg.cdc_port = TINYUSB_CDC_ACM_1;
    ret = tusb_cdc_acm_init(&acm_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CDC1 init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Initialize sub-components */
    ret = usb_console_init();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = usb_log_init();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = usb_winkeyer_init();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = usb_uf2_init();
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "TinyUSB multi-CDC initialized");
    return ESP_OK;
}

size_t usb_cdc_write(uint8_t itf, const uint8_t *data, size_t len) {
    return tinyusb_cdcacm_write(itf, data, len);
}

void usb_cdc_flush(uint8_t itf) {
    tinyusb_cdcacm_write_flush(itf, 0);
}

bool usb_cdc_connected(uint8_t itf) {
    return tud_cdc_n_connected(itf);
}
```

**Step 2: Commit**

```bash
git add components/keyer_usb/src/usb_cdc.c
git commit -m "feat(usb): implement usb_cdc.c with TinyUSB init"
```

---

## Task 8: Implement usb_console.c with Immediate Echo

**Files:**
- Create: `components/keyer_usb/src/usb_console.c`

**Step 1: Create usb_console.c**

Create `components/keyer_usb/src/usb_console.c`:

```c
/**
 * @file usb_console.c
 * @brief CDC0 console with immediate echo
 */

#include "usb_console.h"
#include "usb_cdc.h"
#include "console.h"

#include "tusb_cdc_acm.h"
#include "esp_log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "usb_console";

/**
 * @brief Echo single character to CDC0
 */
static void echo_char(char c) {
    uint8_t buf[4];
    size_t len = 0;

    if (c >= 0x20 && c <= 0x7E) {
        /* Printable character */
        buf[0] = (uint8_t)c;
        len = 1;
    } else if (c == '\r' || c == '\n') {
        /* Enter - echo CRLF */
        buf[0] = '\r';
        buf[1] = '\n';
        len = 2;
    } else if (c == 0x08 || c == 0x7F) {
        /* Backspace or DEL - destructive backspace */
        buf[0] = '\b';
        buf[1] = ' ';
        buf[2] = '\b';
        len = 3;
    }
    /* Ctrl+C, Ctrl+U: no echo */

    if (len > 0) {
        tinyusb_cdcacm_write(CDC_ITF_CONSOLE, buf, len);
    }
}

/**
 * @brief RX callback for CDC0 - immediate echo
 */
static void console_rx_callback(int itf, cdcacm_event_t *event) {
    (void)event;

    if (itf != CDC_ITF_CONSOLE) {
        return;
    }

    uint8_t buf[64];
    size_t len = 0;

    esp_err_t ret = tinyusb_cdcacm_read(itf, buf, sizeof(buf), &len);
    if (ret != ESP_OK || len == 0) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        char c = (char)buf[i];

        /* Echo immediately */
        echo_char(c);

        /* Push to console state machine */
        bool cmd_complete = console_process_char(c);
        if (cmd_complete) {
            usb_console_prompt();
        }
    }

    tinyusb_cdcacm_write_flush(CDC_ITF_CONSOLE, 0);
}

esp_err_t usb_console_init(void) {
    ESP_LOGI(TAG, "Initializing USB console on CDC0");

    /* Register RX callback */
    tinyusb_cdcacm_register_callback(
        CDC_ITF_CONSOLE,
        CDC_EVENT_RX,
        console_rx_callback
    );

    return ESP_OK;
}

void usb_console_print(const char *str) {
    size_t len = strlen(str);
    tinyusb_cdcacm_write(CDC_ITF_CONSOLE, (const uint8_t *)str, len);
    tinyusb_cdcacm_write_flush(CDC_ITF_CONSOLE, 0);
}

void usb_console_printf(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len > 0) {
        tinyusb_cdcacm_write(CDC_ITF_CONSOLE, (const uint8_t *)buf, (size_t)len);
        tinyusb_cdcacm_write_flush(CDC_ITF_CONSOLE, 0);
    }
}

void usb_console_prompt(void) {
    usb_console_print("\r\n> ");
}
```

**Step 2: Commit**

```bash
git add components/keyer_usb/src/usb_console.c
git commit -m "feat(usb): implement usb_console.c with immediate echo"
```

---

## Task 9: Implement usb_log.c with Drain Task

**Files:**
- Create: `components/keyer_usb/src/usb_log.c`

**Step 1: Create usb_log.c**

Create `components/keyer_usb/src/usb_log.c`:

```c
/**
 * @file usb_log.c
 * @brief CDC1 log drain with filtering
 */

#include "usb_log.h"
#include "usb_cdc.h"
#include "rt_log.h"

#include "tusb_cdc_acm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "usb_log";

/* Filter configuration */
#define MAX_TAG_FILTERS 16
#define MAX_TAG_LEN 16

typedef struct {
    char tag[MAX_TAG_LEN];
    log_level_t level;
} tag_filter_t;

static log_level_t s_global_level = LOG_LEVEL_INFO;
static tag_filter_t s_tag_filters[MAX_TAG_FILTERS];
static size_t s_tag_filter_count = 0;

/**
 * @brief Check if entry passes filter
 */
static bool filter_pass(const log_entry_t *entry, const char *tag) {
    /* Check tag-specific filter first */
    for (size_t i = 0; i < s_tag_filter_count; i++) {
        if (strcmp(s_tag_filters[i].tag, tag) == 0) {
            return entry->level <= s_tag_filters[i].level;
        }
    }

    /* Fall back to global level */
    return entry->level <= s_global_level;
}

esp_err_t usb_log_init(void) {
    ESP_LOGI(TAG, "Initializing USB log on CDC1");
    return ESP_OK;
}

void usb_log_task(void *arg) {
    (void)arg;

    log_entry_t entry;
    char line[160];

    ESP_LOGI(TAG, "USB log drain task started");

    for (;;) {
        /* Drain RT log stream */
        while (log_stream_drain(&g_rt_log_stream, &entry)) {
            if (!filter_pass(&entry, "RT")) {
                continue;
            }

            int len = snprintf(line, sizeof(line),
                "[%lld] %s: %.*s\r\n",
                entry.timestamp_us,
                log_level_str(entry.level),
                (int)entry.len,
                entry.msg);

            if (len > 0 && usb_cdc_connected(CDC_ITF_LOG)) {
                tinyusb_cdcacm_write(CDC_ITF_LOG, (uint8_t *)line, (size_t)len);
            }
        }

        /* Drain BG log stream */
        while (log_stream_drain(&g_bg_log_stream, &entry)) {
            if (!filter_pass(&entry, "BG")) {
                continue;
            }

            int len = snprintf(line, sizeof(line),
                "[%lld] %s: %.*s\r\n",
                entry.timestamp_us,
                log_level_str(entry.level),
                (int)entry.len,
                entry.msg);

            if (len > 0 && usb_cdc_connected(CDC_ITF_LOG)) {
                tinyusb_cdcacm_write(CDC_ITF_LOG, (uint8_t *)line, (size_t)len);
            }
        }

        /* Flush and yield */
        if (usb_cdc_connected(CDC_ITF_LOG)) {
            tinyusb_cdcacm_write_flush(CDC_ITF_LOG, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void usb_log_set_level(log_level_t level) {
    s_global_level = level;
}

esp_err_t usb_log_set_tag_level(const char *tag, log_level_t level) {
    /* Check if tag already exists */
    for (size_t i = 0; i < s_tag_filter_count; i++) {
        if (strcmp(s_tag_filters[i].tag, tag) == 0) {
            s_tag_filters[i].level = level;
            return ESP_OK;
        }
    }

    /* Add new tag filter */
    if (s_tag_filter_count >= MAX_TAG_FILTERS) {
        return ESP_ERR_NO_MEM;
    }

    strncpy(s_tag_filters[s_tag_filter_count].tag, tag, MAX_TAG_LEN - 1);
    s_tag_filters[s_tag_filter_count].tag[MAX_TAG_LEN - 1] = '\0';
    s_tag_filters[s_tag_filter_count].level = level;
    s_tag_filter_count++;

    return ESP_OK;
}

log_level_t usb_log_get_level(void) {
    return s_global_level;
}

void usb_log_clear_filters(void) {
    s_tag_filter_count = 0;
}
```

**Step 2: Commit**

```bash
git add components/keyer_usb/src/usb_log.c
git commit -m "feat(usb): implement usb_log.c with drain task and filters"
```

---

## Task 10: Implement usb_winkeyer.c Stub

**Files:**
- Create: `components/keyer_usb/src/usb_winkeyer.c`

**Step 1: Create usb_winkeyer.c**

Create `components/keyer_usb/src/usb_winkeyer.c`:

```c
/**
 * @file usb_winkeyer.c
 * @brief CDC2 Winkeyer3 emulation (stub)
 */

#include "usb_winkeyer.h"
#include "esp_log.h"

static const char *TAG = "usb_winkeyer";

esp_err_t usb_winkeyer_init(void) {
    ESP_LOGI(TAG, "Winkeyer3 stub initialized (not implemented)");
    /* TODO: Implement Winkeyer3 protocol on CDC2
     * - Admin commands (0x00)
     * - Speed (0x02), Sidetone (0x03), Weight (0x04)
     * - PTT timing (0x05)
     * - Clear buffer (0x0A)
     * - Message slots (0x1x)
     * - Status responses
     */
    return ESP_OK;
}

bool usb_winkeyer_is_enabled(void) {
    return false;
}
```

**Step 2: Commit**

```bash
git add components/keyer_usb/src/usb_winkeyer.c
git commit -m "feat(usb): add usb_winkeyer.c stub"
```

---

## Task 11: Implement usb_uf2.c

**Files:**
- Create: `components/keyer_usb/src/usb_uf2.c`

**Step 1: Create usb_uf2.c**

Create `components/keyer_usb/src/usb_uf2.c`:

```c
/**
 * @file usb_uf2.c
 * @brief UF2 bootloader entry via command
 */

#include "usb_uf2.h"
#include "esp_tinyuf2.h"
#include "esp_log.h"

static const char *TAG = "usb_uf2";

static void uf2_update_complete_cb(esp_tinyuf2_ota_state_t state, uint32_t size) {
    if (state == ESP_TINYUF2_OTA_COMPLETE) {
        ESP_LOGI(TAG, "UF2 update complete: %lu bytes", (unsigned long)size);
    }
}

esp_err_t usb_uf2_init(void) {
    ESP_LOGI(TAG, "Initializing UF2 support");

    tinyuf2_ota_config_t ota_config = DEFAULT_TINYUF2_OTA_CONFIG();
    ota_config.complete_cb = uf2_update_complete_cb;
    ota_config.if_restart = true;

    esp_err_t ret = esp_tinyuf2_install(&ota_config, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_tinyuf2_install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

void usb_uf2_enter(void) {
    ESP_LOGI(TAG, "Entering UF2 bootloader mode...");
    esp_restart_from_tinyuf2();
    /* Does not return */
}
```

**Step 2: Commit**

```bash
git add components/keyer_usb/src/usb_uf2.c
git commit -m "feat(usb): implement usb_uf2.c with esp_tinyuf2"
```

---

## Task 12: Add console_push_char to keyer_console

**Files:**
- Modify: `components/keyer_console/include/console.h`
- Modify: `components/keyer_console/src/console.c`

**Step 1: Add declaration to console.h**

Edit `components/keyer_console/include/console.h`, add after `console_process_char` declaration (around line 132):

```c
/**
 * @brief Push character to console (from USB callback)
 *
 * Same as console_process_char but without echo (echo handled by USB).
 *
 * @param c Input character
 * @return true if command was executed
 */
bool console_push_char(char c);
```

**Step 2: Modify console.c to separate echo from processing**

Edit `components/keyer_console/src/console.c`. The current `console_process_char` does echo via `putchar()`. We need to:
1. Rename current function to `console_push_char` (no echo)
2. Create new `console_process_char` that echoes then calls `console_push_char`

Replace the existing `console_process_char` function:

```c
bool console_push_char(char c) {
    if (c == '\r' || c == '\n') {
        if (s_line_pos > 0) {
            s_line_buf[s_line_pos] = '\0';

            /* Parse and execute command */
            console_parsed_cmd_t cmd;
            console_parse_line(s_line_buf, &cmd);

            console_error_t err = console_execute(&cmd);
            if (err != CONSOLE_OK) {
                printf("%s: %s\r\n",
                       console_error_code(err),
                       console_error_message(err));
            }

            s_line_pos = 0;
            return true;
        }
        return false;
    } else if (c == '\b' || c == 0x7F) {
        /* Backspace */
        if (s_line_pos > 0) {
            s_line_pos--;
        }
    } else if (c == 0x03) {
        /* Ctrl+C - cancel current line */
        s_line_pos = 0;
        return true;
    } else if (c == 0x15) {
        /* Ctrl+U - clear line */
        s_line_pos = 0;
    } else if (c >= 0x20 && c <= 0x7E) {
        /* Printable character */
        if (s_line_pos < CONSOLE_LINE_MAX - 1) {
            s_line_buf[s_line_pos++] = c;
        }
    }

    return false;
}

bool console_process_char(char c) {
    /* Echo character (for non-USB usage) */
    if (c >= 0x20 && c <= 0x7E) {
        putchar(c);
    } else if (c == '\r' || c == '\n') {
        printf("\r\n");
    } else if (c == '\b' || c == 0x7F) {
        printf("\b \b");
    } else if (c == 0x03) {
        printf("^C\r\n");
    }
    fflush(stdout);

    return console_push_char(c);
}
```

**Step 3: Commit**

```bash
git add components/keyer_console/
git commit -m "refactor(console): separate echo from char processing"
```

---

## Task 13: Update main.c for USB CDC

**Files:**
- Modify: `main/main.c`

**Step 1: Add USB includes**

Edit `main/main.c`, add include after line 21:

```c
#include "usb_cdc.h"
#include "usb_log.h"
```

**Step 2: Initialize USB CDC early in app_main**

Edit `main/main.c`, add after NVS initialization (after line 48):

```c
    /* Initialize USB CDC (before console) */
    ESP_ERROR_CHECK(usb_cdc_init());
```

**Step 3: Replace uart_logger_task with usb_log_task**

Edit `main/main.c`, replace the uart_logger_task creation (lines 98-107) with:

```c
    /* Create USB log drain task on Core 1 */
    xTaskCreatePinnedToCore(
        usb_log_task,
        "usb_log",
        4096,
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL,
        1  /* Core 1 */
    );
```

**Step 4: Remove console_task creation**

Delete the console_task creation block (lines 109-118). Console is now handled by USB RX callback.

**Step 5: Commit**

```bash
git add main/main.c
git commit -m "feat(main): integrate USB CDC, replace UART logger"
```

---

## Task 14: Add Console Commands for Log and UF2

**Files:**
- Modify: `components/keyer_console/src/commands.c`

**Step 1: Read current commands.c structure**

First read the file to understand the current command registration pattern.

**Step 2: Add log command**

Add to commands.c:

```c
#include "usb_log.h"
#include "usb_uf2.h"

static console_error_t cmd_log(const console_parsed_cmd_t *cmd) {
    if (cmd->argc == 0) {
        /* Show current level */
        printf("Log level: %s\r\n", log_level_str(usb_log_get_level()));
        return CONSOLE_OK;
    }

    /* Parse "level TAG LEVEL" or "TAG=L" format */
    const char *arg = cmd->args[0];

    if (strcmp(arg, "level") == 0) {
        /* ESP-IDF style: log level TAG LEVEL */
        if (cmd->argc < 3) {
            return CONSOLE_ERR_MISSING_ARG;
        }
        const char *tag = cmd->args[1];
        const char *level_str = cmd->args[2];

        log_level_t level;
        if (strcmp(level_str, "ERROR") == 0 || strcmp(level_str, "E") == 0) {
            level = LOG_LEVEL_ERROR;
        } else if (strcmp(level_str, "WARN") == 0 || strcmp(level_str, "W") == 0) {
            level = LOG_LEVEL_WARN;
        } else if (strcmp(level_str, "INFO") == 0 || strcmp(level_str, "I") == 0) {
            level = LOG_LEVEL_INFO;
        } else if (strcmp(level_str, "DEBUG") == 0 || strcmp(level_str, "D") == 0) {
            level = LOG_LEVEL_DEBUG;
        } else if (strcmp(level_str, "TRACE") == 0 || strcmp(level_str, "T") == 0) {
            level = LOG_LEVEL_TRACE;
        } else {
            return CONSOLE_ERR_INVALID_VALUE;
        }

        if (strcmp(tag, "*") == 0) {
            usb_log_set_level(level);
        } else {
            usb_log_set_tag_level(tag, level);
        }
        printf("Log %s = %s\r\n", tag, log_level_str(level));
        return CONSOLE_OK;
    }

    /* Compact style: TAG=L */
    char *eq = strchr(arg, '=');
    if (eq != NULL) {
        *eq = '\0';
        const char *tag = arg;
        const char *level_char = eq + 1;

        log_level_t level;
        switch (*level_char) {
            case 'E': level = LOG_LEVEL_ERROR; break;
            case 'W': level = LOG_LEVEL_WARN; break;
            case 'I': level = LOG_LEVEL_INFO; break;
            case 'D': level = LOG_LEVEL_DEBUG; break;
            case 'T': level = LOG_LEVEL_TRACE; break;
            default: return CONSOLE_ERR_INVALID_VALUE;
        }

        if (strcmp(tag, "*") == 0) {
            usb_log_set_level(level);
        } else {
            usb_log_set_tag_level(tag, level);
        }
        printf("Log %s = %s\r\n", tag, log_level_str(level));
        return CONSOLE_OK;
    }

    return CONSOLE_ERR_INVALID_VALUE;
}

static console_error_t cmd_uf2(const console_parsed_cmd_t *cmd) {
    (void)cmd;
    printf("Entering UF2 bootloader mode...\r\n");
    usb_uf2_enter();
    /* Does not return */
    return CONSOLE_OK;
}
```

**Step 3: Register commands in command table**

Add to the command table array:

```c
    { "log",  "Set log level (log level TAG LEVEL or log TAG=L)", cmd_log },
    { "uf2",  "Enter UF2 bootloader mode", cmd_uf2 },
```

**Step 4: Commit**

```bash
git add components/keyer_console/src/commands.c
git commit -m "feat(console): add log and uf2 commands"
```

---

## Task 15: Build and Test

**Step 1: Clean and reconfigure**

```bash
cd /workspaces/RemoteCWKeyerV3
rm -rf build
idf.py set-target esp32s3
```

**Step 2: Build**

```bash
idf.py build
```

Expected: Build succeeds with no errors.

**Step 3: If build fails, fix errors**

Common issues:
- Missing includes
- Wrong TinyUSB API names
- Component dependency order

**Step 4: Commit final fixes if needed**

```bash
git add -A
git commit -m "fix: resolve build errors for USB multi-CDC"
```

---

## Task 16: Update Documentation

**Files:**
- Modify: `CLAUDE.md`

**Step 1: Add USB CDC section to CLAUDE.md**

Add section documenting the new USB architecture:

```markdown
## USB Architecture

The project uses TinyUSB with multiple CDC endpoints:

| CDC | Port | Function |
|-----|------|----------|
| CDC0 | /dev/ttyACM0 | Console (interactive commands) |
| CDC1 | /dev/ttyACM1 | Log output (RT-safe drain) |
| CDC2 | /dev/ttyACM2 | Winkeyer3 (future) |

### Console Commands

- `log` - Show current log level
- `log level TAG LEVEL` - Set log level (ERROR, WARN, INFO, DEBUG, TRACE)
- `log TAG=L` - Compact syntax (E, W, I, D, T)
- `uf2` - Enter UF2 bootloader for firmware update
```

**Step 2: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: add USB CDC architecture to CLAUDE.md"
```

---

## Summary

This plan implements:
1. TinyUSB multi-CDC (2 ports, stub for 3rd)
2. Immediate echo on CDC0 console (fixes original bug)
3. RT-safe log drain on CDC1 with filtering
4. UF2 firmware update via console command
5. Winkeyer3 stub for future expansion

Total: 16 tasks with granular steps for TDD workflow.

---

Plan complete and saved to `docs/plans/2024-12-22-usb-multi-cdc-implementation.md`. Two execution options:

**1. Subagent-Driven (this session)** - I dispatch fresh subagent per task, review between tasks, fast iteration

**2. Parallel Session (separate)** - Open new session with executing-plans, batch execution with checkpoints

**Which approach?**
