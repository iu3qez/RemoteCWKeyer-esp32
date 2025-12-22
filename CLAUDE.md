# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This repository contains two implementations of the CW (Morse code) keyer:

1. **Rust implementation** (root directory) - Original implementation using esp-rs
2. **C implementation** 
 - Pure C implementation using ESP-IDF

Both implementations follow the same architectural principles from [ARCHITECTURE.md](ARCHITECTURE.md).

**CRITICAL**: Before making any code changes, read [ARCHITECTURE.md](ARCHITECTURE.md). It defines immutable design principles that MUST be followed. Non-compliant code will not be merged.

---

## C Implementation

The pure C implementation uses ESP-IDF v5.x with strict coding standards for embedded real-time systems.

### Directory Structure

```
 /
├── CMakeLists.txt          # Top-level ESP-IDF project
├── sdkconfig.defaults      # ESP-IDF configuration
├── partitions.csv          # Flash partition table
├── PVS-Studio.cfg          # Static analyzer config
├── components/
│   ├── keyer_core/         # Stream, sample, consumer, fault
│   ├── keyer_iambic/       # Iambic FSM
│   ├── keyer_audio/        # Sidetone, buffer, PTT
│   ├── keyer_logging/      # RT-safe logging
│   ├── keyer_console/      # Serial console
│   ├── keyer_config/       # Generated config
│   └── keyer_hal/          # GPIO, I2S, ES8311
├── main/
│   ├── main.c              # Entry point
│   ├── rt_task.c           # Core 0 RT task
│   └── bg_task.c           # Core 1 background task
├── scripts/
│   └── gen_config_c.py     # Config code generator
└── test_host/              # Unity-based host tests
```

### Building

```bash
cd keyer_c

# Configure (first time only)
idf.py set-target esp32s3

# Build
idf.py build

# Flash and monitor
idf.py flash monitor

# Clean build
idf.py fullclean
idf.py build
```

### Code Generation

Configuration is generated from [parameters.yaml](parameters.yaml):

```bash
# Regenerate config (done automatically during build)
python scripts/gen_config_c.py

# Generated files in components/keyer_config/
# - config.h      - Atomic config struct
# - config.c      - Default values
# - config_nvs.h  - NVS persistence
```

---

## ESP32 C Best Practices

### Compiler Flags (MANDATORY)

All code MUST compile cleanly with these flags:

```cmake
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wno-unused-parameter
    -Wconversion
    -Wsign-conversion
    -Wdouble-promotion
    -Wformat=2
    -Wformat-security
    -Wnull-dereference
    -Wstack-protector
    -fstack-protector-strong
    -Wstrict-overflow=2
)
```

### Static Analysis (PVS-Studio)

Run PVS-Studio before every commit:

```bash
# Generate compile_commands.json
idf.py build

# Run PVS-Studio
pvs-studio-analyzer analyze -f  /build/compile_commands.json \
    -o  /build/pvs-report.log \
    -j4

# Convert to readable format
plog-converter -t fullhtml -o  /build/pvs-report  /build/pvs-report.log
```

**Zero warnings policy**: All PVS-Studio warnings must be fixed or explicitly suppressed with justification.

### Memory Management

**NO DYNAMIC ALLOCATION IN RT PATH**

```c
// FORBIDDEN in RT path:
malloc(), calloc(), realloc(), free()
strdup(), asprintf()
new, delete (C++)

// REQUIRED: Static allocation only
static uint8_t buffer[BUFFER_SIZE];
static stream_sample_t samples[STREAM_CAPACITY];
```

**Heap usage allowed only**:
- During initialization (before RT task starts)
- In best-effort tasks on Core 1
- Never in Core 0 RT task

### Atomic Operations (C11 stdatomic.h)

```c
#include <stdatomic.h>

// Correct usage
atomic_store_explicit(&var, value, memory_order_release);
value = atomic_load_explicit(&var, memory_order_acquire);

// For counters/indices in single-producer scenarios
atomic_store_explicit(&idx, new_idx, memory_order_relaxed);
```

**Memory ordering guide**:
- `memory_order_relaxed` - Single producer/consumer, no synchronization needed
- `memory_order_acquire` - Consumer reading shared data
- `memory_order_release` - Producer writing shared data
- `memory_order_seq_cst` - Avoid unless absolutely necessary (performance cost)

### Defensive Coding Patterns

```c
// 1. Always validate pointers
void func(const data_t *data) {
    if (data == NULL) {
        return;  // or set FAULT
    }
    // ...
}

// 2. Bounds checking
if (index >= ARRAY_SIZE) {
    fault_set(&g_fault_state, FAULT_INDEX_OUT_OF_BOUNDS, index);
    return;
}

// 3. Use const correctness
void process(const stream_sample_t *sample);  // Won't modify sample

// 4. Initialize all variables
uint32_t count = 0;  // Never leave uninitialized

// 5. Use static for file-local symbols
static void internal_helper(void);  // Not visible outside file

// 6. Use IRAM_ATTR for ISR and RT-critical functions
static void IRAM_ATTR gpio_isr_handler(void *arg);
```

### FreeRTOS Best Practices

```c
// 1. Pin RT task to Core 0
xTaskCreatePinnedToCore(
    rt_task,
    "rt_task",
    4096,
    NULL,
    configMAX_PRIORITIES - 1,  // Highest priority
    NULL,
    0  // Core 0
);

// 2. Use vTaskDelayUntil for periodic tasks (not vTaskDelay)
TickType_t last_wake = xTaskGetTickCount();
for (;;) {
    // Do work...
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1));
}

// 3. Never use mutexes in RT path
// Use lock-free atomics instead

// 4. Stack size: measure with uxTaskGetStackHighWaterMark()
UBaseType_t stack_remaining = uxTaskGetStackHighWaterMark(NULL);
```

### RT-Safe Logging

**NEVER use in RT path**:
- `ESP_LOGx()` macros
- `printf()`, `puts()`, `fprintf()`
- Any blocking I/O

**ALWAYS use RT-safe macros**:

```c
#include "rt_log.h"

int64_t now_us = esp_timer_get_time();

RT_ERROR(&g_rt_log_stream, now_us, "FAULT: %s", fault_code_str(code));
RT_WARN(&g_rt_log_stream, now_us, "Lag: %u samples", lag);
RT_INFO(&g_rt_log_stream, now_us, "Key down");
RT_DEBUG(&g_rt_log_stream, now_us, "State: %d", state);
```

### Integer Types

```c
// Use exact-width types
#include <stdint.h>
uint8_t  byte;
uint16_t half_word;
uint32_t word;
int64_t  timestamp_us;

// Use size_t for sizes and indices
size_t length = strlen(str);
for (size_t i = 0; i < count; i++) { ... }

// Use bool for flags
#include <stdbool.h>
bool is_active = false;
```

### Error Handling Pattern

```c
// Return esp_err_t for functions that can fail
esp_err_t hal_gpio_init(const hal_gpio_config_t *config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

// Check return values
ESP_ERROR_CHECK(hal_gpio_init(&cfg));  // Abort on error (init only)

// Or handle gracefully
esp_err_t ret = some_function();
if (ret != ESP_OK) {
    RT_ERROR(&stream, now, "Failed: %s", esp_err_to_name(ret));
    return ret;
}
```

---

## Architecture Principles (Immutable)

From [ARCHITECTURE.md](ARCHITECTURE.md):

### The Stream is Truth

```
Producer → KeyingStream → Consumers
           (lock-free)
```

All keying events flow through `keying_stream_t`. No other shared state.

### Hard Real-Time Path

```
GPIO Poll → Iambic FSM → Stream Push → Audio/TX Consumer
           (Core 0, highest priority, ZERO context switches)
```

**Latency ceiling: 100 microseconds**

**Forbidden in RT path**:
- Heap allocation (`malloc`, `free`)
- Blocking I/O
- Mutexes/locks
- `ESP_LOGx`, `printf`
- Context switches

### FAULT Philosophy

> Corrupted CW timing is worse than silence. If in doubt, FAULT and stop.

```c
if (lag > MAX_LAG_SAMPLES) {
    fault_set(&g_fault_state, FAULT_CONSUMER_LAG, lag);
    hal_gpio_set_tx(false);  // TX off immediately
    return HARD_RT_FAULT;
}
```

### Threading Model

| Core | Task | Priority | Purpose |
|------|------|----------|---------|
| 0 | rt_task | MAX-1 | GPIO, Iambic, Stream, Audio/TX |
| 1 | bg_task | IDLE+2 | Remote, decoder, diagnostics |
| 1 | uart_log | IDLE+1 | Log drain to UART |
| 1 | console | IDLE+1 | Serial console |

---

## Key Components (C Implementation)

### keyer_core

- **[stream.h]( /components/keyer_core/include/stream.h)** - Lock-free SPMC ring buffer
- **[sample.h]( /components/keyer_core/include/sample.h)** - 6-byte packed sample struct
- **[consumer.h]( /components/keyer_core/include/consumer.h)** - Hard RT and best-effort consumers
- **[fault.h]( /components/keyer_core/include/fault.h)** - Atomic fault state

### keyer_iambic

- **[iambic.h]( /components/keyer_iambic/include/iambic.h)** - Iambic FSM (Mode A/B)

### keyer_audio

- **[sidetone.h]( /components/keyer_audio/include/sidetone.h)** - Phase accumulator sidetone
- **[audio_buffer.h]( /components/keyer_audio/include/audio_buffer.h)** - Lock-free audio ring buffer
- **[ptt.h]( /components/keyer_audio/include/ptt.h)** - PTT controller with tail timeout

### keyer_logging

- **[rt_log.h]( /components/keyer_logging/include/rt_log.h)** - RT-safe logging macros

### keyer_hal

- **[hal_gpio.h]( /components/keyer_hal/include/hal_gpio.h)** - Paddle input, TX output
- **[hal_audio.h]( /components/keyer_hal/include/hal_audio.h)** - I2S audio output
- **[hal_es8311.h]( /components/keyer_hal/include/hal_es8311.h)** - ES8311 codec driver

---

## Code Review Checklist

Every change must verify:

1. **Stream-only communication** - No shared state except `keying_stream_t`
2. **No forbidden patterns** - No malloc, mutex, blocking I/O in RT path
3. **RT latency budget** - Operations complete within 100µs
4. **Static allocation** - All buffers statically allocated
5. **FAULT semantics** - Timing violations trigger FAULT
6. **RT-safe logging** - Only `RT_*()` macros in RT path
7. **Compiler warnings** - Zero warnings with strict flags
8. **PVS-Studio clean** - No static analysis warnings

Non-compliant code shall not be merged.

---

## Testing

### Host Tests (Unity)

```bash
cd  /test_host

# Build and run all tests
cmake -B build
cmake --build build
./build/test_runner

# Run with sanitizers
cmake -B build -DCMAKE_C_FLAGS="-fsanitize=address,undefined"
cmake --build build
./build/test_runner
```

### Testing Principles

- Components tested by providing streams (fake, recorded, synthesized)
- No mocking needed (stream is the only interface)
- Tests run on host without hardware
- **If you need a mock, the design is wrong**

---

## Common Pitfalls

1. **Don't bypass the stream** - All communication through `keying_stream_t`
2. **Don't add mutexes** - Use `stdatomic.h` only
3. **Don't use ESP_LOGx in RT path** - Use `RT_*()` macros
4. **Don't allocate in RT path** - Static allocation only
5. **Don't edit generated files** - Edit `parameters.yaml` instead
6. **Don't add callbacks** - Producers and consumers are isolated
7. **Don't share state** - Only the stream is shared
8. **Don't ignore compiler warnings** - Fix all warnings

---

## External Documentation

- **ESP-IDF**: https://docs.espressif.com/projects/esp-idf/
- **ESP-IDF API Reference**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/
- **FreeRTOS**: https://www.freertos.org/Documentation/RTOS_book.html
- **PVS-Studio**: https://pvs-studio.com/en/docs/
