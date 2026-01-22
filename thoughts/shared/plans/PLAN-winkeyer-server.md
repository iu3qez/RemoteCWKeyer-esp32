# Implementation Plan: Winkeyer v3 Protocol Server

**Generated:** 2026-01-22
**Spec:** thoughts/shared/specs/2026-01-22-winkeyer-server-spec.md
**Handoff:** thoughts/shared/handoffs/build-20260122-winkeyer-server/
**Status:** Ready for implementation

---

## Goal

Implement a K1EL Winkeyer v3 protocol server over USB CDC that integrates with the existing iambic keyer infrastructure. The server will allow logging software (N1MM, fldigi, etc.) to send CW text and control keyer parameters while local paddle operations maintain priority.

**Key constraints (from ARCHITECTURE.md):**
- Stream is truth - all keying through `keying_stream_t`
- No heap allocation in RT path
- Atomic-only cross-core communication
- RT latency budget: 100 microseconds
- FAULT on timing violations

---

## Research Summary

### Existing Infrastructure Analysis

**USB CDC (`keyer_usb/`):**
- TinyUSB multi-CDC already configured with 3 interfaces:
  - CDC0: Console (interactive commands)
  - CDC1: Log (RT-safe ring buffer drain)
  - CDC2: Winkeyer3 (STUB - our target)
- `usb_winkeyer.c` exists as stub returning ESP_OK
- Pattern established: register callback via `tinyusb_cdcacm_register_callback()`
- Write via `tinyusb_cdcacm_write_queue()` + `tinyusb_cdcacm_write_flush()`

**Configuration System (`keyer_config/`):**
- `g_config` uses atomic fields for lock-free cross-core access
- `CONFIG_GET_*()` / `CONFIG_SET_*()` macros with `memory_order_relaxed`
- `config_bump_generation()` signals changes to RT task
- RT task checks `generation` counter during IDLE state and reloads config

**Iambic FSM (`keyer_iambic/`):**
- Pure logic, no hardware dependencies
- `iambic_tick(proc, now_us, gpio)` produces `stream_sample_t`
- Key insight: GPIO state drives the FSM - we need to inject "virtual" paddle presses
- Timing calculated from WPM: `iambic_dit_duration_us()`, `iambic_dah_duration_us()`

**RT Task (`main/rt_task.c`):**
- 1ms tick rate, Core 0, highest priority
- Reads `g_config` atomically, reloads during IDLE
- Flow: GPIO poll -> iambic_tick -> stream_push -> audio/TX consume
- **Critical**: Cannot block or allocate

**BG Task (`main/bg_task.c`):**
- Core 1, low priority
- Best-effort stream consumer pattern
- 1ms tick via `vTaskDelay()`

### Winkeyer v3 Protocol Insights

- Binary protocol, commands 0x00-0x1F
- Admin (0x00) has sub-commands for session management
- Status byte 0xC0|flags sent on state changes
- Text characters (0x20-0x7F) queued for morse transmission
- XOFF/XON flow control when buffer 2/3 full

---

## Existing Codebase Patterns

### Lock-Free Cross-Core Communication Pattern

```c
// Producer (Core 1) - config update
CONFIG_SET_WPM(new_wpm);  // atomic store + bump generation

// Consumer (Core 0) - config read
if (current_gen != last_config_gen && iambic.state == IAMBIC_STATE_IDLE) {
    iambic_cfg.wpm = CONFIG_GET_WPM();  // atomic load
    iambic_set_config(&iambic, &iambic_cfg);
    last_config_gen = current_gen;
}
```

### USB CDC Callback Pattern

```c
static void console_rx_callback(int itf, cdcacm_event_t *event) {
    if (itf != TINYUSB_CDC_ACM_0) return;

    uint8_t buf[64];
    size_t len = 0;
    tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, buf, sizeof(buf), &len);
    // Process bytes...
}

// Registration
tinyusb_cdcacm_register_callback(
    TINYUSB_CDC_ACM_0,
    CDC_EVENT_RX,
    console_rx_callback
);
```

### Testing Pattern (Unity on Host)

```c
// test_*.c follows pattern:
void test_feature_name(void) {
    // Setup
    component_t comp;
    config_t cfg = CONFIG_DEFAULT;
    component_init(&comp, &cfg);

    // Act
    result = component_action(&comp, input);

    // Assert
    TEST_ASSERT_EQUAL(expected, result);
}
```

---

## Architecture Design

### Data Flow

```
USB CDC2 (Core 1)
      |
      v
+------------------+     +-------------------+
| Winkeyer Parser  |---->| Morse Element     |
| (state machine)  |     | Queue (lock-free) |
+------------------+     +-------------------+
      |                           |
      | config updates            | morse elements
      v                           v
+------------------+     +-------------------+
| g_config         |     | RT Task (Core 0)  |
| (atomics)        |     | - Check paddle    |
+------------------+     | - If no paddle:   |
                         |   consume queue   |
                         +-------------------+
```

### Morse Element Queue Design

Lock-free SPSC (single producer, single consumer):
- **Producer**: Winkeyer controller on Core 1
- **Consumer**: RT task on Core 0

```c
typedef enum {
    MORSE_ELEMENT_DIT,
    MORSE_ELEMENT_DAH,
    MORSE_ELEMENT_CHAR_SPACE,  // 3 dit gap (inter-char)
    MORSE_ELEMENT_WORD_SPACE,  // 7 dit gap (inter-word)
    MORSE_ELEMENT_KEY_DOWN,    // Immediate key (cmd 0x0B)
    MORSE_ELEMENT_KEY_UP,      // Immediate key release
} morse_element_type_t;

typedef struct {
    morse_element_type_t type;
} morse_element_t;

#define MORSE_QUEUE_SIZE 64  // Static, power of 2

typedef struct {
    morse_element_t buffer[MORSE_QUEUE_SIZE];
    atomic_size_t write_idx;  // Producer (Core 1)
    atomic_size_t read_idx;   // Consumer (Core 0)
} morse_queue_t;
```

### Priority Arbitration

RT task decides when to consume from morse queue:
1. Check local paddle GPIO
2. If paddle active -> use paddle (local priority)
3. If paddle idle AND morse queue not empty -> consume queue element
4. Send BREAKIN status if paddle interrupts winkeyer transmission

---

## Implementation Phases

### Phase 1: Morse Lookup Table & Queue Infrastructure

**Files to create:**
- `components/keyer_winkeyer/include/winkeyer_morse.h` - Morse encoding table
- `components/keyer_winkeyer/src/winkeyer_morse.c` - Lookup implementation
- `components/keyer_winkeyer/include/morse_queue.h` - Lock-free queue
- `components/keyer_winkeyer/src/morse_queue.c` - Queue implementation
- `test_host/test_morse.c` - Unit tests

**Steps:**
1. Create `keyer_winkeyer` component directory structure:
   ```
   components/keyer_winkeyer/
   ├── CMakeLists.txt
   ├── include/
   │   ├── winkeyer.h
   │   ├── winkeyer_morse.h
   │   └── morse_queue.h
   └── src/
       ├── winkeyer.c
       ├── winkeyer_morse.c
       └── morse_queue.c
   ```

2. Implement ASCII-to-Morse lookup table:
   ```c
   // ITU morse encoding as bit patterns
   // 0=dit, 1=dah, length encoded in high bits
   typedef struct {
       uint8_t pattern;  // LSB first, 0=dit, 1=dah
       uint8_t length;   // Number of elements (1-6)
   } morse_char_t;

   const morse_char_t *morse_lookup(char c);
   ```

3. Implement lock-free morse queue:
   ```c
   void morse_queue_init(morse_queue_t *q);
   bool morse_queue_push(morse_queue_t *q, morse_element_t elem);  // Core 1
   bool morse_queue_pop(morse_queue_t *q, morse_element_t *elem);  // Core 0
   bool morse_queue_is_empty(const morse_queue_t *q);
   size_t morse_queue_count(const morse_queue_t *q);
   void morse_queue_clear(morse_queue_t *q);
   ```

4. Add tests to `test_host/CMakeLists.txt`

**Acceptance criteria:**
- [ ] Morse lookup covers A-Z, 0-9, common punctuation
- [ ] Queue push/pop is lock-free with atomic indices
- [ ] All tests pass with sanitizers enabled
- [ ] Zero heap allocation in queue operations

---

### Phase 2: Winkeyer Protocol Parser

**Files to create:**
- `components/keyer_winkeyer/include/winkeyer_protocol.h` - Protocol constants
- `components/keyer_winkeyer/src/winkeyer_parser.c` - State machine
- `test_host/test_winkeyer_parser.c` - Parser tests

**Steps:**
1. Define protocol constants:
   ```c
   // Command codes
   #define WK_CMD_ADMIN           0x00
   #define WK_CMD_SIDETONE        0x01
   #define WK_CMD_SPEED           0x02
   #define WK_CMD_WEIGHT          0x03
   #define WK_CMD_PTT_TIMING      0x04
   // ... etc

   // Admin sub-commands
   #define WK_ADMIN_CALIBRATE     0x00
   #define WK_ADMIN_RESET         0x01
   #define WK_ADMIN_HOST_OPEN     0x02
   #define WK_ADMIN_HOST_CLOSE    0x03
   // ... etc

   // Status byte
   #define WK_STATUS_BASE         0xC0
   #define WK_STATUS_XOFF         0x01
   #define WK_STATUS_BREAKIN      0x02
   #define WK_STATUS_BUSY         0x04
   ```

2. Implement parser state machine:
   ```c
   typedef enum {
       WK_STATE_IDLE,
       WK_STATE_ADMIN_WAIT_SUB,
       WK_STATE_CMD_PARAM1,
       WK_STATE_CMD_PARAM2,
   } winkeyer_parser_state_t;

   typedef struct {
       winkeyer_parser_state_t state;
       uint8_t current_cmd;
       uint8_t param1;
       bool session_open;
       // ... response buffer
   } winkeyer_parser_t;

   void winkeyer_parser_init(winkeyer_parser_t *p);
   void winkeyer_parser_byte(winkeyer_parser_t *p, uint8_t byte,
                             winkeyer_callbacks_t *cb);
   ```

3. Define callback interface for parser actions:
   ```c
   typedef struct {
       void (*on_host_open)(void *ctx);
       void (*on_host_close)(void *ctx);
       void (*on_speed)(void *ctx, uint8_t wpm);
       void (*on_sidetone)(void *ctx, uint8_t freq_code);
       void (*on_text)(void *ctx, char c);
       void (*on_clear_buffer)(void *ctx);
       void (*on_key_immediate)(void *ctx, bool down);
       // ... etc
       void *ctx;
   } winkeyer_callbacks_t;
   ```

4. Add comprehensive parser tests

**Acceptance criteria:**
- [ ] Parser handles all commands from spec
- [ ] Invalid commands ignored gracefully (no crash)
- [ ] State machine tested with recorded USB captures
- [ ] Parser has no side effects (pure state machine)

---

### Phase 3: Winkeyer Controller

**Files to modify:**
- `components/keyer_winkeyer/src/winkeyer.c` - Main controller

**Steps:**
1. Implement controller that connects parser to actions:
   ```c
   typedef struct {
       winkeyer_parser_t parser;
       morse_queue_t *morse_queue;  // Shared with RT task

       // Session state
       bool session_open;
       uint8_t status;

       // Response buffer
       uint8_t tx_buf[32];
       size_t tx_len;

       // Text buffer
       char text_buf[128];
       size_t text_head;
       size_t text_tail;
   } winkeyer_controller_t;

   void winkeyer_init(winkeyer_controller_t *wk, morse_queue_t *queue);
   void winkeyer_process_rx(winkeyer_controller_t *wk,
                             const uint8_t *data, size_t len);
   size_t winkeyer_get_tx(winkeyer_controller_t *wk,
                           uint8_t *buf, size_t max_len);
   ```

2. Implement text-to-morse conversion:
   ```c
   // Called periodically to feed morse queue from text buffer
   void winkeyer_pump_text(winkeyer_controller_t *wk);
   ```

3. Implement config updates via atomic macros:
   ```c
   static void on_speed(void *ctx, uint8_t wpm) {
       CONFIG_SET_WPM(wpm);
   }

   static void on_sidetone(void *ctx, uint8_t freq_code) {
       uint16_t freq_hz = 4000 / (freq_code + 1);  // Winkeyer formula
       CONFIG_SET_SIDETONE_FREQ_HZ(freq_hz);
   }
   ```

4. Implement status byte generation:
   ```c
   uint8_t winkeyer_get_status(const winkeyer_controller_t *wk);
   void winkeyer_set_breakin(winkeyer_controller_t *wk, bool breakin);
   ```

**Acceptance criteria:**
- [ ] Config updates via atomic macros (no direct struct access)
- [ ] Text buffer prevents overflow (XON/XOFF)
- [ ] Status byte reflects actual state
- [ ] No blocking operations

---

### Phase 4: USB CDC Integration

**Files to modify:**
- `components/keyer_usb/src/usb_cdc.c` - Add CDC2 initialization
- `components/keyer_usb/src/usb_winkeyer.c` - Replace stub with real implementation
- `components/keyer_usb/include/usb_winkeyer.h` - Update API

**Steps:**
1. Initialize CDC2 in `usb_cdc_init()`:
   ```c
   // Add after CDC1 init
   acm_cfg.cdc_port = TINYUSB_CDC_ACM_2;
   ret = tusb_cdc_acm_init(&acm_cfg);
   ```

2. Update `usb_winkeyer.c`:
   ```c
   static winkeyer_controller_t s_wk;
   static morse_queue_t s_morse_queue;  // Shared with RT task

   // Global accessor for RT task
   morse_queue_t *winkeyer_get_morse_queue(void);

   static void winkeyer_rx_callback(int itf, cdcacm_event_t *event) {
       if (itf != TINYUSB_CDC_ACM_2) return;

       uint8_t buf[64];
       size_t len = 0;
       tinyusb_cdcacm_read(TINYUSB_CDC_ACM_2, buf, sizeof(buf), &len);

       winkeyer_process_rx(&s_wk, buf, len);

       // Send any pending response
       uint8_t tx[32];
       size_t tx_len = winkeyer_get_tx(&s_wk, tx, sizeof(tx));
       if (tx_len > 0) {
           tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_2, tx, tx_len);
           tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_2, 0);
       }
   }

   esp_err_t usb_winkeyer_init(void) {
       morse_queue_init(&s_morse_queue);
       winkeyer_init(&s_wk, &s_morse_queue);

       tinyusb_cdcacm_register_callback(
           TINYUSB_CDC_ACM_2,
           CDC_EVENT_RX,
           winkeyer_rx_callback
       );

       return ESP_OK;
   }
   ```

3. Create periodic pump task or integrate with bg_task:
   ```c
   // In bg_task.c or separate winkeyer_task
   winkeyer_pump_text(&s_wk);  // Convert text to morse elements
   ```

**Acceptance criteria:**
- [ ] CDC2 receives data from host
- [ ] Winkeyer responds to Admin commands
- [ ] Host Open returns version byte
- [ ] Speed/sidetone changes reflected in g_config

---

### Phase 5: RT Task Integration (HIGH RISK - CRITICAL)

> **USER DIRECTIVE:** This phase requires maximum caution. Write comprehensive tests,
> implement performance monitoring, and stress test thoroughly to ensure ZERO
> interference with the RT task's 100μs latency budget.

**Files to modify:**
- `main/rt_task.c` - Add morse queue consumption
- `components/keyer_usb/include/usb_winkeyer.h` - Expose morse queue

**Risk Mitigations (MANDATORY):**

1. **Instrumentation First:**
   - Add RT timing instrumentation BEFORE any morse queue code
   - Measure baseline tick duration without winkeyer
   - Log max/min/avg tick duration to RT log stream

2. **Unit Tests (Host):**
   - `test_host/test_winkeyer_rt.c` - Test priority arbitration logic
   - Mock morse queue consumer with various queue states
   - Test paddle interrupt scenarios exhaustively

3. **Stress Tests (Device):**
   - Continuous queue consumption under load
   - Rapid paddle interrupt/resume cycles
   - Full queue edge cases
   - Empty queue polling overhead

4. **Performance Budget:**
   - Queue poll: <1μs (single atomic load)
   - Queue pop: <5μs (atomic load + store)
   - Element processing: <10μs
   - Total overhead: <20μs (leaves 80μs margin)

5. **Fallback Strategy:**
   - If latency exceeds 80μs, disable winkeyer queue consumption
   - Set FAULT and log the issue
   - Local paddle must always work regardless of winkeyer state

**Steps:**
1. Add morse queue consumer state to RT task:
   ```c
   // In rt_task.c
   extern morse_queue_t *winkeyer_get_morse_queue(void);

   // New state for virtual keying
   typedef struct {
       morse_element_t current;
       int64_t element_end_us;
       bool active;
   } winkeyer_tx_state_t;

   static winkeyer_tx_state_t s_wk_tx = {0};
   ```

2. Implement priority arbitration in RT loop:
   ```c
   // After GPIO poll, before iambic_tick
   bool paddle_active = !gpio_is_idle(gpio);

   if (paddle_active) {
       // Local paddle takes priority
       if (s_wk_tx.active) {
           // Interrupted winkeyer - set BREAKIN status
           usb_winkeyer_set_breakin(true);
           s_wk_tx.active = false;
       }
       // Use normal iambic processing
       sample = iambic_tick(&iambic, now_us, gpio);
   } else if (s_wk_tx.active || !morse_queue_is_empty(winkeyer_get_morse_queue())) {
       // Process winkeyer morse
       sample = winkeyer_consume_element(&s_wk_tx, now_us, &iambic.config);
   } else {
       // Idle
       sample = iambic_tick(&iambic, now_us, gpio);
   }
   ```

3. Implement element consumption:
   ```c
   static stream_sample_t winkeyer_consume_element(
       winkeyer_tx_state_t *state,
       int64_t now_us,
       const iambic_config_t *cfg)
   {
       stream_sample_t sample = STREAM_SAMPLE_EMPTY;

       if (!state->active) {
           // Start new element
           morse_element_t elem;
           if (!morse_queue_pop(winkeyer_get_morse_queue(), &elem)) {
               return sample;  // Queue empty
           }

           state->current = elem;
           state->active = true;

           // Calculate duration based on element type
           int64_t duration;
           switch (elem.type) {
               case MORSE_ELEMENT_DIT:
                   duration = iambic_dit_duration_us(cfg);
                   break;
               case MORSE_ELEMENT_DAH:
                   duration = iambic_dah_duration_us(cfg);
                   break;
               case MORSE_ELEMENT_CHAR_SPACE:
                   duration = iambic_dit_duration_us(cfg) * 3;
                   break;
               case MORSE_ELEMENT_WORD_SPACE:
                   duration = iambic_dit_duration_us(cfg) * 7;
                   break;
               // ... etc
           }
           state->element_end_us = now_us + duration;
       }

       // Check if element complete
       if (now_us >= state->element_end_us) {
           state->active = false;
           sample.local_key = 0;
       } else {
           // DIT/DAH = key down, spaces = key up
           sample.local_key = (state->current.type == MORSE_ELEMENT_DIT ||
                               state->current.type == MORSE_ELEMENT_DAH) ? 1 : 0;
       }

       return sample;
   }
   ```

4. Handle inter-element gaps:
   - After each DIT/DAH, automatically insert 1-dit gap
   - Implement as implicit state or explicit queue elements

**Acceptance criteria:**
- [ ] Winkeyer text transmits as CW
- [ ] Local paddle interrupts winkeyer transmission
- [ ] BREAKIN status sent on paddle interrupt
- [ ] Timing matches iambic config (WPM-accurate)
- [ ] RT latency budget maintained (<100μs) - **VERIFIED BY MEASUREMENT**
- [ ] RT timing instrumentation shows max tick <80μs with winkeyer active
- [ ] Stress test: 1000 paddle interrupt cycles with zero latency violations
- [ ] Stress test: Full queue drain with continuous timing measurement
- [ ] Host unit tests cover all priority arbitration edge cases
- [ ] Logic analyzer verification of RT timing (optional but recommended)

---

### Phase 6: Status Reporting & Flow Control

**Files to modify:**
- `components/keyer_winkeyer/src/winkeyer.c` - Status management
- `main/rt_task.c` - Status updates

**Steps:**
1. Implement atomic status flags:
   ```c
   // In winkeyer.h
   extern atomic_uint g_winkeyer_status;

   #define WINKEYER_STATUS_XOFF    0x01
   #define WINKEYER_STATUS_BREAKIN 0x02
   #define WINKEYER_STATUS_BUSY    0x04

   void winkeyer_set_breakin(bool breakin);
   void winkeyer_set_busy(bool busy);
   ```

2. Monitor queue fill level:
   ```c
   void winkeyer_pump_text(winkeyer_controller_t *wk) {
       size_t queue_fill = morse_queue_count(wk->morse_queue);
       bool xoff = (queue_fill > MORSE_QUEUE_SIZE * 2 / 3);

       if (xoff != wk->xoff_sent) {
           wk->xoff_sent = xoff;
           // Update status and send to host
       }
   }
   ```

3. Send status byte on changes:
   ```c
   // Periodic check in USB task
   uint8_t new_status = winkeyer_get_status(&s_wk);
   if (new_status != s_last_status) {
       uint8_t msg = WK_STATUS_BASE | new_status;
       tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_2, &msg, 1);
       tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_2, 0);
       s_last_status = new_status;
   }
   ```

**Acceptance criteria:**
- [ ] XOFF sent when queue 2/3 full
- [ ] XON sent when queue drains
- [ ] BREAKIN status on paddle interrupt
- [ ] BUSY status while transmitting

---

## Testing Strategy

### Unit Tests (Host)

| Test File | Coverage |
|-----------|----------|
| `test_morse.c` | Morse lookup, queue push/pop, edge cases |
| `test_winkeyer_parser.c` | All commands, invalid input, state transitions |
| `test_winkeyer_integration.c` | Text-to-morse conversion, config updates |

### Integration Tests (Device)

1. **USB Loopback**: Send commands via pyserial, verify responses
2. **N1MM Compatibility**: Run N1MM CW macros, verify audio output
3. **fldigi Compatibility**: Configure fldigi with Winkeyer, send test QSO
4. **Paddle Priority**: Send text, interrupt with paddle, verify BREAKIN

### Test Commands

```python
# test_winkeyer.py
import serial

# Host Open
ser.write(b'\x00\x02')  # Admin, Host Open
assert ser.read(1) == b'\x17'  # Version 23 (WK3)

# Set speed
ser.write(b'\x02\x19')  # Speed cmd, 25 WPM
# Verify via console: "cfg wpm" should show 25

# Send text
ser.write(b'CQ TEST')
# Verify audio output
```

---

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| **RT latency exceeded** | **CRITICAL**: Instrument timing, measure baseline, set hard 80μs budget, FAULT on violation |
| RT timing regression | Add RT timing metrics to CI (measure before/after each change) |
| Buffer overflow on fast typing | Proper XOFF/XON flow control |
| Incompatibility with logging software | Test with N1MM, fldigi, DXLab |
| Race conditions | Use only atomic operations; verify with ThreadSanitizer |
| Text buffer exhaustion | Static allocation; reject when full |
| Queue poll overhead | Verify atomic_load is <1μs; use relaxed memory order |
| Paddle priority failure | Test paddle interrupt with queue in all states (empty, partial, full) |

### RT Safety Verification Protocol

Before merging Phase 5:

1. **Baseline Measurement:**
   ```
   Without winkeyer: max_tick_us = X
   With winkeyer (idle queue): max_tick_us = Y
   With winkeyer (active queue): max_tick_us = Z

   PASS if Z < 80μs
   FAIL if Z >= 80μs - revert and optimize
   ```

2. **Stress Test Protocol:**
   ```bash
   # On device, run for 5 minutes:
   # - Continuous winkeyer text transmission
   # - Random paddle interrupts (hardware or simulated)
   # - Monitor RT log for any latency warnings

   # Success: ZERO latency violations
   # Failure: ANY single violation = investigate before merge
   ```

3. **Regression Gate:**
   - Add `test_host/test_winkeyer_rt.c` to CI
   - All RT-related tests must pass with sanitizers

---

## File Summary

### New Files

| Path | Purpose |
|------|---------|
| `components/keyer_winkeyer/CMakeLists.txt` | Component build config |
| `components/keyer_winkeyer/include/winkeyer.h` | Public API |
| `components/keyer_winkeyer/include/winkeyer_protocol.h` | Protocol constants |
| `components/keyer_winkeyer/include/winkeyer_morse.h` | Morse lookup API |
| `components/keyer_winkeyer/include/morse_queue.h` | Lock-free queue API |
| `components/keyer_winkeyer/src/winkeyer.c` | Main controller |
| `components/keyer_winkeyer/src/winkeyer_parser.c` | Protocol parser |
| `components/keyer_winkeyer/src/winkeyer_morse.c` | ASCII-to-Morse |
| `components/keyer_winkeyer/src/morse_queue.c` | Queue implementation |
| `test_host/test_morse.c` | Morse & queue tests |
| `test_host/test_winkeyer_parser.c` | Parser tests |

### Modified Files

| Path | Changes |
|------|---------|
| `components/keyer_usb/src/usb_cdc.c` | Initialize CDC2 |
| `components/keyer_usb/src/usb_winkeyer.c` | Replace stub with implementation |
| `components/keyer_usb/include/usb_winkeyer.h` | New API declarations |
| `main/rt_task.c` | Add morse queue consumption |
| `test_host/CMakeLists.txt` | Add winkeyer test sources |

---

## Estimated Complexity

| Phase | Effort | Risk |
|-------|--------|------|
| Phase 1: Morse Table & Queue | 2-3 hours | Low |
| Phase 2: Protocol Parser | 3-4 hours | Medium |
| Phase 3: Controller | 2-3 hours | Medium |
| Phase 4: USB Integration | 1-2 hours | Low |
| Phase 5: RT Integration | 4-6 hours | **CRITICAL** |
| Phase 6: Status & Flow Control | 2-3 hours | Medium |

**Total: ~15-20 hours (~2-3 days)**

---

## Dependencies Between Phases

```
Phase 1 (Morse/Queue) ─┬─> Phase 3 (Controller) ─> Phase 4 (USB)
                       │                                │
Phase 2 (Parser) ──────┘                                v
                                                   Phase 5 (RT)
                                                        │
                                                        v
                                                   Phase 6 (Status)
```

Phases 1 and 2 can be implemented in parallel. Phase 5 is the highest risk and should be done carefully with RT profiling.

---

## Out of Scope (Phase 1)

Per spec, these are explicitly excluded:
- Message memories (commands 18-22)
- Buffered speed change
- HSCW mode
- Prosign handling
- Serial number generation
- NVS persistence of Winkeyer parameters (session-only)

---

## Handoff Artifacts

Upon completion, produce:
- `thoughts/shared/handoffs/build-20260122-winkeyer-server/HANDOFF.md`
- Test results summary
- Any architectural decisions made during implementation
- Known limitations and future work items
