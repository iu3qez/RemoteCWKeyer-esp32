# Validation Checklist for Winkeyer Implementation

**This checklist tracks validation findings during implementation.**

---

## Pre-Implementation Verification (BLOCKING)

### Protocol Verification
- [ ] **REQUIRED:** Obtain official K1EL Winkeyer v3 protocol documentation
  - Source: _________________________ (document URL/path)
  - Date obtained: __________________
  - Version verified: ________________
- [ ] Verify sidetone frequency formula `4000 / (code + 1)` matches spec
  - Tested codes: 0→__Hz, 31→__Hz, 63→__Hz, 127→__Hz
  - Formula correct: YES / NO / NEEDS ADJUSTMENT
- [ ] Identify correct buffer overflow handling strategy
  - Strategy: [ ] FAULT [ ] DROP [ ] DISCONNECT [ ] OTHER: _________
  - Documented in code: YES / NO

### Baseline Measurements (GATE FOR PHASE 5)
- [ ] **Measure without Winkeyer code:**
  - Instrument rt_task.c with `RT_DEBUG()` for tick duration
  - Run 10,000 RT ticks on device, record max/avg/min
  - Baseline max_tick_us: _____________ μs
  - Measurement date: __________________
  - Measurement method: _____________________________
- [ ] **Baseline must be <50μs to proceed to Phase 5**
  - Current: _____ μs
  - Status: [ ] PASS [ ] FAIL - OPTIMIZE FIRST

---

## Phase 1: Morse Lookup Table & Queue Infrastructure

### Code Implementation
- [ ] `components/keyer_winkeyer/CMakeLists.txt` created
- [ ] `components/keyer_winkeyer/include/winkeyer_morse.h` created
  - Morse pattern lookup for A-Z, 0-9, punctuation
  - Pattern format documented (LSB first, length encoded)
- [ ] `components/keyer_winkeyer/src/winkeyer_morse.c` implemented
  - Lookup table complete
  - Edge cases handled (unknown chars, case insensitivity)
- [ ] `components/keyer_winkeyer/include/morse_queue.h` created
  - Queue data structure defined
  - Static allocation (size 64, power-of-2)
  - Function prototypes: push, pop, is_empty, count, clear
- [ ] `components/keyer_winkeyer/src/morse_queue.c` implemented
  - **CRITICAL:** Memory ordering documented
    - Producer: `memory_order_acq_rel` [ ] VERIFIED
    - Consumer: `memory_order_acquire` [ ] VERIFIED
  - Index wrapping: bitwise AND with (MORSE_QUEUE_SIZE-1) [ ] VERIFIED
  - Full queue behavior documented [ ] VERIFIED

### Testing
- [ ] `test_host/test_morse.c` created
  - Morse lookup tests for all characters
  - Tests pass: [ ] YES [ ] NO - Fix: ___________
- [ ] `test_host/test_morse_queue.c` created
  - Push/pop basic operations
  - Queue full scenario (returns false, doesn't overflow)
  - Queue empty scenario (returns false gracefully)
  - Wraparound at power-of-2 boundary
  - All tests pass with sanitizers: [ ] YES [ ] NO - Fix: ___________

### Code Quality
- [ ] Compiler warnings: [ ] ZERO [ ] N/A (Phase 1)
- [ ] PVS-Studio: [ ] CLEAN [ ] N/A (Phase 1)
- [ ] Memory ordering comments clear: [ ] YES [ ] NO - Fix: ___________

---

## Phase 2: Winkeyer Protocol Parser

### Code Implementation
- [ ] `components/keyer_winkeyer/include/winkeyer_protocol.h` created
  - All command constants (0x00-0x1F) defined
  - Admin sub-command constants defined
  - Status byte flags defined
  - Protocol documented/cited: ________________________
- [ ] `components/keyer_winkeyer/src/winkeyer_parser.c` implemented
  - State machine handles all command types
  - Invalid commands gracefully ignored (no crash)
  - Callback interface for actions defined
  - Callbacks: HOST_OPEN, HOST_CLOSE, SPEED, SIDETONE, TEXT, etc.

### Testing
- [ ] `test_host/test_winkeyer_parser.c` created
  - Valid commands tested
  - Invalid/malformed commands tested (must not crash)
  - State machine transitions tested
  - Multi-byte command sequences tested
  - All tests pass: [ ] YES [ ] NO - Fix: ___________

### Code Quality
- [ ] Protocol source cited in header comment: [ ] YES [ ] NO
- [ ] All magic numbers documented with spec reference: [ ] YES [ ] NO
- [ ] Error handling explicit (no silent drops): [ ] YES [ ] NO

---

## Phase 3: Winkeyer Controller

### Code Implementation
- [ ] `components/keyer_winkeyer/src/winkeyer.c` created
  - `winkeyer_init()` initializes controller and queue
  - `winkeyer_process_rx()` calls parser
  - `winkeyer_get_tx()` returns response buffer
  - `winkeyer_pump_text()` converts text to morse elements
  - Config updates use atomic macros:
    - CONFIG_SET_WPM() [ ] VERIFIED
    - CONFIG_SET_SIDETONE_FREQ_HZ() [ ] VERIFIED

### Configuration Integration
- [ ] CONFIG_SET_* macros used for all parameter updates [ ] VERIFIED
- [ ] Sidetone frequency formula implementation [ ] VERIFIED
  - Formula: ______________________________
  - Source: ________________________________
  - Test: code=0→__Hz, code=127→__Hz
- [ ] Text buffer management
  - Size defined: MORSE_TEXT_BUFFER_SIZE = ___
  - Overflow protection: [ ] SPECIFIED [ ] IMPLEMENTED [ ] TESTED
  - Strategy: ______________________________

### Testing
- [ ] Integration tests for config updates
- [ ] Sidetone frequency mapping tests with known reference values
- [ ] Text-to-morse conversion tests
- [ ] Buffer overflow scenario tests
- [ ] All tests pass: [ ] YES [ ] NO - Fix: ___________

---

## Phase 4: USB CDC Integration

### Code Implementation
- [ ] `components/keyer_usb/src/usb_cdc.c` updated
  - CDC2 initialization added (after CDC1)
  - Port: TINYUSB_CDC_ACM_2 [ ] VERIFIED
- [ ] `components/keyer_usb/src/usb_winkeyer.c` implemented
  - Replace stub with real implementation
  - `static winkeyer_controller_t s_wk;` created [ ] VERIFIED
  - `static morse_queue_t s_morse_queue;` created [ ] VERIFIED
  - `winkeyer_rx_callback()` implemented [ ] VERIFIED
    - Reads from CDC2 (checks `itf == TINYUSB_CDC_ACM_2`)
    - Calls `winkeyer_process_rx()`
    - Non-blocking (no delays in callback) [ ] VERIFIED
  - Response sending: write_queue + flush pattern [ ] VERIFIED

### Testing
- [ ] USB loopback test
  - Send command, verify response
  - Host Open command returns version byte [ ] VERIFIED
  - Speed command accepted [ ] VERIFIED
  - Text command accepted [ ] VERIFIED
- [ ] All tests pass: [ ] YES [ ] NO - Fix: ___________

---

## Phase 5: RT Task Integration (HIGH RISK - MEASUREMENT GATED)

### Pre-Phase 5 Verification
- [ ] **Baseline measurement complete:** ____________ μs max tick
- [ ] **Baseline < 50μs:** [ ] YES [ ] NO - Cannot proceed
- [ ] Timing instrumentation added to rt_task.c
  - RT_DEBUG() calls for tick duration [ ] VERIFIED
  - RT_DEBUG() for queue operations [ ] VERIFIED
  - Measurements logged to rt_log_stream [ ] VERIFIED

### Code Implementation
- [ ] `main/rt_task.c` updated
  - Include `usb_winkeyer.h` [ ] VERIFIED
  - `extern morse_queue_t *winkeyer_get_morse_queue();` [ ] VERIFIED
  - Winkeyer TX state structure added [ ] VERIFIED
    - `morse_element_t current`
    - `int64_t element_end_us`
    - `bool active`
  - Paddle check added before iambic_tick [ ] VERIFIED
  - Priority arbitration logic implemented [ ] VERIFIED
    - If paddle active: use iambic_tick, set BREAKIN if interrupted
    - If !paddle && !queue_empty: consume queue
    - Else: normal iambic_tick
  - Element consumption function [ ] VERIFIED
    - Handles DIT, DAH, CHAR_SPACE, WORD_SPACE
    - Inter-element gaps correctly timed (1-dit default)
    - Returns synthetic stream_sample_t [ ] VERIFIED

### Latency Measurement
- [ ] Measure RT tick with empty Winkeyer queue
  - Max tick: ____________ μs
  - Must be < 55μs: [ ] PASS [ ] FAIL
- [ ] Measure RT tick with active queue consumption
  - Max tick: ____________ μs
  - Must be < 75μs: [ ] PASS [ ] FAIL
- [ ] Measure RT tick with paddle interrupt
  - Max tick: ____________ μs
  - Must be < 80μs: [ ] PASS [ ] FAIL
- [ ] All latency tests PASS before proceeding

### Stress Testing
- [ ] Stress test implemented on device
  - Duration: 300 seconds continuous
  - Queue states: idle, filling, draining, full, empty cycles
  - Paddle interrupts: 10 Hz random
  - Test result: [ ] PASS (zero violations) [ ] FAIL - Details: _________
  - Max tick observed: ____________ μs
  - 99th percentile: ____________ μs
- [ ] Log dump shows no FAULT triggers: [ ] YES [ ] NO
- [ ] XOFF/XON flow control active (visible in logs): [ ] YES [ ] NO

### Paddle Interrupt Testing
- [ ] Test: Winkeyer transmitting "CQ", paddle pressed mid-element
  - Expected: Immediate switchover to paddle input
  - Actual: ______________________________
  - Status: [ ] PASS [ ] FAIL
- [ ] BREAKIN status sent on interrupt: [ ] VERIFIED
- [ ] Morse timing continues correctly: [ ] VERIFIED

### Code Quality (Phase 5)
- [ ] Compiler warnings: [ ] ZERO [ ] Issues: ___________
- [ ] PVS-Studio: [ ] CLEAN [ ] Issues: ___________
- [ ] Memory ordering in queue implementation: [ ] VERIFIED
- [ ] RT-safe logging only (RT_*() macros): [ ] VERIFIED
- [ ] No malloc, mutex, or blocking I/O in RT path: [ ] VERIFIED

### Pre-Merge Verification
- [ ] **All latency tests PASS:** [ ] YES [ ] NO
- [ ] **Stress test PASS (zero violations):** [ ] YES [ ] NO
- [ ] **Compiler warnings ZERO:** [ ] YES [ ] NO
- [ ] **PVS-Studio CLEAN:** [ ] YES [ ] NO
- [ ] **Can only merge if ALL four above are YES**

---

## Phase 6: Status Reporting & Flow Control

### Code Implementation
- [ ] Status byte generation implemented
  - XOFF flag set when queue > 2/3 full [ ] VERIFIED
  - BREAKIN flag set on paddle interrupt [ ] VERIFIED
  - BUSY flag set during transmission [ ] VERIFIED
- [ ] Flow control notification
  - XOFF sent to host when buffer fills [ ] VERIFIED
  - XON sent when buffer drains [ ] VERIFIED
  - Status updated on changes, not constantly [ ] VERIFIED
- [ ] Overflow behavior specified and implemented
  - Strategy: ______________________________
  - Code location: _________________________
  - FAULT triggered if needed: [ ] YES [ ] APPLICABLE

### Testing
- [ ] Fast text input triggers XOFF
  - Send "CQCQCQCQCQ" continuously
  - XOFF status byte sent: [ ] YES [ ] NO
  - Verify in device logs: [ ] CONFIRMED
- [ ] Host ignores XOFF (simulated)
  - Overflow behavior executes: [ ] YES [ ] NO
  - Correct status sent: [ ] YES [ ] NO

---

## Final Code Quality

### Compiler & Static Analysis
- [ ] All C files compile with strict flags
  ```
  -Wall -Wextra -Werror
  -Wconversion -Wsign-conversion
  -Wnull-dereference -Wformat=2
  ```
- [ ] Zero warnings: [ ] YES [ ] NO - List: ___________
- [ ] PVS-Studio report
  - Status: [ ] CLEAN [ ] N/A [ ] ISSUES: _________
  - All issues resolved or suppressed with justification: [ ] YES

### Host Testing
- [ ] All host tests pass
  ```bash
  cd /test_host
  cmake -B build -DCMAKE_C_FLAGS="-fsanitize=address,undefined"
  cmake --build build
  ./build/test_runner
  ```
- [ ] AddressSanitizer: [ ] CLEAN [ ] NO
- [ ] UBSan: [ ] CLEAN [ ] NO
- [ ] ThreadSanitizer (queue tests): [ ] CLEAN [ ] N/A [ ] NO

### Device Testing
- [ ] USB loopback test passes: [ ] YES [ ] NO
- [ ] N1MM+ sends commands, keyer responds: [ ] YES [ ] NO
- [ ] fldigi compatibility test: [ ] YES [ ] N/A [ ] NO
- [ ] Paddle priority test passes: [ ] YES [ ] NO
- [ ] 300-second stress test passes: [ ] YES [ ] NO

---

## Documentation

### Code Documentation
- [ ] Winkeyer protocol source cited in code: [ ] YES
- [ ] Sidetone formula explanation with source: [ ] YES
- [ ] Memory ordering documented in queue: [ ] YES
- [ ] Buffer overflow strategy documented: [ ] YES
- [ ] All RT path changes commented: [ ] YES

### External Documentation
- [ ] Validation report completed: `validation-winkeyer-server.md` [ ] YES
- [ ] Implementation notes in handoff: [ ] YES
- [ ] Known limitations documented: [ ] YES

---

## Merge Gates (ALL MUST BE YES)

### Architectural Compliance
- [ ] Stream as truth: All keying through `keying_stream_t`
- [ ] Lock-free only: No mutexes, atomics only
- [ ] No forbidden patterns: No callbacks, dependency injection, channels
- [ ] FAULT on timing: Violations trigger FAULT
- [ ] Static allocation: No heap in runtime
- [ ] RT-safe logging: Only RT_*() macros in RT path

### Functional Requirements
- [ ] All Winkeyer commands implemented and tested
- [ ] Status byte sent on changes
- [ ] Paddle priority working (tested)
- [ ] BREAKIN status on interrupt (tested)
- [ ] XOFF/XON flow control working (tested)
- [ ] Compatible with N1MM+ or fldigi (tested)

### Quality Requirements
- [ ] Zero compiler warnings (-Wall -Wextra -Werror)
- [ ] PVS-Studio clean
- [ ] Host tests pass (all phases)
- [ ] Device stress test passes (<80μs ceiling maintained)
- [ ] Baseline measurements documented

### Safety Requirements (Phase 5 Only)
- [ ] RT latency <80μs (measurement-verified, 100% of samples)
- [ ] 300-second stress test zero violations
- [ ] Paddle interrupt tests pass
- [ ] No FAULT triggers during stress test

---

## Sign-Off

| Role | Name | Date | Status |
|------|------|------|--------|
| Implementation | _________ | ______ | Complete / In Progress |
| Code Review | _________ | ______ | Approved / Pending |
| Architecture Review | _________ | ______ | Approved / Pending |
| Testing | _________ | ______ | Passed / Pending |
| Final Approval | _________ | ______ | Merged / Blocked |

**Blockers (if any):**
- [ ] None
- [ ] Issue: ___________________________
- [ ] Issue: ___________________________

---

**Last Updated:** ________________
**Next Phase:** ________________
