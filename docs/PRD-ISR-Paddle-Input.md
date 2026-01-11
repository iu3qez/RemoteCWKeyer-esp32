# PRD: ISR-Based Paddle Input with Blanking

## Overview

Replace polling-based paddle input detection with interrupt-driven approach to reduce key-press latency from ~1-4ms to ~5-10µs.

## Problem Statement

Current implementation polls GPIO paddles every 1ms in the RT task loop. This introduces:
- **Worst-case latency**: 1ms (polling interval) + processing time
- **Measured latency**: ~4ms (unexplained, possibly bounce-related)
- **Jitter**: 0-1ms depending on when keypress occurs relative to tick

For high-speed CW operators (40+ WPM), even 1ms latency is perceptible and affects timing feel.

## Goals

| Goal | Target | Current |
|------|--------|---------|
| Press detection latency | <10µs | ~1-4ms |
| Determinism | Constant latency | Variable 0-1ms jitter |
| Bounce immunity | Hardware-grade | Software debounce 5ms |
| RT-safety | No blocking, no heap | Already compliant |
| Backward compatibility | Polling as fallback | N/A |

## Non-Goals

- Changing the iambic FSM logic
- Modifying audio/sidetone path
- Remote keying latency improvements
- Release detection optimization (less critical)

## Technical Approach

### Architecture: ISR + Blanking Timer

```
GPIO Edge (falling) ──► ISR Handler ──► Set atomic flag
                              │
                              ├──► Disable interrupt
                              └──► Start blanking timer (3ms)

Blanking Timer expires ──► Re-enable interrupt

RT Task tick ──► Check atomic flag ──► If set: force paddle=pressed
                                  └──► Clear flag
```

### Key Design Decisions

1. **Edge-triggered interrupts**: React to falling edge (active-low paddles)
2. **Immediate action**: ISR sets atomic flag instantly (~5µs)
3. **Blanking period**: Disable interrupt for 3ms to ignore bounce
4. **Timer re-enable**: esp_timer one-shot re-enables interrupt after blanking
5. **RT task integration**: Atomic flag overrides polling result
6. **Fallback**: Polling continues as secondary detection

### Memory Model

```c
// ISR → RT Task communication (lock-free)
static volatile atomic_bool s_dit_pending;
static volatile atomic_bool s_dah_pending;

// ISR writes with memory_order_release
// RT task reads with memory_order_acquire
```

### Blanking Duration

| Duration | Pros | Cons |
|----------|------|------|
| 1ms | Fast repeat detection | May not cover all bounce |
| 3ms | Covers typical bounce | Slightly slower repeat |
| 5ms | Very safe | Limits max speed (~200 presses/sec) |

**Recommendation**: 3ms (configurable via parameter)

## Implementation Plan

### Phase 1: HAL Layer (hal_gpio)

#### 1.1 Add ISR infrastructure to hal_gpio.c

```c
// New static state
static volatile atomic_bool s_dit_pending = ATOMIC_VAR_INIT(false);
static volatile atomic_bool s_dah_pending = ATOMIC_VAR_INIT(false);
static esp_timer_handle_t s_dit_blanking_timer;
static esp_timer_handle_t s_dah_blanking_timer;
static int64_t s_dit_last_edge_us;
static int64_t s_dah_last_edge_us;

// ISR handlers (IRAM_ATTR)
static void IRAM_ATTR dit_isr_handler(void *arg);
static void IRAM_ATTR dah_isr_handler(void *arg);

// Blanking timer callbacks
static void dit_blanking_expired(void *arg);
static void dah_blanking_expired(void *arg);
```

#### 1.2 Extend hal_gpio.h API

```c
/**
 * @brief Initialize GPIO with ISR support
 * @param config GPIO configuration including blanking_us
 * @return ESP_OK on success
 */
esp_err_t hal_gpio_init_isr(const hal_gpio_config_t *config);

/**
 * @brief Check and consume pending DIT press from ISR
 * @return true if DIT was pressed since last call
 * @note RT-safe, lock-free
 */
bool hal_gpio_consume_dit_press(void);

/**
 * @brief Check and consume pending DAH press from ISR
 * @return true if DAH was pressed since last call
 * @note RT-safe, lock-free
 */
bool hal_gpio_consume_dah_press(void);
```

#### 1.3 Add blanking parameter to config

In `parameters.yaml` under `hardware` family:

```yaml
isr_blanking_us:
  type: u32
  default: 3000
  range: [1000, 10000]
  nvs_key: "isr_blank"
  runtime_change: reboot
  priority: 23
```

### Phase 2: RT Task Integration

#### 2.1 Modify rt_task.c paddle reading

```c
// Current:
gpio_state_t gpio = hal_gpio_read_paddles();

// New:
gpio_state_t gpio = hal_gpio_read_paddles();

// Override with ISR-detected presses
if (hal_gpio_consume_dit_press()) {
    gpio = gpio_with_dit(gpio, true);
}
if (hal_gpio_consume_dah_press()) {
    gpio = gpio_with_dah(gpio, true);
}
```

#### 2.2 Add gpio helper function

In `keyer_core/include/sample.h`:

```c
static inline gpio_state_t gpio_with_dit(gpio_state_t gpio, bool pressed) {
    return pressed ? (gpio | 0x01) : (gpio & ~0x01);
}

static inline gpio_state_t gpio_with_dah(gpio_state_t gpio, bool pressed) {
    return pressed ? (gpio | 0x02) : (gpio & ~0x02);
}
```

### Phase 3: Iambic FSM Adjustment

#### 3.1 Remove/reduce debounce in iambic.c

Current `IAMBIC_DEBOUNCE_RELEASE_US = 5000` can be:
- **Kept for release** (release bounce less critical)
- **Removed for press** (ISR blanking handles it)

```c
// Modify update_gpio() to skip press debounce when ISR is active
// Release debounce remains for clean key-up detection
```

### Phase 4: Host Testing Support

#### 4.1 Add test injection API

```c
#ifndef ESP_PLATFORM
/**
 * @brief Inject ISR press event for testing
 * @param dit Simulate DIT press
 * @param dah Simulate DAH press
 */
void hal_gpio_test_inject_isr_press(bool dit, bool dah);
#endif
```

### Phase 5: Diagnostics & Validation

#### 5.1 Add latency measurement

```c
// In ISR: record timestamp
s_dit_isr_timestamp_us = esp_timer_get_time();

// In RT task: calculate and log latency
int64_t latency = now_us - s_dit_isr_timestamp_us;
RT_DIAG_DEBUG(&g_rt_log_stream, now_us, "ISR→RT latency: %lldus", latency);
```

#### 5.2 Add console command

```
> diag isr
DIT ISR triggers: 1234
DAH ISR triggers: 567
Avg latency: 8µs
Max latency: 45µs
Blanking rejects: 23
```

## File Changes Summary

| File | Changes |
|------|---------|
| `components/keyer_hal/include/hal_gpio.h` | Add ISR API functions |
| `components/keyer_hal/src/hal_gpio.c` | Implement ISR + blanking |
| `components/keyer_core/include/sample.h` | Add gpio_with_dit/dah helpers |
| `components/keyer_iambic/include/iambic.h` | Reduce DEBOUNCE_RELEASE_US |
| `components/keyer_iambic/src/iambic.c` | Conditional debounce logic |
| `main/rt_task.c` | Integrate ISR consume calls |
| `main/main.c` | Call hal_gpio_init_isr() |
| `parameters.yaml` | Add isr_blanking_us parameter |
| `test_host/test_hal_gpio.c` | Add ISR injection tests |

## Risks & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| ISR causes crash | Low | High | IRAM_ATTR, minimal ISR code |
| Race condition | Medium | Medium | Atomic operations, memory barriers |
| Timer starvation | Low | Medium | Use high-priority esp_timer |
| Bounce not fully covered | Medium | Low | Configurable blanking, fallback polling |

## Success Criteria

1. **Latency**: Measured press→TX latency < 50µs (with logic analyzer)
2. **Stability**: 24h soak test with no crashes or missed presses
3. **Bounce immunity**: No double-triggers with mechanical paddles
4. **Tests pass**: All existing + new unit tests green
5. **No regressions**: Polling fallback works if ISR disabled

## Timeline Estimate

| Phase | Effort |
|-------|--------|
| Phase 1: HAL Layer | 2-3 hours |
| Phase 2: RT Integration | 1 hour |
| Phase 3: Iambic Adjust | 1 hour |
| Phase 4: Host Testing | 1-2 hours |
| Phase 5: Diagnostics | 1-2 hours |
| **Total** | **6-10 hours** |

## References

- [ESP-IDF GPIO ISR Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/gpio.html#gpio-interrupt)
- [ESP Timer API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/esp_timer.html)
- [ARCHITECTURE.md](../ARCHITECTURE.md) - RT constraints
