# Code Style & Conventions

## Language
- Pure C (C11 standard), no C++
- ESP-IDF v5.x APIs

## Compiler Flags (mandatory)
`-Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wdouble-promotion -Wformat=2 -Wformat-security -Wnull-dereference -fstack-protector-strong -Wstrict-overflow=2 -Wno-unused-parameter`

## Naming
- snake_case for functions and variables
- UPPER_CASE for macros and constants
- `_t` suffix for typedefs
- `static` for file-local symbols
- Prefix components: `keyer_core_`, `keyer_iambic_`, etc.

## Types
- Exact-width: `uint8_t`, `uint16_t`, `uint32_t`, `int64_t`
- `size_t` for sizes/indices
- `bool` from `<stdbool.h>`
- `esp_err_t` for fallible functions

## Error Handling
- Return `esp_err_t` from functions that can fail
- `ESP_ERROR_CHECK()` during init only
- Graceful handling in runtime paths
- FAULT and stop on RT path errors

## Memory
- Static allocation in RT path (no heap)
- Heap allowed during init and in Core 1 tasks
- `IRAM_ATTR` for ISR and RT-critical functions

## Atomics
- `#include <stdatomic.h>`
- `memory_order_acquire` for consumers
- `memory_order_release` / `memory_order_acq_rel` for producers
- `memory_order_relaxed` only for stats counters

## Logging
- RT path: `RT_INFO()`, `RT_WARN()`, `RT_ERROR()`, `RT_DEBUG()` from `rt_log.h`
- Non-RT: standard `ESP_LOGx()` macros

## Const Correctness
- Use `const` on pointer params when not modifying
- Prefer `const` wherever possible
