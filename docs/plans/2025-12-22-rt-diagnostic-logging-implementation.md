# RT Diagnostic Logging Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add runtime-togglable diagnostic logging to keying and audio systems for production diagnostics.

**Architecture:** External logging in `rt_task.c` that observes state transitions without modifying pure-logic components. Uses atomic bool for zero-overhead when disabled.

**Tech Stack:** C11 stdatomic, ESP-IDF, FreeRTOS

---

## Task 1: Add Diagnostic Enable Flag to rt_log

**Files:**
- Modify: `components/keyer_logging/include/rt_log.h:82-89`
- Modify: `components/keyer_logging/src/log_stream.c:9-11`

**Step 1: Add extern declaration in rt_log.h**

After line 88 (after `extern log_stream_t g_bg_log_stream;`), add:

```c
/** Diagnostic logging enable flag (atomic for RT-safe access) */
extern atomic_bool g_rt_diag_enabled;
```

**Step 2: Add definition in log_stream.c**

After line 11 (after `log_stream_t g_bg_log_stream = LOG_STREAM_INIT;`), add:

```c
/* Diagnostic logging enable flag (default: off) */
atomic_bool g_rt_diag_enabled = false;
```

**Step 3: Build to verify no errors**

Run: `cd /workspaces/RustRemoteCWKeyer && idf.py build 2>&1 | head -50`
Expected: Build succeeds (or partial success showing no errors in keyer_logging)

**Step 4: Commit**

```bash
git add components/keyer_logging/include/rt_log.h components/keyer_logging/src/log_stream.c
git commit -m "feat(logging): add g_rt_diag_enabled atomic flag"
```

---

## Task 2: Add RT_DIAG_* Macros

**Files:**
- Modify: `components/keyer_logging/include/rt_log.h:175-209`

**Step 1: Add RT_DIAG macros after RT_TRACE macro (after line 209)**

```c
/* ============================================================================
 * Diagnostic Logging Macros (conditionally enabled at runtime)
 * ============================================================================ */

/**
 * @brief Diagnostic log macro (internal)
 *
 * Only logs if g_rt_diag_enabled is true. Single atomic load (~1 cycle).
 */
#define RT_DIAG_LOG(stream, level, ts, fmt, ...) do { \
    if (atomic_load_explicit(&g_rt_diag_enabled, memory_order_relaxed)) { \
        RT_LOG(stream, level, ts, fmt, ##__VA_ARGS__); \
    } \
} while(0)

/** Diagnostic info (key down/up events) */
#define RT_DIAG_INFO(stream, ts, fmt, ...) \
    RT_DIAG_LOG(stream, LOG_LEVEL_INFO, ts, fmt, ##__VA_ARGS__)

/** Diagnostic debug (timing details, audio state) */
#define RT_DIAG_DEBUG(stream, ts, fmt, ...) \
    RT_DIAG_LOG(stream, LOG_LEVEL_DEBUG, ts, fmt, ##__VA_ARGS__)

/** Diagnostic warning (timing drift) */
#define RT_DIAG_WARN(stream, ts, fmt, ...) \
    RT_DIAG_LOG(stream, LOG_LEVEL_WARN, ts, fmt, ##__VA_ARGS__)
```

**Step 2: Build to verify**

Run: `cd /workspaces/RustRemoteCWKeyer && idf.py build 2>&1 | head -50`
Expected: Build succeeds

**Step 3: Commit**

```bash
git add components/keyer_logging/include/rt_log.h
git commit -m "feat(logging): add RT_DIAG_* conditional macros"
```

---

## Task 3: Add Diagnostic State Tracking to rt_task.c

**Files:**
- Modify: `main/rt_task.c`

**Step 1: Add includes and diagnostic state struct after line 26**

After `#include "hal_gpio.h"`, add:

```c
/* Drift threshold: 5% */
#define DIAG_DRIFT_THRESHOLD_PCT 5
```

After line 34 (`static audio_ring_buffer_t s_audio_buffer;`), add:

```c
/* ============================================================================
 * Diagnostic State Tracking
 * ============================================================================ */

/**
 * @brief Diagnostic state for detecting transitions
 */
typedef struct {
    bool prev_key_down;              /**< Previous key state */
    fade_state_t prev_fade_state;    /**< Previous sidetone fade state */
    iambic_state_t prev_iambic_state; /**< Previous iambic FSM state */
    int64_t element_start_us;        /**< When current element started */
    int64_t expected_duration_us;    /**< Expected element duration */
} rt_diag_state_t;

static rt_diag_state_t s_diag = {0};

/**
 * @brief Get expected duration for current element
 */
static int64_t get_expected_duration(const iambic_processor_t *iambic) {
    switch (iambic->state) {
        case IAMBIC_STATE_SEND_DIT:
            return iambic_dit_duration_us(&iambic->config);
        case IAMBIC_STATE_SEND_DAH:
            return iambic_dah_duration_us(&iambic->config);
        default:
            return 0;
    }
}

/**
 * @brief Log diagnostic events based on state transitions
 */
static void rt_diag_log(rt_diag_state_t *diag,
                        const iambic_processor_t *iambic,
                        const sidetone_gen_t *sidetone,
                        bool key_down,
                        int64_t now_us) {
    /* Key down transition */
    if (key_down && !diag->prev_key_down) {
        diag->element_start_us = now_us;
        diag->expected_duration_us = get_expected_duration(iambic);

        if (iambic->state == IAMBIC_STATE_SEND_DIT) {
            RT_DIAG_INFO(&g_rt_log_stream, now_us, "KEY DIT down");
        } else if (iambic->state == IAMBIC_STATE_SEND_DAH) {
            RT_DIAG_INFO(&g_rt_log_stream, now_us, "KEY DAH down");
        }
    }

    /* Key up transition */
    if (!key_down && diag->prev_key_down) {
        int64_t actual_duration = now_us - diag->element_start_us;
        RT_DIAG_INFO(&g_rt_log_stream, now_us, "KEY up %lldus",
                     (long long)actual_duration);

        /* Check for timing drift */
        if (diag->expected_duration_us > 0) {
            int64_t diff = actual_duration - diag->expected_duration_us;
            if (diff < 0) diff = -diff;
            int64_t drift_pct = (diff * 100) / diag->expected_duration_us;

            if (drift_pct > DIAG_DRIFT_THRESHOLD_PCT) {
                RT_DIAG_WARN(&g_rt_log_stream, now_us,
                             "DRIFT %lld%% (exp=%lld act=%lld)",
                             (long long)drift_pct,
                             (long long)diag->expected_duration_us,
                             (long long)actual_duration);
            }
        }
    }

    /* Memory armed (detect via iambic state) */
    if (iambic->dit_memory && diag->prev_iambic_state != iambic->state) {
        RT_DIAG_DEBUG(&g_rt_log_stream, now_us, "MEM DIT");
    }
    if (iambic->dah_memory && diag->prev_iambic_state != iambic->state) {
        RT_DIAG_DEBUG(&g_rt_log_stream, now_us, "MEM DAH");
    }

    /* Sidetone fade state transitions */
    if (sidetone->fade_state != diag->prev_fade_state) {
        const char *state_str;
        switch (sidetone->fade_state) {
            case FADE_SILENT:  state_str = "SILENT"; break;
            case FADE_IN:      state_str = "FADE_IN"; break;
            case FADE_SUSTAIN: state_str = "SUSTAIN"; break;
            case FADE_OUT:     state_str = "FADE_OUT"; break;
            default:           state_str = "?"; break;
        }
        RT_DIAG_DEBUG(&g_rt_log_stream, now_us, "TONE %s", state_str);
    }

    /* Update previous state */
    diag->prev_key_down = key_down;
    diag->prev_fade_state = sidetone->fade_state;
    diag->prev_iambic_state = iambic->state;
}
```

**Step 2: Call rt_diag_log in the RT loop**

After line 119 (`ptt_tick(&ptt, (uint64_t)now_us);`), add:

```c
        /* 6. Diagnostic logging (zero overhead if disabled) */
        rt_diag_log(&s_diag, &iambic, &sidetone,
                    out.local_key != 0, now_us);
```

**Step 3: Build to verify**

Run: `cd /workspaces/RustRemoteCWKeyer && idf.py build 2>&1 | tail -20`
Expected: Build succeeds

**Step 4: Commit**

```bash
git add main/rt_task.c
git commit -m "feat(rt): add diagnostic state tracking and logging"
```

---

## Task 4: Add diag Console Command

**Files:**
- Modify: `components/keyer_console/src/commands.c`

**Step 1: Add rt_log.h include after line 24**

After `#include "usb_uf2.h"`, add:

```c
#include "rt_log.h"
```

**Step 2: Add cmd_diag function before line 440 (before command registry)**

Before `/* ============================================================================` (the command registry section), add:

```c
/**
 * @brief diag [on|off] - Toggle RT diagnostic logging
 */
static console_error_t cmd_diag(const console_parsed_cmd_t *cmd) {
    if (cmd->argc == 0) {
        /* Show current state */
        bool enabled = atomic_load_explicit(&g_rt_diag_enabled, memory_order_relaxed);
        printf("Diagnostic logging: %s\r\n", enabled ? "ON" : "OFF");
        return CONSOLE_OK;
    }

    const char *arg = cmd->args[0];
    if (strcmp(arg, "on") == 0) {
        atomic_store_explicit(&g_rt_diag_enabled, true, memory_order_relaxed);
        printf("Diagnostic logging: ON\r\n");
    } else if (strcmp(arg, "off") == 0) {
        atomic_store_explicit(&g_rt_diag_enabled, false, memory_order_relaxed);
        printf("Diagnostic logging: OFF\r\n");
    } else {
        return CONSOLE_ERR_INVALID_VALUE;
    }
    return CONSOLE_OK;
}
```

**Step 3: Add diag to command registry**

In `s_commands[]` array (around line 458), add after the `factory-reset` entry:

```c
    { "diag",          "RT diagnostic logging",        cmd_diag },
```

**Step 4: Build to verify**

Run: `cd /workspaces/RustRemoteCWKeyer && idf.py build 2>&1 | tail -20`
Expected: Build succeeds

**Step 5: Commit**

```bash
git add components/keyer_console/src/commands.c
git commit -m "feat(console): add diag command for RT diagnostic toggle"
```

---

## Task 5: Add Tab Completion for diag Command

**Files:**
- Modify: `components/keyer_console/src/completion.c`

**Step 1: Add diag subcommand completion**

The current completion only handles commands and parameters. We need to add special handling for `diag on|off`.

After line 51 (after `find_matching_param` function), add:

```c
/**
 * @brief Find matching diag subcommand starting from index
 */
static const char *find_matching_diag_arg(const char *prefix, size_t len, size_t *start_idx) {
    static const char *diag_args[] = { "on", "off" };
    static const size_t diag_args_count = 2;

    for (size_t i = *start_idx; i < diag_args_count; i++) {
        if (strncmp(diag_args[i], prefix, len) == 0) {
            *start_idx = i + 1;
            return diag_args[i];
        }
    }
    return NULL;
}
```

**Step 2: Update console_complete to handle diag**

Replace the `completing_param` logic (lines 68-69) with:

```c
    /* Determine what we're completing */
    bool completing_param = false;
    bool completing_diag = false;

    if (token_start > 0) {
        if (strncmp(line, "set ", 4) == 0 || strncmp(line, "show ", 5) == 0) {
            completing_param = true;
        } else if (strncmp(line, "diag ", 5) == 0) {
            completing_diag = true;
        }
    }
```

**Step 3: Update match logic**

Replace the match finding code (lines 80-97) with:

```c
    const char *match = NULL;
    size_t idx = s_last_match_idx;

    if (completing_diag) {
        match = find_matching_diag_arg(prefix, prefix_len, &idx);
    } else if (completing_param) {
        match = find_matching_param(prefix, prefix_len, &idx);
    } else {
        match = find_matching_command(prefix, prefix_len, &idx);
    }

    if (match == NULL) {
        /* Wrap around */
        idx = 0;
        if (completing_diag) {
            match = find_matching_diag_arg(prefix, prefix_len, &idx);
        } else if (completing_param) {
            match = find_matching_param(prefix, prefix_len, &idx);
        } else {
            match = find_matching_command(prefix, prefix_len, &idx);
        }
    }
```

**Step 4: Build to verify**

Run: `cd /workspaces/RustRemoteCWKeyer && idf.py build 2>&1 | tail -20`
Expected: Build succeeds

**Step 5: Commit**

```bash
git add components/keyer_console/src/completion.c
git commit -m "feat(console): add tab completion for diag on|off"
```

---

## Task 6: Add Host Test for Diagnostic Macros

**Files:**
- Create: `test_host/test_rt_diag.c`
- Modify: `test_host/CMakeLists.txt`
- Modify: `test_host/test_main.c`

**Step 1: Create test file**

Create `test_host/test_rt_diag.c`:

```c
/**
 * @file test_rt_diag.c
 * @brief Tests for RT diagnostic logging macros
 */

#include "unity.h"
#include "rt_log.h"

void test_diag_disabled_by_default(void) {
    TEST_ASSERT_FALSE(atomic_load(&g_rt_diag_enabled));
}

void test_diag_enable_disable(void) {
    /* Start disabled */
    atomic_store(&g_rt_diag_enabled, false);
    TEST_ASSERT_FALSE(atomic_load(&g_rt_diag_enabled));

    /* Enable */
    atomic_store(&g_rt_diag_enabled, true);
    TEST_ASSERT_TRUE(atomic_load(&g_rt_diag_enabled));

    /* Disable */
    atomic_store(&g_rt_diag_enabled, false);
    TEST_ASSERT_FALSE(atomic_load(&g_rt_diag_enabled));
}

void test_diag_macro_does_not_crash_when_disabled(void) {
    atomic_store(&g_rt_diag_enabled, false);

    /* These should not crash even with null stream when disabled */
    int64_t ts = 12345;
    RT_DIAG_INFO(&g_rt_log_stream, ts, "test %d", 42);
    RT_DIAG_DEBUG(&g_rt_log_stream, ts, "debug");
    RT_DIAG_WARN(&g_rt_log_stream, ts, "warn");

    /* If we got here, test passes */
    TEST_PASS();
}

void test_diag_macro_logs_when_enabled(void) {
    log_stream_init(&g_rt_log_stream);
    atomic_store(&g_rt_diag_enabled, true);

    int64_t ts = 99999;
    RT_DIAG_INFO(&g_rt_log_stream, ts, "hello %s", "world");

    /* Verify entry was logged */
    log_entry_t entry;
    TEST_ASSERT_TRUE(log_stream_drain(&g_rt_log_stream, &entry));
    TEST_ASSERT_EQUAL(LOG_LEVEL_INFO, entry.level);
    TEST_ASSERT_EQUAL(ts, entry.timestamp_us);
    TEST_ASSERT_EQUAL_STRING("hello world", entry.msg);

    /* Clean up */
    atomic_store(&g_rt_diag_enabled, false);
}

void run_rt_diag_tests(void) {
    RUN_TEST(test_diag_disabled_by_default);
    RUN_TEST(test_diag_enable_disable);
    RUN_TEST(test_diag_macro_does_not_crash_when_disabled);
    RUN_TEST(test_diag_macro_logs_when_enabled);
}
```

**Step 2: Add log_stream.c to test build**

In `test_host/CMakeLists.txt`, add after line 66 (after AUDIO_SOURCES):

```cmake
set(LOGGING_SOURCES
    ${COMPONENT_DIR}/keyer_logging/src/log_stream.c
)
```

And update `add_executable` (around line 97) to include:

```cmake
    ${LOGGING_SOURCES}
```

Also add to TEST_SOURCES:

```cmake
    test_rt_diag.c
```

**Step 3: Add test runner call to test_main.c**

Add extern declaration and call:

```c
extern void run_rt_diag_tests(void);

// In main():
    run_rt_diag_tests();
```

**Step 4: Build and run tests**

Run: `cd /workspaces/RustRemoteCWKeyer/test_host && cmake -B build && cmake --build build && ./build/test_runner`
Expected: All tests pass

**Step 5: Commit**

```bash
git add test_host/test_rt_diag.c test_host/CMakeLists.txt test_host/test_main.c
git commit -m "test: add RT diagnostic logging tests"
```

---

## Task 7: Final Integration Test

**Files:** None (verification only)

**Step 1: Full build**

Run: `cd /workspaces/RustRemoteCWKeyer && idf.py fullclean && idf.py build`
Expected: Build succeeds with no warnings

**Step 2: Run all host tests**

Run: `cd /workspaces/RustRemoteCWKeyer/test_host && cmake -B build && cmake --build build && ./build/test_runner`
Expected: All tests pass

**Step 3: Commit final state**

If any fixes were needed, commit them:

```bash
git add -A
git commit -m "fix: address integration issues in RT diagnostic logging"
```

---

## Summary

| Task | Description | Files |
|------|-------------|-------|
| 1 | Add g_rt_diag_enabled flag | rt_log.h, log_stream.c |
| 2 | Add RT_DIAG_* macros | rt_log.h |
| 3 | Add diagnostic tracking to rt_task | rt_task.c |
| 4 | Add diag console command | commands.c |
| 5 | Add tab completion for diag | completion.c |
| 6 | Add host tests | test_rt_diag.c, CMakeLists.txt, test_main.c |
| 7 | Final integration test | (verification) |

**Total commits:** 6-7 atomic commits
