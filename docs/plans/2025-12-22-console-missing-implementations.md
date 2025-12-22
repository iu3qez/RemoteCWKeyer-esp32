# Console Missing Implementations Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Complete the console implementation to match the design document [2025-12-20-console-commands-design.md](2025-12-20-console-commands-design.md).

**Architecture:** Console runs on Core 1 (best-effort), uses static buffers only (zero heap), integrates with the existing keyer_config registry via config_console.h. All commands interact with configuration through the registry pattern.

**Tech Stack:** C (ESP-IDF), Unity for tests, stdatomic.h for config access

---

## Gap Analysis

| Feature | Design Doc | Current State |
|---------|-----------|---------------|
| config_console.c (CONSOLE_PARAMS) | Required | **Missing** - header exists, no implementation |
| history.c | Arrow key navigation | **Stub** |
| completion.c | Tab completion | **Stub** |
| `show` command | Uses config registry | Hardcoded values |
| `set` command | Uses config registry + validation | Hardcoded values |
| `save` command | Calls config_save_to_nvs() | Only prints message |
| `factory-reset confirm` | Erases NVS + reboot | **Missing** |
| `debug` command | ESP-IDF log levels | Implemented as `log` |
| `flash` command | Bootloader mode | Implemented as `uf2` |

---

### Task 1: Implement config_console.c (Parameter Registry)

**Files:**
- Create: `components/keyer_config/src/config_console.c`
- Modify: `components/keyer_config/CMakeLists.txt`
- Test: `test_host/test_config_console.c`

**Step 1: Write the failing test**

```c
// test_host/test_config_console.c
#include "unity.h"
#include "config_console.h"
#include "config.h"

void test_config_find_param_wpm(void) {
    const param_descriptor_t *p = config_find_param("wpm");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("wpm", p->name);
    TEST_ASSERT_EQUAL_STRING("keyer", p->category);
    TEST_ASSERT_EQUAL(PARAM_TYPE_U16, p->type);
    TEST_ASSERT_EQUAL(5, p->min);
    TEST_ASSERT_EQUAL(100, p->max);
}

void test_config_find_param_unknown(void) {
    const param_descriptor_t *p = config_find_param("nonexistent");
    TEST_ASSERT_NULL(p);
}

void test_config_get_param_str_wpm(void) {
    config_init_defaults(&g_config);
    char buf[32];
    int ret = config_get_param_str("wpm", buf, sizeof(buf));
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL_STRING("25", buf);
}

void test_config_set_param_str_wpm(void) {
    config_init_defaults(&g_config);
    int ret = config_set_param_str("wpm", "30");
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(30, CONFIG_GET_WPM());
}

void test_config_set_param_str_out_of_range(void) {
    config_init_defaults(&g_config);
    int ret = config_set_param_str("wpm", "200");
    TEST_ASSERT_EQUAL(-4, ret);  /* CONSOLE_ERR_OUT_OF_RANGE */
}
```

**Step 2: Run test to verify it fails**

Run: `cd test_host && cmake -B build && cmake --build build && ./build/test_runner`
Expected: Linker errors (undefined reference to config_find_param, etc.)

**Step 3: Write minimal implementation**

```c
// components/keyer_config/src/config_console.c
#include "config_console.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Get/set function implementations */
static param_value_t get_wpm(void) {
    param_value_t v;
    v.u16 = atomic_load_explicit(&g_config.wpm, memory_order_relaxed);
    return v;
}
static void set_wpm(param_value_t v) {
    atomic_store_explicit(&g_config.wpm, v.u16, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_iambic_mode(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.iambic_mode, memory_order_relaxed);
    return v;
}
static void set_iambic_mode(param_value_t v) {
    atomic_store_explicit(&g_config.iambic_mode, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_memory_window_us(void) {
    param_value_t v;
    v.u32 = atomic_load_explicit(&g_config.memory_window_us, memory_order_relaxed);
    return v;
}
static void set_memory_window_us(param_value_t v) {
    atomic_store_explicit(&g_config.memory_window_us, v.u32, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_weight(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.weight, memory_order_relaxed);
    return v;
}
static void set_weight(param_value_t v) {
    atomic_store_explicit(&g_config.weight, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_sidetone_freq_hz(void) {
    param_value_t v;
    v.u16 = atomic_load_explicit(&g_config.sidetone_freq_hz, memory_order_relaxed);
    return v;
}
static void set_sidetone_freq_hz(param_value_t v) {
    atomic_store_explicit(&g_config.sidetone_freq_hz, v.u16, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_sidetone_volume(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.sidetone_volume, memory_order_relaxed);
    return v;
}
static void set_sidetone_volume(param_value_t v) {
    atomic_store_explicit(&g_config.sidetone_volume, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_fade_duration_ms(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.fade_duration_ms, memory_order_relaxed);
    return v;
}
static void set_fade_duration_ms(param_value_t v) {
    atomic_store_explicit(&g_config.fade_duration_ms, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_gpio_dit(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.gpio_dit, memory_order_relaxed);
    return v;
}
static void set_gpio_dit(param_value_t v) {
    atomic_store_explicit(&g_config.gpio_dit, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_gpio_dah(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.gpio_dah, memory_order_relaxed);
    return v;
}
static void set_gpio_dah(param_value_t v) {
    atomic_store_explicit(&g_config.gpio_dah, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_gpio_tx(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.gpio_tx, memory_order_relaxed);
    return v;
}
static void set_gpio_tx(param_value_t v) {
    atomic_store_explicit(&g_config.gpio_tx, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_ptt_tail_ms(void) {
    param_value_t v;
    v.u32 = atomic_load_explicit(&g_config.ptt_tail_ms, memory_order_relaxed);
    return v;
}
static void set_ptt_tail_ms(param_value_t v) {
    atomic_store_explicit(&g_config.ptt_tail_ms, v.u32, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_tick_rate_hz(void) {
    param_value_t v;
    v.u32 = atomic_load_explicit(&g_config.tick_rate_hz, memory_order_relaxed);
    return v;
}
static void set_tick_rate_hz(param_value_t v) {
    atomic_store_explicit(&g_config.tick_rate_hz, v.u32, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_debug_logging(void) {
    param_value_t v;
    v.b = atomic_load_explicit(&g_config.debug_logging, memory_order_relaxed);
    return v;
}
static void set_debug_logging(param_value_t v) {
    atomic_store_explicit(&g_config.debug_logging, v.b, memory_order_relaxed);
    config_bump_generation(&g_config);
}

static param_value_t get_led_brightness(void) {
    param_value_t v;
    v.u8 = atomic_load_explicit(&g_config.led_brightness, memory_order_relaxed);
    return v;
}
static void set_led_brightness(param_value_t v) {
    atomic_store_explicit(&g_config.led_brightness, v.u8, memory_order_relaxed);
    config_bump_generation(&g_config);
}

/* Parameter registry */
const param_descriptor_t CONSOLE_PARAMS[CONSOLE_PARAM_COUNT] = {
    { "wpm", "keyer", PARAM_TYPE_U16, 5, 100, get_wpm, set_wpm },
    { "iambic_mode", "keyer", PARAM_TYPE_ENUM, 0, 1, get_iambic_mode, set_iambic_mode },
    { "memory_window_us", "keyer", PARAM_TYPE_U32, 0, 1000, get_memory_window_us, set_memory_window_us },
    { "weight", "keyer", PARAM_TYPE_U8, 33, 67, get_weight, set_weight },
    { "sidetone_freq_hz", "audio", PARAM_TYPE_U16, 400, 800, get_sidetone_freq_hz, set_sidetone_freq_hz },
    { "sidetone_volume", "audio", PARAM_TYPE_U8, 1, 100, get_sidetone_volume, set_sidetone_volume },
    { "fade_duration_ms", "audio", PARAM_TYPE_U8, 1, 10, get_fade_duration_ms, set_fade_duration_ms },
    { "gpio_dit", "hardware", PARAM_TYPE_U8, 0, 45, get_gpio_dit, set_gpio_dit },
    { "gpio_dah", "hardware", PARAM_TYPE_U8, 0, 45, get_gpio_dah, set_gpio_dah },
    { "gpio_tx", "hardware", PARAM_TYPE_U8, 0, 45, get_gpio_tx, set_gpio_tx },
    { "ptt_tail_ms", "timing", PARAM_TYPE_U32, 50, 500, get_ptt_tail_ms, set_ptt_tail_ms },
    { "tick_rate_hz", "timing", PARAM_TYPE_U32, 1000, 10000, get_tick_rate_hz, set_tick_rate_hz },
    { "debug_logging", "system", PARAM_TYPE_BOOL, 0, 1, get_debug_logging, set_debug_logging },
    { "led_brightness", "system", PARAM_TYPE_U8, 0, 100, get_led_brightness, set_led_brightness },
};

const char *CATEGORIES[CATEGORY_COUNT] = {
    "keyer", "audio", "hardware", "timing", "system"
};

const param_descriptor_t *config_find_param(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < CONSOLE_PARAM_COUNT; i++) {
        if (strcmp(CONSOLE_PARAMS[i].name, name) == 0) {
            return &CONSOLE_PARAMS[i];
        }
    }
    return NULL;
}

int config_get_param_str(const char *name, char *buf, size_t len) {
    const param_descriptor_t *p = config_find_param(name);
    if (p == NULL) {
        return -1;  /* Unknown parameter */
    }

    param_value_t v = p->get_fn();

    switch (p->type) {
        case PARAM_TYPE_U8:
            snprintf(buf, len, "%u", v.u8);
            break;
        case PARAM_TYPE_U16:
            snprintf(buf, len, "%u", v.u16);
            break;
        case PARAM_TYPE_U32:
            snprintf(buf, len, "%lu", (unsigned long)v.u32);
            break;
        case PARAM_TYPE_BOOL:
            snprintf(buf, len, "%s", v.b ? "true" : "false");
            break;
        case PARAM_TYPE_ENUM:
            snprintf(buf, len, "%u", v.u8);
            break;
    }
    return 0;
}

int config_set_param_str(const char *name, const char *value) {
    const param_descriptor_t *p = config_find_param(name);
    if (p == NULL) {
        return -1;  /* Unknown parameter */
    }

    param_value_t v;
    char *endptr;
    unsigned long ul;

    switch (p->type) {
        case PARAM_TYPE_U8:
        case PARAM_TYPE_ENUM:
            ul = strtoul(value, &endptr, 10);
            if (*endptr != '\0') {
                return -2;  /* Invalid value */
            }
            if (ul < p->min || ul > p->max) {
                return -4;  /* Out of range */
            }
            v.u8 = (uint8_t)ul;
            break;

        case PARAM_TYPE_U16:
            ul = strtoul(value, &endptr, 10);
            if (*endptr != '\0') {
                return -2;
            }
            if (ul < p->min || ul > p->max) {
                return -4;
            }
            v.u16 = (uint16_t)ul;
            break;

        case PARAM_TYPE_U32:
            ul = strtoul(value, &endptr, 10);
            if (*endptr != '\0') {
                return -2;
            }
            if (ul < p->min || ul > p->max) {
                return -4;
            }
            v.u32 = (uint32_t)ul;
            break;

        case PARAM_TYPE_BOOL:
            if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
                v.b = true;
            } else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
                v.b = false;
            } else {
                return -2;  /* Invalid value */
            }
            break;
    }

    p->set_fn(v);
    return 0;
}
```

**Step 4: Update CMakeLists.txt**

```cmake
# components/keyer_config/CMakeLists.txt - add config_console.c to SRCS
```

**Step 5: Run test to verify it passes**

Run: `cd test_host && cmake --build build && ./build/test_runner`
Expected: PASS

**Step 6: Commit**

```bash
git add components/keyer_config/src/config_console.c components/keyer_config/CMakeLists.txt test_host/test_config_console.c
git commit -m "feat(config): implement parameter registry for console"
```

---

### Task 2: Integrate show/set Commands with Config Registry

**Files:**
- Modify: `components/keyer_console/src/commands.c:179-199`
- Modify: `components/keyer_console/CMakeLists.txt` (add keyer_config dependency)

**Step 1: Update cmd_show to use registry**

Replace the hardcoded `cmd_show` with:

```c
static console_error_t cmd_show(const console_parsed_cmd_t *cmd) {
    const char *pattern = (cmd->argc > 0) ? cmd->args[0] : NULL;
    size_t pattern_len = pattern ? strlen(pattern) : 0;
    bool has_wildcard = pattern && pattern[pattern_len - 1] == '*';

    if (has_wildcard) {
        pattern_len--;  /* Exclude the wildcard */
    }

    for (size_t i = 0; i < CONSOLE_PARAM_COUNT; i++) {
        const param_descriptor_t *p = &CONSOLE_PARAMS[i];
        bool match = false;

        if (pattern == NULL) {
            match = true;
        } else if (has_wildcard) {
            match = (strncmp(p->name, pattern, pattern_len) == 0) ||
                    (strncmp(p->category, pattern, pattern_len) == 0);
        } else {
            match = (strcmp(p->name, pattern) == 0);
        }

        if (match) {
            char buf[32];
            config_get_param_str(p->name, buf, sizeof(buf));
            printf("%s=%s\r\n", p->name, buf);
        }
    }
    return CONSOLE_OK;
}
```

**Step 2: Update cmd_set to use registry**

Replace the hardcoded `cmd_set` with:

```c
static console_error_t cmd_set(const console_parsed_cmd_t *cmd) {
    if (cmd->argc < 2) {
        return CONSOLE_ERR_MISSING_ARG;
    }

    int ret = config_set_param_str(cmd->args[0], cmd->args[1]);
    switch (ret) {
        case 0:
            return CONSOLE_OK;
        case -1:
            return CONSOLE_ERR_UNKNOWN_CMD;
        case -2:
            return CONSOLE_ERR_INVALID_VALUE;
        case -4:
            return CONSOLE_ERR_OUT_OF_RANGE;
        default:
            return CONSOLE_ERR_INVALID_VALUE;
    }
}
```

**Step 3: Add include to commands.c**

```c
#include "config_console.h"
```

**Step 4: Build and test**

Run: `idf.py build`
Expected: Build succeeds

**Step 5: Commit**

```bash
git add components/keyer_console/src/commands.c components/keyer_console/CMakeLists.txt
git commit -m "feat(console): integrate show/set with config registry"
```

---

### Task 3: Implement save Command with NVS Persistence

**Files:**
- Modify: `components/keyer_console/src/commands.c:169-175`

**Step 1: Update cmd_save to call config_save_to_nvs**

```c
static console_error_t cmd_save(const console_parsed_cmd_t *cmd) {
    (void)cmd;
#ifdef CONFIG_IDF_TARGET
    int ret = config_save_to_nvs();
    if (ret < 0) {
        return CONSOLE_ERR_NVS_ERROR;
    }
    printf("Saved %d parameters to NVS\r\n", ret);
#else
    printf("NVS not available on host\r\n");
#endif
    return CONSOLE_OK;
}
```

**Step 2: Add include**

```c
#include "config_nvs.h"
```

**Step 3: Build**

Run: `idf.py build`
Expected: Build succeeds

**Step 4: Commit**

```bash
git add components/keyer_console/src/commands.c
git commit -m "feat(console): implement save command with NVS persistence"
```

---

### Task 4: Add factory-reset Command

**Files:**
- Modify: `components/keyer_console/src/commands.c`

**Step 1: Add cmd_factory_reset handler**

```c
/**
 * @brief factory-reset confirm - Erase NVS and reboot
 */
static console_error_t cmd_factory_reset(const console_parsed_cmd_t *cmd) {
    if (cmd->argc == 0 || strcmp(cmd->args[0], "confirm") != 0) {
        return CONSOLE_ERR_REQUIRES_CONFIRM;
    }
#ifdef CONFIG_IDF_TARGET
    printf("Erasing NVS and rebooting...\r\n");

    /* Erase the keyer config namespace */
    nvs_handle_t handle;
    esp_err_t err = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
#else
    printf("factory-reset not available on host\r\n");
#endif
    return CONSOLE_OK;
}
```

**Step 2: Add to command registry**

```c
static const console_cmd_t s_commands[] = {
    // ... existing commands ...
    { "factory-reset", "Erase NVS and reboot", cmd_factory_reset },
};
```

**Step 3: Add includes**

```c
#include "nvs.h"
#include "config_nvs.h"
```

**Step 4: Build**

Run: `idf.py build`
Expected: Build succeeds

**Step 5: Commit**

```bash
git add components/keyer_console/src/commands.c
git commit -m "feat(console): add factory-reset command"
```

---

### Task 5: Rename debug Command to Match Design (ESP-IDF Log Levels)

**Files:**
- Modify: `components/keyer_console/src/commands.c`

**Step 1: Add cmd_debug handler with ESP-IDF log levels**

The design document specifies `debug` command syntax:
- `debug none` - disable all logging
- `debug * verbose` - all verbose
- `debug wifi warn` - set wifi tag to warn
- `debug info` - show RT log buffer status

```c
/**
 * @brief debug - Set ESP-IDF log levels
 */
static console_error_t cmd_debug(const console_parsed_cmd_t *cmd) {
#ifdef CONFIG_IDF_TARGET
    if (cmd->argc == 0) {
        /* Show current RT log status */
        printf("RT Log: (use 'debug info' for details)\r\n");
        return CONSOLE_OK;
    }

    const char *arg1 = cmd->args[0];

    /* debug info - show RT log buffer status */
    if (strcmp(arg1, "info") == 0) {
        printf("RT Log: buffer status\r\n");
        printf("(RT log stats not yet implemented)\r\n");
        return CONSOLE_OK;
    }

    /* debug none - disable all logging */
    if (strcmp(arg1, "none") == 0) {
        esp_log_level_set("*", ESP_LOG_NONE);
        printf("All logging disabled\r\n");
        return CONSOLE_OK;
    }

    /* debug <tag> <level> */
    if (cmd->argc < 2) {
        return CONSOLE_ERR_MISSING_ARG;
    }

    const char *tag = arg1;
    const char *level_str = cmd->args[1];
    esp_log_level_t level;

    if (strcmp(level_str, "none") == 0) {
        level = ESP_LOG_NONE;
    } else if (strcmp(level_str, "error") == 0) {
        level = ESP_LOG_ERROR;
    } else if (strcmp(level_str, "warn") == 0) {
        level = ESP_LOG_WARN;
    } else if (strcmp(level_str, "info") == 0) {
        level = ESP_LOG_INFO;
    } else if (strcmp(level_str, "debug") == 0) {
        level = ESP_LOG_DEBUG;
    } else if (strcmp(level_str, "verbose") == 0) {
        level = ESP_LOG_VERBOSE;
    } else {
        return CONSOLE_ERR_INVALID_VALUE;
    }

    esp_log_level_set(tag, level);
    printf("Log %s = %s\r\n", tag, level_str);
    return CONSOLE_OK;
#else
    (void)cmd;
    printf("debug not available on host\r\n");
    return CONSOLE_OK;
#endif
}
```

**Step 2: Update command registry**

Add `debug` command and keep `log` as alias if desired.

**Step 3: Add include**

```c
#include "esp_log.h"
```

**Step 4: Build**

Run: `idf.py build`
Expected: Build succeeds

**Step 5: Commit**

```bash
git add components/keyer_console/src/commands.c
git commit -m "feat(console): add debug command for ESP-IDF log levels"
```

---

### Task 6: Implement Command History

**Files:**
- Modify: `components/keyer_console/src/history.c`
- Modify: `components/keyer_console/include/console.h`
- Modify: `components/keyer_console/src/console.c`
- Test: `test_host/test_history.c`

**Step 1: Write the failing test**

```c
// test_host/test_history.c
#include "unity.h"
#include "console.h"

void test_history_push_and_prev(void) {
    console_history_init();
    console_history_push("help");
    console_history_push("show");

    const char *line = console_history_prev();
    TEST_ASSERT_EQUAL_STRING("show", line);

    line = console_history_prev();
    TEST_ASSERT_EQUAL_STRING("help", line);
}

void test_history_next(void) {
    console_history_init();
    console_history_push("cmd1");
    console_history_push("cmd2");

    console_history_prev();  /* cmd2 */
    console_history_prev();  /* cmd1 */

    const char *line = console_history_next();
    TEST_ASSERT_EQUAL_STRING("cmd2", line);
}

void test_history_wrap_at_depth(void) {
    console_history_init();
    console_history_push("old1");
    console_history_push("old2");
    console_history_push("old3");
    console_history_push("old4");
    console_history_push("new");  /* Should overwrite old1 */

    /* Navigate back - should only find 4 entries */
    const char *line = console_history_prev();
    TEST_ASSERT_EQUAL_STRING("new", line);
    line = console_history_prev();
    TEST_ASSERT_EQUAL_STRING("old4", line);
    line = console_history_prev();
    TEST_ASSERT_EQUAL_STRING("old3", line);
    line = console_history_prev();
    TEST_ASSERT_EQUAL_STRING("old2", line);
    line = console_history_prev();
    TEST_ASSERT_NULL(line);  /* No more history */
}
```

**Step 2: Run test to verify it fails**

Run: `cd test_host && cmake --build build && ./build/test_runner`
Expected: Linker errors

**Step 3: Write implementation**

```c
// components/keyer_console/src/history.c
#include "console.h"
#include <string.h>

static char s_history[CONSOLE_HISTORY_SIZE][CONSOLE_LINE_MAX];
static size_t s_history_count = 0;
static size_t s_history_write = 0;
static size_t s_history_nav = 0;
static bool s_navigating = false;

void console_history_init(void) {
    s_history_count = 0;
    s_history_write = 0;
    s_history_nav = 0;
    s_navigating = false;
    memset(s_history, 0, sizeof(s_history));
}

void console_history_push(const char *line) {
    if (line == NULL || line[0] == '\0') {
        return;
    }

    /* Don't add duplicates of the last entry */
    if (s_history_count > 0) {
        size_t last = (s_history_write + CONSOLE_HISTORY_SIZE - 1) % CONSOLE_HISTORY_SIZE;
        if (strcmp(s_history[last], line) == 0) {
            return;
        }
    }

    strncpy(s_history[s_history_write], line, CONSOLE_LINE_MAX - 1);
    s_history[s_history_write][CONSOLE_LINE_MAX - 1] = '\0';

    s_history_write = (s_history_write + 1) % CONSOLE_HISTORY_SIZE;
    if (s_history_count < CONSOLE_HISTORY_SIZE) {
        s_history_count++;
    }

    s_navigating = false;
}

const char *console_history_prev(void) {
    if (s_history_count == 0) {
        return NULL;
    }

    if (!s_navigating) {
        s_history_nav = s_history_write;
        s_navigating = true;
    }

    /* Calculate how far back we can go */
    size_t oldest = (s_history_write + CONSOLE_HISTORY_SIZE - s_history_count) % CONSOLE_HISTORY_SIZE;
    size_t prev = (s_history_nav + CONSOLE_HISTORY_SIZE - 1) % CONSOLE_HISTORY_SIZE;

    if (s_history_nav == oldest) {
        return NULL;  /* Already at oldest */
    }

    s_history_nav = prev;
    return s_history[s_history_nav];
}

const char *console_history_next(void) {
    if (!s_navigating || s_history_count == 0) {
        return NULL;
    }

    size_t next = (s_history_nav + 1) % CONSOLE_HISTORY_SIZE;

    if (next == s_history_write) {
        s_navigating = false;
        return NULL;  /* Back to current input */
    }

    s_history_nav = next;
    return s_history[s_history_nav];
}

void console_history_reset_nav(void) {
    s_navigating = false;
}
```

**Step 4: Add declarations to console.h**

```c
void console_history_init(void);
void console_history_push(const char *line);
const char *console_history_prev(void);
const char *console_history_next(void);
void console_history_reset_nav(void);
```

**Step 5: Integrate into console.c**

Handle escape sequences for arrow keys (`\x1b[A` up, `\x1b[B` down) in `console_push_char`.

**Step 6: Run tests**

Run: `cd test_host && cmake --build build && ./build/test_runner`
Expected: PASS

**Step 7: Commit**

```bash
git add components/keyer_console/src/history.c components/keyer_console/include/console.h components/keyer_console/src/console.c test_host/test_history.c
git commit -m "feat(console): implement command history with arrow keys"
```

---

### Task 7: Implement Tab Completion

**Files:**
- Modify: `components/keyer_console/src/completion.c`
- Modify: `components/keyer_console/include/console.h`
- Modify: `components/keyer_console/src/console.c`
- Test: `test_host/test_completion.c`

**Step 1: Write the failing test**

```c
// test_host/test_completion.c
#include "unity.h"
#include "console.h"

void test_complete_command_help(void) {
    char line[64] = "hel";
    size_t pos = 3;

    bool completed = console_complete(line, &pos, sizeof(line));
    TEST_ASSERT_TRUE(completed);
    TEST_ASSERT_EQUAL_STRING("help", line);
    TEST_ASSERT_EQUAL(4, pos);
}

void test_complete_param_after_set(void) {
    char line[64] = "set wp";
    size_t pos = 6;

    bool completed = console_complete(line, &pos, sizeof(line));
    TEST_ASSERT_TRUE(completed);
    TEST_ASSERT_EQUAL_STRING("set wpm", line);
}

void test_complete_no_match(void) {
    char line[64] = "xyz";
    size_t pos = 3;

    bool completed = console_complete(line, &pos, sizeof(line));
    TEST_ASSERT_FALSE(completed);
}
```

**Step 2: Run test to verify it fails**

Run: `cd test_host && cmake --build build && ./build/test_runner`
Expected: Linker errors

**Step 3: Write implementation**

```c
// components/keyer_console/src/completion.c
#include "console.h"
#include "config_console.h"
#include <string.h>

static size_t s_last_match_idx = 0;
static size_t s_prefix_len = 0;
static bool s_cycling = false;

static const char *find_matching_command(const char *prefix, size_t len, size_t *start_idx) {
    size_t count;
    const console_cmd_t *cmds = console_get_commands(&count);

    for (size_t i = *start_idx; i < count; i++) {
        if (strncmp(cmds[i].name, prefix, len) == 0) {
            *start_idx = i + 1;
            return cmds[i].name;
        }
    }
    return NULL;
}

static const char *find_matching_param(const char *prefix, size_t len, size_t *start_idx) {
    for (size_t i = *start_idx; i < CONSOLE_PARAM_COUNT; i++) {
        if (strncmp(CONSOLE_PARAMS[i].name, prefix, len) == 0) {
            *start_idx = i + 1;
            return CONSOLE_PARAMS[i].name;
        }
    }
    return NULL;
}

bool console_complete(char *line, size_t *pos, size_t max_len) {
    if (line == NULL || pos == NULL || *pos == 0) {
        return false;
    }

    /* Find start of current token */
    size_t token_start = *pos;
    while (token_start > 0 && line[token_start - 1] != ' ') {
        token_start--;
    }

    const char *prefix = &line[token_start];
    size_t prefix_len = *pos - token_start;

    /* Determine what we're completing */
    bool completing_param = (token_start > 0) &&
        (strncmp(line, "set ", 4) == 0 || strncmp(line, "show ", 5) == 0);

    /* Check if we're cycling through matches */
    if (s_cycling && s_prefix_len == prefix_len) {
        /* Continue from last match */
    } else {
        s_last_match_idx = 0;
        s_prefix_len = prefix_len;
        s_cycling = true;
    }

    const char *match = NULL;
    size_t idx = s_last_match_idx;

    if (completing_param) {
        match = find_matching_param(prefix, prefix_len, &idx);
    } else {
        match = find_matching_command(prefix, prefix_len, &idx);
    }

    if (match == NULL) {
        /* Wrap around */
        idx = 0;
        if (completing_param) {
            match = find_matching_param(prefix, prefix_len, &idx);
        } else {
            match = find_matching_command(prefix, prefix_len, &idx);
        }
    }

    if (match == NULL) {
        s_cycling = false;
        return false;
    }

    s_last_match_idx = idx;

    /* Replace the prefix with the match */
    size_t match_len = strlen(match);
    size_t new_line_len = token_start + match_len;

    if (new_line_len >= max_len) {
        return false;
    }

    strcpy(&line[token_start], match);
    line[new_line_len] = '\0';
    *pos = new_line_len;

    return true;
}

void console_complete_reset(void) {
    s_cycling = false;
    s_last_match_idx = 0;
}
```

**Step 4: Add declarations to console.h**

```c
bool console_complete(char *line, size_t *pos, size_t max_len);
void console_complete_reset(void);
```

**Step 5: Integrate into console.c**

Handle Tab character (0x09) in `console_push_char`.

**Step 6: Run tests**

Run: `cd test_host && cmake --build build && ./build/test_runner`
Expected: PASS

**Step 7: Commit**

```bash
git add components/keyer_console/src/completion.c components/keyer_console/include/console.h components/keyer_console/src/console.c test_host/test_completion.c
git commit -m "feat(console): implement tab completion for commands and parameters"
```

---

### Task 8: Add flash Command Alias

**Files:**
- Modify: `components/keyer_console/src/commands.c`

**Step 1: Add flash command alias**

The design doc mentions `flash` but implementation has `uf2`. Add alias:

```c
static const console_cmd_t s_commands[] = {
    // ... existing ...
    { "flash", "Enter bootloader mode", cmd_uf2 },  /* Alias for uf2 */
};
```

**Step 2: Build**

Run: `idf.py build`
Expected: Build succeeds

**Step 3: Commit**

```bash
git add components/keyer_console/src/commands.c
git commit -m "feat(console): add flash as alias for uf2 command"
```

---

## Summary

| Task | Description | Status |
|------|-------------|--------|
| 1 | Implement config_console.c parameter registry | Pending |
| 2 | Integrate show/set with config registry | Pending |
| 3 | Implement save with NVS persistence | Pending |
| 4 | Add factory-reset command | Pending |
| 5 | Add debug command for ESP-IDF log levels | Pending |
| 6 | Implement command history | Pending |
| 7 | Implement tab completion | Pending |
| 8 | Add flash command alias | Pending |

---

Plan complete and saved to `docs/plans/2025-12-22-console-missing-implementations.md`. Two execution options:

**1. Subagent-Driven (this session)** - I dispatch fresh subagent per task, review between tasks, fast iteration

**2. Parallel Session (separate)** - Open new session with executing-plans, batch execution with checkpoints

Which approach?
