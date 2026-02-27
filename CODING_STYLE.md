# Coding Style & Patterns

Detailed C coding standards for the RemoteCWKeyer-esp32 project. Read this when writing or reviewing C code.

For architectural constraints and RT path rules, see [ARCHITECTURE.md](ARCHITECTURE.md).

---

## Compiler Flags (MANDATORY)

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

---

## Static Analysis (PVS-Studio)

Run PVS-Studio before every commit:

```bash
# Generate compile_commands.json
idf.py build

# Run PVS-Studio
pvs-studio-analyzer analyze -f build/compile_commands.json \
    -o build/pvs-report.log \
    -j4

# Convert to readable format
plog-converter -t fullhtml -o build/pvs-report build/pvs-report.log
```

**Zero warnings policy**: All PVS-Studio warnings must be fixed or explicitly suppressed with justification.

---

## Memory Management

**NO DYNAMIC ALLOCATION IN RT PATH**

```c
// FORBIDDEN in RT path:
malloc(), calloc(), realloc(), free()
strdup(), asprintf()

// REQUIRED: Static allocation only
static uint8_t buffer[BUFFER_SIZE];
static stream_sample_t samples[STREAM_CAPACITY];
```

**Heap usage allowed only**:
- During initialization (before RT task starts)
- In best-effort tasks on Core 1
- Never in Core 0 RT task

---

## Atomic Operations (C11 stdatomic.h)

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

---

## Defensive Coding Patterns

```c
// 1. Always validate pointers
void func(const data_t *data) {
    if (data == NULL) {
        return;  // or set FAULT
    }
}

// 2. Bounds checking
if (index >= ARRAY_SIZE) {
    fault_set(&g_fault_state, FAULT_INDEX_OUT_OF_BOUNDS, index);
    return;
}

// 3. Use const correctness
void process(const stream_sample_t *sample);

// 4. Initialize all variables
uint32_t count = 0;

// 5. Use static for file-local symbols
static void internal_helper(void);

// 6. Use IRAM_ATTR for ISR and RT-critical functions
static void IRAM_ATTR gpio_isr_handler(void *arg);
```

---

## FreeRTOS Best Practices

```c
// 1. Pin RT task to Core 0
xTaskCreatePinnedToCore(
    rt_task, "rt_task", 4096, NULL,
    configMAX_PRIORITIES - 1,  // Highest priority
    NULL, 0  // Core 0
);

// 2. Use vTaskDelayUntil for periodic tasks (not vTaskDelay)
TickType_t last_wake = xTaskGetTickCount();
for (;;) {
    // Do work...
    vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1));
}

// 3. Never use mutexes in RT path — use lock-free atomics instead

// 4. Stack size: measure with uxTaskGetStackHighWaterMark()
UBaseType_t stack_remaining = uxTaskGetStackHighWaterMark(NULL);
```

---

## RT-Safe Logging

**NEVER use in RT path**: `ESP_LOGx()`, `printf()`, `puts()`, any blocking I/O.

**ALWAYS use RT-safe macros**:

```c
#include "rt_log.h"

int64_t now_us = esp_timer_get_time();

RT_ERROR(&g_rt_log_stream, now_us, "FAULT: %s", fault_code_str(code));
RT_WARN(&g_rt_log_stream, now_us, "Lag: %u samples", lag);
RT_INFO(&g_rt_log_stream, now_us, "Key down");
RT_DEBUG(&g_rt_log_stream, now_us, "State: %d", state);
```

---

## Integer Types

- Use exact-width types from `<stdint.h>`: `uint8_t`, `uint16_t`, `uint32_t`, `int64_t`
- Use `size_t` for sizes and indices
- Use `bool` from `<stdbool.h>` for flags

---

## Error Handling Pattern

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
