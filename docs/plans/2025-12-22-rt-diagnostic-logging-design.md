# RT Diagnostic Logging Design

## Overview

Add runtime-togglable diagnostic logging to the keying and audio generation systems to monitor operation without modifying the pure-logic components.

## Requirements

1. **Production diagnostics** - Minimal logging for significant events
2. **Log keying + audio + timing** - Full visibility into system operation
3. **Zero overhead when disabled** - Runtime check with atomic bool (~1 cycle)
4. **Pure components** - No changes to `iambic.c` or `sidetone.c`

## Architecture

```
┌─────────────┐     ┌──────────────┐     ┌─────────────┐
│  rt_task.c  │────▶│  iambic.c    │     │ sidetone.c  │
│  (logging)  │     │  (pure logic)│     │ (pure logic)│
└──────┬──────┘     └──────────────┘     └─────────────┘
       │
       ▼
┌──────────────────┐
│ g_rt_log_stream  │  (lock-free ring buffer)
└────────┬─────────┘
         │ drain (Core 1)
         ▼
┌──────────────────┐
│  UART / Console  │
└──────────────────┘
```

**Principle**: Components `iambic.c` and `sidetone.c` remain pure. Logging happens in `rt_task.c` by comparing state before/after each tick.

## Events to Log

### Iambic FSM

| Event | Level | Format |
|-------|-------|--------|
| Key down (DIT start) | INFO | `KEY DIT↓` |
| Key down (DAH start) | INFO | `KEY DAH↓` |
| Key up | INFO | `KEY ↑ %dus` (actual duration) |
| Timing drift >5% | WARN | `DRIFT %d%% (exp=%d act=%d)` |
| Memory armed | DEBUG | `MEM DIT` or `MEM DAH` |

### Sidetone

| Event | Level | Format |
|-------|-------|--------|
| Fade state change | DEBUG | `TONE FADE_IN` / `SUSTAIN` / `FADE_OUT` / `SILENT` |

## Implementation

### Runtime Check Mechanism

**Global variable** (in `rt_log.h`):

```c
/** Diagnostic logging enable (atomic for RT-safe read) */
extern atomic_bool g_rt_diag_enabled;
```

**Wrapper macros** (in `rt_log.h`):

```c
#define RT_DIAG_INFO(stream, ts, fmt, ...) do { \
    if (atomic_load_explicit(&g_rt_diag_enabled, memory_order_relaxed)) { \
        RT_INFO(stream, ts, fmt, ##__VA_ARGS__); \
    } \
} while(0)

#define RT_DIAG_DEBUG(stream, ts, fmt, ...) do { \
    if (atomic_load_explicit(&g_rt_diag_enabled, memory_order_relaxed)) { \
        RT_DEBUG(stream, ts, fmt, ##__VA_ARGS__); \
    } \
} while(0)

#define RT_DIAG_WARN(stream, ts, fmt, ...) do { \
    if (atomic_load_explicit(&g_rt_diag_enabled, memory_order_relaxed)) { \
        RT_WARN(stream, ts, fmt, ##__VA_ARGS__); \
    } \
} while(0)
```

**Overhead when disabled**: One `atomic_load` relaxed (~1 CPU cycle) + branch prediction (usually correct).

### State Tracking in rt_task.c

```c
/* Diagnostic state tracking */
typedef struct {
    bool prev_key_down;              /* Previous key state */
    fade_state_t prev_fade_state;    /* Previous sidetone fade state */
    int64_t element_start_us;        /* When current element started */
    int64_t expected_duration_us;    /* Expected element duration */
    iambic_state_t prev_iambic_state; /* To detect DIT vs DAH */
} rt_diag_state_t;

static rt_diag_state_t s_diag = {0};
```

**RT loop logic** (pseudocode):

```c
void rt_loop_iteration(int64_t now_us) {
    // 1. Read GPIO, run iambic tick
    stream_sample_t sample = iambic_tick(&proc, now_us, gpio);

    // 2. Generate sidetone
    int16_t audio = sidetone_next_sample(&gen, sample.local_key);

    // 3. Diagnostic logging (only if enabled)
    if (atomic_load_explicit(&g_rt_diag_enabled, memory_order_relaxed)) {
        rt_diag_log(&s_diag, &proc, &gen, sample.local_key, now_us);
    }
}
```

**Function `rt_diag_log`**: Compares states, emits logs on transitions, calculates drift.

### Console Command

```
> diag
Diagnostic logging: OFF

> diag on
Diagnostic logging: ON

> diag off
Diagnostic logging: OFF
```

**Implementation** (in `commands.c`):

```c
static int cmd_diag(int argc, char **argv) {
    if (argc < 2) {
        bool enabled = atomic_load_explicit(&g_rt_diag_enabled,
                                            memory_order_relaxed);
        printf("Diagnostic logging: %s\n", enabled ? "ON" : "OFF");
        return 0;
    }

    if (strcmp(argv[1], "on") == 0) {
        atomic_store_explicit(&g_rt_diag_enabled, true,
                              memory_order_relaxed);
        printf("Diagnostic logging: ON\n");
    } else if (strcmp(argv[1], "off") == 0) {
        atomic_store_explicit(&g_rt_diag_enabled, false,
                              memory_order_relaxed);
        printf("Diagnostic logging: OFF\n");
    }
    return 0;
}
```

## Files to Modify

| File | Changes |
|------|---------|
| `components/keyer_logging/include/rt_log.h` | Add `g_rt_diag_enabled`, `RT_DIAG_*` macros |
| `components/keyer_logging/src/rt_log.c` | Define `atomic_bool g_rt_diag_enabled = false` |
| `main/rt_task.c` | Add `rt_diag_state_t`, call `rt_diag_log()` |
| `components/keyer_console/src/commands.c` | Add `cmd_diag` |
| `components/keyer_console/src/completion.c` | Add completion for `diag on|off` |

## Implementation Order

1. `rt_log.h/c` - variable and macros
2. `rt_task.c` - tracking and logging
3. `commands.c` - console command
4. `completion.c` - tab completion

## Non-Goals

- No changes to `iambic.c` or `sidetone.c` (remain pure logic)
- No compile-time conditionals (runtime check only)
- No persistent config for diag state (starts OFF on boot)
