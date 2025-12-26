# RemoteCWKeyerV3 — Architecture Principles

**Project**: RemoteCWKeyerV3  
**Version**: 1.0  
**Status**: IMMUTABLE  
**Date**: 2025-01-XX

---

## Preface

This document defines the fundamental architectural principles for RemoteCWKeyerV3.

RemoteCWKeyerV3 is a professional-grade CW (Morse code) keyer with remote operation capability, built in Rust for ESP32-S3/P4 platforms. It implements iambic keying, sidetone generation, and network-transparent remote CW operation.

These rules are **immutable**. Every implementation decision, every line of code, every review must conform to these principles.

Violations are not technical debt. They are defects.

---

## 1. The Stream Is The Truth

### 1.1 Single Source of Truth

```
┌─────────────────────────────────────────────────────────────┐
│                     KeyingStream                            │
│                                                             │
│   The heart of RemoteCWKeyerV3.                          │
│                                                             │
│   All events flow through the stream.                       │
│   The stream is the only shared state.                      │
│   The stream is the only synchronization mechanism.         │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**RULE 1.1.1**: All keying events (GPIO, iambic output, remote CW) MUST flow through the KeyingStream.

**RULE 1.1.2**: No component shall communicate with another component except through the stream.

**RULE 1.1.3**: The stream contains time-aligned samples of all channels. Silence is data.

### 1.2 Stream Structure

```c
typedef struct __attribute__((packed)) {
    gpio_state_t gpio;       // Physical paddle state
    uint8_t      local_key;  // Iambic/manual keyer output (1=key down)
    uint8_t      audio_level;// Audio output level
    uint8_t      flags;      // Edge flags, silence marker
    uint16_t     config_gen; // Config generation / silence ticks
} stream_sample_t;           // 6 bytes packed
```

**RULE 1.2.1**: All three channels (GPIO, local, remote) are captured in every sample.

**RULE 1.2.2**: Samples are time-aligned at the stream tick rate (10-50 kHz).

**RULE 1.2.3**: Silence is compressed using run-length encoding, but logically present.

---

## 2. Producers and Consumers Are Isolated

### 2.1 Producer Rules

```
Producer ──────▶ Stream
   │
   └── Knows nothing about consumers
```

**RULE 2.1.1**: A producer writes to the stream. It does not know who reads.

**RULE 2.1.2**: A producer does not wait for consumers.

**RULE 2.1.3**: A producer does not notify consumers.

**RULE 2.1.4**: Multiple producers coordinate only through `write_idx.fetch_add()`.

### 2.2 Consumer Rules

```
Stream ──────▶ Consumer
                  │
                  └── Knows nothing about producers or other consumers
```

**RULE 2.2.1**: A consumer reads from the stream. It does not know who writes.

**RULE 2.2.2**: A consumer maintains its own `read_idx`. This is local state.

**RULE 2.2.3**: A consumer processes at its own pace.

**RULE 2.2.4**: Consumers do not communicate with each other.

### 2.3 Forbidden Patterns

**RULE 2.3.1**: NO callbacks between components.

**RULE 2.3.2**: NO injected dependencies (`SetXxx()` methods).

**RULE 2.3.3**: NO shared state other than the stream.

**RULE 2.3.4**: NO mutexes, locks, or condition variables.

**RULE 2.3.5**: NO channels, queues, or message passing between components.

---

## 3. Lock-Free By Design

### 3.1 Atomic Operations Only

**RULE 3.1.1**: The only synchronization primitive is atomic operations on stream indices.

**RULE 3.1.2**: `atomic_fetch_add_explicit(&write_idx, 1, memory_order_acq_rel)` is the only write synchronization.

**RULE 3.1.3**: `atomic_load_explicit(&write_idx, memory_order_acquire)` is the only read synchronization.

**RULE 3.1.4**: No operation shall block waiting for another operation.

### 3.2 Memory Ordering

```c
#include <stdatomic.h>

// Producer: acq_rel (both acquire and release semantics)
size_t idx = atomic_fetch_add_explicit(&write_idx, 1, memory_order_acq_rel);

// Consumer: acquire (sees all writes before this index)
size_t head = atomic_load_explicit(&write_idx, memory_order_acquire);
```

**RULE 3.2.1**: Producers use `memory_order_acq_rel` for read-modify-write operations.

**RULE 3.2.2**: Consumers use `memory_order_acquire` for read operations.

**RULE 3.2.3**: Relaxed ordering is permitted only for statistics counters.

---

## 4. Hard Real-Time Consumers

### 4.1 Deadline Contract

```
┌─────────────────────────────────────────────────────────────┐
│                     HARD RT CONSUMER                        │
│                                                             │
│   "I MUST keep up with the stream, or I FAULT."            │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**RULE 4.1.1**: A Hard RT consumer has a maximum allowed lag (in samples).

**RULE 4.1.2**: Exceeding maximum lag triggers a FAULT.

**RULE 4.1.3**: A FAULT immediately stops output (audio off, TX off).

**RULE 4.1.4**: Corrupted timing is worse than silence. Always FAULT.

### 4.2 Fault Behavior

**RULE 4.2.1**: On FAULT, the Hard RT consumer stops processing.

**RULE 4.2.2**: FAULT state is visible (LED, log, diagnostic).

**RULE 4.2.3**: Recovery requires explicit resync after idle period.

**RULE 4.2.4**: FAULT history is preserved for diagnostics.

### 4.3 Hard RT Path

```
GPIO Poll ──▶ Iambic FSM ──▶ Stream Push ──▶ Audio/TX Consumer
                                                    │
                                              HARD RT PATH
                                              Max latency: 100µs
```

**RULE 4.3.1**: The Hard RT path is: GPIO → Iambic → Stream → Audio/TX.

**RULE 4.3.2**: Hard RT path shall achieve minimum possible latency, with an absolute ceiling of 100 microseconds.

**RULE 4.3.3**: Nothing in the Hard RT path may allocate memory.

**RULE 4.3.4**: Nothing in the Hard RT path may perform I/O (except GPIO).

**RULE 4.3.5**: Nothing in the Hard RT path may log (ESP_LOGx is forbidden).

**RULE 4.3.6**: The entire Hard RT path (GPIO read → Iambic tick → Stream push → Audio/TX output) MUST execute in a single thread with zero context switches. Producer and Hard RT consumer are co-located in the same task.

---

## 5. Best-Effort Consumers

### 5.1 Contract

```
┌─────────────────────────────────────────────────────────────┐
│                   BEST EFFORT CONSUMER                      │
│                                                             │
│   "I process when I can. I skip if I fall behind."         │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**RULE 5.1.1**: A Best-Effort consumer never FAULTs.

**RULE 5.1.2**: If behind, a Best-Effort consumer skips forward.

**RULE 5.1.3**: Skipped samples are counted for diagnostics.

**RULE 5.1.4**: Best-Effort consumers do not affect Hard RT path.

### 5.2 Examples

- Remote CW forwarder: Best-Effort
- Morse decoder: Best-Effort  
- Timeline logger: Best-Effort
- Diagnostics: Best-Effort

---

## 6. Threading Model

### 6.1 Thread Independence

**RULE 6.1.1**: Each thread owns its consumers. No sharing.

**RULE 6.1.2**: The only shared object is the KeyingStream (read-only reference).

**RULE 6.1.3**: Threads do not communicate except through the stream.

**RULE 6.1.4**: Thread creation/destruction does not affect other threads.

### 6.2 Core Affinity

```
┌────────────────────────────┐  ┌────────────────────────────┐
│         CORE 0             │  │         CORE 1             │
│                            │  │                            │
│  RT Task (priority MAX)    │  │  Background Task           │
│  • GPIO poll               │  │  • Remote consumer         │
│  • Iambic FSM              │  │  • Decoder consumer        │
│  • Stream push             │  │  • Diagnostics             │
│  • Audio/TX consume        │  │                            │
│                            │  │  WiFi/BT Stack             │
└────────────────────────────┘  └────────────────────────────┘
```

**RULE 6.2.1**: Hard RT task is pinned to Core 0.

**RULE 6.2.2**: Hard RT task has highest priority.

**RULE 6.2.3**: Best-Effort tasks run on Core 1.

**RULE 6.2.4**: WiFi/BT stack runs on Core 1.

### 6.3 Thread Simplicity

**RULE 6.3.1**: Each thread is a simple loop: read/process/write.

**RULE 6.3.2**: Each thread can be tested in isolation.

**RULE 6.3.3**: Adding a new consumer = adding a new thread. No changes elsewhere.

---

## 7. Testing Principles

### 7.1 Stream Enables Testing

**RULE 7.1.1**: Any component can be tested by providing a fake stream.

**RULE 7.1.2**: Streams can be recorded from hardware for replay tests.

**RULE 7.1.3**: Streams can be synthesized for edge case tests.

**RULE 7.1.4**: Golden output streams define expected behavior.

### 7.2 CI Requirements

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│ Recorded    │     │ Component   │     │ Expected    │
│ Input       │ ──▶ │ Under Test  │ ──▶ │ Output      │
│ Stream      │     │             │     │ Comparison  │
└─────────────┘     └─────────────┘     └─────────────┘
```

**RULE 7.2.1**: All timing-critical logic must have stream-based tests.

**RULE 7.2.2**: Tests run on host (no hardware required).

**RULE 7.2.3**: Recorded hardware captures are test fixtures.

**RULE 7.2.4**: Timing accuracy is verified to microsecond precision.

### 7.3 No Mocking

**RULE 7.3.1**: Components have no dependencies to mock.

**RULE 7.3.2**: The stream is the only interface.

**RULE 7.3.3**: If you need a mock, the design is wrong.

---

## 8. Fault Handling Philosophy

### 8.1 Core Principle

```
┌─────────────────────────────────────────────────────────────┐
│                                                             │
│   Corrupted CW timing is worse than silence.                │
│   If in doubt, FAULT and stop.                              │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**RULE 8.1.1**: A keyer that sends wrong timing is broken.

**RULE 8.1.2**: A keyer that sends nothing is safe.

**RULE 8.1.3**: FAULT is not failure. FAULT is correct behavior under stress.

### 8.2 Fault Visibility

**RULE 8.2.1**: FAULT state must be immediately visible (LED, audio, log).

**RULE 8.2.2**: FAULT cause must be logged with timestamp.

**RULE 8.2.3**: Stream history before FAULT must be preserved.

**RULE 8.2.4**: FAULT count is a permanent diagnostic counter.

---

## 9. Memory and Allocation

### 9.1 Static Allocation

**RULE 9.1.1**: The stream is statically allocated.

**RULE 9.1.2**: All consumers are statically allocated.

**RULE 9.1.3**: No heap allocation in the Hard RT path.

**RULE 9.1.4**: PSRAM is used for stream buffer (large, not latency-critical).

### 9.2 Buffer Sizing

**RULE 9.2.1**: Stream buffer holds 10-30 seconds of worst-case data.

**RULE 9.2.2**: Silence compression makes actual usage much smaller.

**RULE 9.2.3**: Buffer overflow is a FAULT condition.

---

## 10. Language and Platform

### 10.1 C (ESP-IDF)

**RULE 10.1.1**: Implementation language is C (C11 standard).

**RULE 10.1.2**: All synchronization uses C11 `stdatomic.h` primitives.

**RULE 10.1.3**: No dynamic allocation in RT path (`malloc`, `free` forbidden).

**RULE 10.1.4**: Strict compiler warnings (`-Wall -Wextra -Werror`) enforced.

### 10.2 Target Platform

**RULE 10.2.1**: Primary target: ESP32-S3, ESP32-P4.

**RULE 10.2.2**: Core logic must compile and test on host (x86/ARM).

**RULE 10.2.3**: HAL is a thin wrapper, not business logic.

---

## 11. Logging

### 11.1 RT-Safe Logging

```
┌─────────────────────────────────────────────────────────────┐
│                      LOGGING ARCHITECTURE                   │
│                                                             │
│   RT Thread              LogStream            UART Thread   │
│   ──────────             ─────────            ───────────   │
│                                                             │
│   RT_INFO() ──────────▶ [L0][L1][L2] ──────▶ UART TX       │
│   ~100ns                  lock-free           blocking ok   │
│   non-blocking            ring buffer         Core 1        │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**RULE 11.1.1**: The RT path shall NEVER call blocking log functions.

**RULE 11.1.2**: `ESP_LOGx`, `printf`, `puts` are FORBIDDEN in RT path.

**RULE 11.1.3**: RT path uses `RT_INFO()`, `RT_WARN()`, `RT_ERROR()` macros which push to LogStream.

**RULE 11.1.4**: LogStream is lock-free SPSC (single producer per core, single consumer).

**RULE 11.1.5**: Log push completes in < 200 nanoseconds.

### 11.2 Log Consumer

**RULE 11.2.1**: UART transmission runs in dedicated low-priority thread on Core 1.

**RULE 11.2.2**: Log messages may be dropped if ring full. This is acceptable.

**RULE 11.2.3**: Dropped message count is tracked and reported periodically.

**RULE 11.2.4**: Log consumer never affects RT path timing.

### 11.3 Forbidden vs Allowed

```c
// ═══════════════════════════════════════════════════════════
// FORBIDDEN in RT path — blocks for milliseconds
// ═══════════════════════════════════════════════════════════
ESP_LOGI(TAG, "...");       // ESP-IDF log: UART mutex + TX
printf("...");              // C printf: blocks
puts("...");                // C puts: blocks
fprintf(stderr, "...");     // C fprintf: blocks

// ═══════════════════════════════════════════════════════════
// ALLOWED in RT path — non-blocking push, ~100-200ns
// ═══════════════════════════════════════════════════════════
int64_t now_us = esp_timer_get_time();
RT_INFO(&g_rt_log_stream, now_us, "Key %d @ %lld", key_down, timestamp);
RT_WARN(&g_rt_log_stream, now_us, "Lag %u samples", lag);
RT_ERROR(&g_rt_log_stream, now_us, "FAULT: %s", fault_code_str(code));
```

### 11.4 LogStream Structure

```c
typedef struct {
    int64_t timestamp_us;       // Capture time
    log_level_t level;          // INFO/WARN/ERROR
    uint8_t len;                // Message length
    char msg[LOG_MAX_MSG_LEN];  // Pre-formatted message (120 bytes)
} log_entry_t;

typedef struct {
    log_entry_t entries[LOG_BUFFER_SIZE];  // Ring buffer (256 entries)
    atomic_uint write_idx;                  // Producer index
    atomic_uint read_idx;                   // Consumer index (UART thread)
    atomic_uint dropped;                    // Dropped message counter
} log_stream_t;
```

**RULE 11.4.1**: LogEntry is fixed size (no allocation).

**RULE 11.4.2**: Message formatting happens in caller's stack, not in ring.

**RULE 11.4.3**: Ring size (256 entries) provides ~2-5 seconds buffer at typical log rates.

### 11.5 Timing Budget

| Operation | Max Time | Notes |
|-----------|----------|-------|
| `RT_INFO()` push | 200ns | Format + atomic store |
| Ring full check | 10ns | Two atomic loads |
| Message drop | 5ns | Increment counter |
| UART drain | 1ms/msg | Background, Core 1 |

---

## Summary of Forbidden Patterns

| Pattern | Why Forbidden |
|---------|---------------|
| Callbacks between components | Creates hidden coupling |
| Dependency injection (`SetXxx()`) | Creates initialization order dependencies |
| Mutexes, locks | Can cause priority inversion, deadlock |
| Shared mutable state | Race conditions |
| Channels/queues between components | Hidden communication paths |
| Heap allocation in RT path | Non-deterministic timing |
| Blocking logs in RT path (`ESP_LOGx`, `printf`) | Blocks for milliseconds |
| Mocking in tests | Indicates design flaw |

---

## Summary of Required Patterns

| Pattern | Why Required |
|---------|--------------|
| Single stream as truth | Simplifies everything |
| Lock-free atomics only | Deterministic timing |
| Producer/consumer isolation | Testability, thread safety |
| FAULT on deadline miss | Correctness over availability |
| Static allocation | Deterministic memory |
| Stream-based testing | Reproducible, no hardware needed |
| RT-safe logging (`RT_INFO()`) | Non-blocking diagnostics |

---

## Compliance

Every code review must verify:

1. Does this change communicate through the stream only?
2. Does this change avoid forbidden patterns?
3. Does this change maintain Hard RT latency budget?
4. Does this change have stream-based tests?
5. Does this change preserve FAULT semantics?
6. Does this change use only `RT_*()` macros in RT path (no blocking logs)?

Non-compliant code shall not be merged.

---

## Document Control

This document may only be amended by unanimous agreement and documented rationale. Amendments must not weaken the core principles.

**Principle hierarchy (highest to lowest):**

1. FAULT on timing corruption
2. Lock-free stream as only shared state
3. Producer/consumer isolation
4. Non-blocking operations in RT path (including logging)
5. Static allocation
6. Testability

In case of conflict, higher-ranked principles take precedence.

### Amendment History

**Amendment 001** (2025-01-19):
- **Section**: 4.3.2 (Hard Real-Time Consumers)
- **Change**: Clarified latency requirement from "Total latency budget: 100 microseconds" to "achieve minimum possible latency, with an absolute ceiling of 100 microseconds"
- **Rationale**: The original formulation could be misinterpreted as a target rather than a limit. The new wording emphasizes that:
  1. Latency must be **minimized** in the design (not just "kept under budget")
  2. 100µs is an **absolute ceiling**, not a performance goal
  3. This strengthens the real-time guarantee without weakening core principles
- **Impact**: README.md updated for consistency. Does not weaken any core principle; strengthens real-time commitment.

**Amendment 002** (2025-01-19):
- **Section**: 4.3.6 (Hard Real-Time Consumers)
- **Change**: Added new rule mandating zero context switches on RT path
- **Rationale**: Context switching is a major source of latency and jitter:
  1. ESP32 context switch: 5-20µs typical, unpredictable
  2. Cumulative effect at 10 kHz tick rate would violate latency ceiling
  3. Co-locating Producer and Hard RT Consumer in single thread eliminates this entirely
  4. GPIO→Audio/TX path becomes deterministic single-thread execution
- **Impact**: Threading model simplified. Producer and Audio/TX consumer run in same task. Reinforces minimum latency principle (Amendment 001).

---

## Appendix: Project Identity

**Name**: RemoteCWKeyerV3

**Mission**: A CW keyer that is correct, testable, and maintainable. Remote operation is a first-class feature, not an afterthought.

**Non-goals**: 
- Maximum features
- Backward compatibility with legacy architectures
- Supporting every ESP32 variant

**Repository structure**:
```
RemoteCWKeyerV3/
├── ARCHITECTURE.md          # This document (immutable)
├──  /                 # C implementation (ESP-IDF)
│   ├── components/
│   │   ├── keyer_core/      # stream, sample, consumer, fault
│   │   ├── keyer_iambic/    # Iambic FSM (pure logic)
│   │   ├── keyer_audio/     # Sidetone, buffer, PTT
│   │   ├── keyer_logging/   # RT-safe logging
│   │   ├── keyer_console/   # Serial console
│   │   ├── keyer_config/    # Generated config
│   │   └── keyer_hal/       # Hardware abstraction
│   ├── main/                # Entry point, RT/BG tasks
│   ├── test_host/           # Unity tests (host)
│   └── CMakeLists.txt       # ESP-IDF project
├── parameters.yaml          # Configuration source of truth
└── docs/                    # Design documents
```
