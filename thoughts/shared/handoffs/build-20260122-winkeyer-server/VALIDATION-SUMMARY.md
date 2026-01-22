# Validation Summary: Winkeyer Server Implementation Plan

**Date:** 2026-01-22
**Validated Against:** ARCHITECTURE.md, CLAUDE.md, Winkeyer Protocol Specification
**Overall Verdict:** ✓ PASS (with critical Phase 5 conditions)

---

## Quick Status

| Phase | Risk | Verdict | Notes |
|-------|------|---------|-------|
| Phase 1: Morse Table & Queue | LOW | ✓ PASS | Straightforward, well-scoped |
| Phase 2: Protocol Parser | MEDIUM | ✓ PASS | State machine is clean, needs protocol verification |
| Phase 3: Winkeyer Controller | MEDIUM | ✓ PASS | Config integration solid, buffer overflow needs spec |
| Phase 4: USB CDC Integration | LOW | ✓ PASS | Pattern proven in codebase |
| Phase 5: RT Task Integration | **CRITICAL** | ⚠ CONDITIONAL | Latency verification MANDATORY before merge |
| Phase 6: Status & Flow Control | MEDIUM | ✓ PASS | Needs overflow handling clarification |

---

## Critical Requirements for Merge

### Before Starting Implementation

- [ ] Verify sidetone frequency formula against official Winkeyer v3 protocol documentation
- [ ] Decide buffer overflow strategy: FAULT vs drop vs disconnect?

### Before Phase 5 Implementation

- [ ] Measure baseline RT tick duration WITHOUT winkeyer (document result)
- [ ] Establish acceptance criteria: max_tick < 50μs baseline, < 75μs with winkeyer

### Before Phase 5 Merge

- [ ] Verify RT tick remains <80μs in all scenarios (100% requirement, not 99%)
- [ ] Complete 300-second stress test with zero violations
- [ ] Run PVS-Studio: must be zero warnings
- [ ] Compiler warnings: must be zero (with strict -Wall -Wextra -Werror)

---

## Key Findings

### Architectural Alignment: EXCELLENT

✓ Stream as truth: Correct (morse queue feeds iambic)
✓ Lock-free design: Correct (SPSC queue, atomic indices)
✓ Producer/consumer isolation: Correct (Core 1/Core 0 separation)
✓ FAULT semantics: Plan includes FAULT on buffer overflow
✓ Static allocation: All buffers static, zero heap in runtime

### Protocol Compliance: GOOD (with verification needed)

✓ Commands match spec (0x00-0x0F implemented)
✓ Status byte design correct (XOFF/BREAKIN/BUSY flags)
? Sidetone frequency formula: Formula `4000 / (code + 1)` MUST be verified
? XOFF/XON flow control: Implementation mechanism needs clarification

### RT Safety: UNCERTAIN (measurement-dependent)

⚠ Plan assumes <20μs overhead (15μs queue pop + 5μs element processing)
⚠ 80μs budget is tight; any underestimate breaks timing
⚠ No baseline measurement data provided
⚠ Stress test parameters incomplete (duration, frequency, acceptance criteria)

### Lock-Free Correctness: GOOD (needs code review)

✓ Queue design is safe (SPSC, power-of-2 size, static allocation)
? Memory ordering not specified (acq_rel for producer, acquire for consumer)
? Queue full handling strategy unclear (drop? retry? FAULT?)
? Inter-element gap mechanism unclear (explicit queue element? implicit state?)

---

## Implementation Priorities

### Priority 1: Verification (Before any code)

1. **Sidetone Formula Verification**
   - Find official Winkeyer v3 protocol documentation (PDF, website, or emulation reference)
   - Verify: Does `4000 / (code + 1)` correctly map parameter 0-127 to 400-800 Hz?
   - If wrong: Find correct formula, cite source in code comments

2. **Baseline Measurements**
   - Instrument rt_task.c with timing macros
   - Measure max RT tick duration WITHOUT winkeyer code
   - Document: max_tick_baseline = ? μs
   - This gates Phase 5 implementation

### Priority 2: Phases 1-4 (Low risk, high confidence)

3. **Phase 1: Morse Queue**
   - Implement with correct memory ordering (acq_rel/acquire)
   - Unit tests: push/pop, full queue, wraparound
   - Use bitwise AND for index wrapping: `(idx + 1) & 63`

4. **Phase 2: Protocol Parser**
   - Comprehensive state machine tests
   - Invalid command handling (must not crash)
   - Cite protocol documentation in code

5. **Phase 3: Controller**
   - Config updates via atomic macros (verified pattern)
   - Buffer overflow detection
   - Callback pattern for parser

6. **Phase 4: USB Integration**
   - CDC2 registration (pattern proven)
   - RX callback with timing measurement
   - Response queuing

### Priority 3: Phase 5 (HIGH RISK, measurement-gated)

7. **Phase 5: RT Integration**
   - Only start AFTER baseline measurements obtained
   - Implement timing instrumentation first
   - Stress test: 300 seconds continuous, zero violations
   - Paddle interrupt testing (hardware or simulation)

### Priority 4: Phase 6 (Medium risk, clarification-dependent)

8. **Phase 6: Status Reporting**
   - XOFF/XON mechanism clarified
   - Buffer overflow strategy implemented
   - Status byte generation tested

---

## High-Risk Areas (Focus for Code Review)

### 1. Phase 5 RT Latency (CRITICAL)

**Risk:** Latency exceeds 100μs ceiling, corrupts CW timing
**What Could Go Wrong:**
- Queue pop is faster than assumed (unlikely, but measure it)
- Element processing is slower than assumed (more likely)
- Context switch surprise (verify zero context switches in hot path)
- Paddle check overhead higher than expected

**Mitigation:**
- Implement instrumentation FIRST (before any morse queue code)
- Measure baseline: rt_task without winkeyer code
- Measure with empty queue: minimal overhead
- Measure with full queue: worst case
- Measure with paddle interrupt: state transition cost
- Stress test proves zero violations

**Gating Criterion:** max_tick < 80μs on device, 100% of samples in 300-second stress test

### 2. Sidetone Frequency Compatibility (MEDIUM)

**Risk:** N1MM+ incompatible due to wrong frequency
**What Could Go Wrong:**
- Formula `4000 / (code + 1)` is not Winkeyer standard
- Parameter mapping is inverted (code=0 gives wrong frequency)
- Range is wrong (400-800 Hz vs actual Winkeyer range)

**Mitigation:**
- Find authoritative source (K1EL official docs or accepted emulation)
- Cite source in code comments
- Unit test: map known code values to expected frequencies
- Integration test: verify N1MM+ gets recognizable sidetone

**Gating Criterion:** Protocol source cited, test pass with known reference values

### 3. Buffer Overflow Strategy (MEDIUM)

**Risk:** Host sends fast, RT task can't keep up, queue/text buffer overflows
**What Could Go Wrong:**
- XOFF/XON not implemented correctly
- Host ignores XOFF and continues sending
- Queue overflows silently, dropping characters
- Text buffer exhaustion causes silent drops or crashes

**Mitigation:**
- Clear specification: what happens on overflow? (FAULT vs drop vs disconnect)
- Unit test: overflow behavior with fast producer
- Stress test: host sends maximum rate, verify XOFF sent at 2/3 full
- Acceptance: zero silent drops, clear status indication to host

**Gating Criterion:** Overflow handling specified and tested

### 4. Memory Ordering in Queue (MEDIUM)

**Risk:** Data race in lock-free queue on multi-core system
**What Could Go Wrong:**
- Wrong memory order allows consumer to see garbage data
- Queue indices corrupt under load
- Race condition manifests only under stress (hard to debug)

**Mitigation:**
- Implement with correct memory_order_acq_rel (producer) and memory_order_acquire (consumer)
- Run with ThreadSanitizer (TSAN) on x86 host
- Stress test: 10 million push/pop cycles, verify zero corruption

**Gating Criterion:** No TSAN warnings, stress test passes

---

## Testing Checkpoints

### Host Testing (Phases 1-3)
```bash
cd /home/sf/src/RustRemoteCWKeyer/test_host
cmake -B build -DCMAKE_C_FLAGS="-fsanitize=address,undefined"
cmake --build build
./build/test_runner
```
Required to pass: All tests, with AddressSanitizer and UBSan enabled

### Device Baseline Measurement (Gate for Phase 5)
```
Without winkeyer code:
- max RT tick: ? μs
- measure 10,000 iterations on device
- document in validation report
```

### Device Stress Testing (Phase 5 merge gate)
```
With winkeyer active:
- Duration: 300 seconds
- Queue states: idle, filling, draining, full, empty cycles
- Paddle interrupts: 10 Hz random
- Success: zero violations of 80μs latency ceiling
- Logs: proof of XOFF/XON flow control
```

---

## Design Notes for Implementation

### Morse Queue Memory Ordering

```c
// Producer (Core 1, Winkeyer USB callback):
size_t idx = atomic_fetch_add_explicit(&q->write_idx, 1, memory_order_acq_rel);
q->buffer[idx & (MORSE_QUEUE_SIZE - 1)] = elem;

// Consumer (Core 0, RT task):
size_t head = atomic_load_explicit(&q->write_idx, memory_order_acquire);
if (q->read_idx != head) {
    morse_element_t elem = q->buffer[q->read_idx & (MORSE_QUEUE_SIZE - 1)];
    q->read_idx++;
    // Process elem
}
```

### Inter-Element Gap Handling

Recommended: Use explicit queue elements
```c
typedef enum {
    MORSE_ELEMENT_DIT,
    MORSE_ELEMENT_DAH,
    MORSE_ELEMENT_GAP,        // 1-dit gap
    MORSE_ELEMENT_CHAR_SPACE,  // 3-dit gap
    MORSE_ELEMENT_WORD_SPACE,  // 7-dit gap
} morse_element_type_t;
```

RT task handles all durations uniformly in `winkeyer_consume_element()`.

### Buffer Overflow Strategy (Recommended)

```c
// In winkeyer_pump_text():
if (text_buf_fill >= MORSE_TEXT_BUFFER_SIZE - 1 && xoff_not_sent) {
    // Send XOFF status
    send_status_byte(WK_STATUS_BASE | WK_STATUS_XOFF);
    xoff_sent = true;
}

if (text_buf_fill >= MORSE_TEXT_BUFFER_SIZE - 1 && text_buf_fill >= MORSE_TEXT_BUFFER_SIZE) {
    // Host ignored XOFF too long - this is a protocol violation
    fault_set(&g_fault_state, FAULT_WINKEYER_HOST_BUFFER_OVERFLOW, text_buf_fill);
    return;
}
```

---

## Files Modified/Created

### New Files (Phase 1-6)
- `components/keyer_winkeyer/CMakeLists.txt`
- `components/keyer_winkeyer/include/winkeyer.h`
- `components/keyer_winkeyer/include/winkeyer_protocol.h`
- `components/keyer_winkeyer/include/winkeyer_morse.h`
- `components/keyer_winkeyer/include/morse_queue.h`
- `components/keyer_winkeyer/src/winkeyer.c`
- `components/keyer_winkeyer/src/winkeyer_parser.c`
- `components/keyer_winkeyer/src/winkeyer_morse.c`
- `components/keyer_winkeyer/src/morse_queue.c`
- `test_host/test_morse.c`
- `test_host/test_winkeyer_parser.c`

### Modified Files
- `components/keyer_usb/src/usb_cdc.c` (CDC2 initialization)
- `components/keyer_usb/src/usb_winkeyer.c` (replace stub)
- `components/keyer_usb/include/usb_winkeyer.h` (new API)
- `main/rt_task.c` (morse queue consumption + timing instrumentation)
- `test_host/CMakeLists.txt` (add winkeyer tests)

---

## Success Criteria Checklist

### Architecture
- [x] All keying through stream
- [x] No shared state except stream
- [x] Lock-free atomics only
- [x] No mutexes, callbacks, channels
- [x] FAULT on latency violation
- [x] RT-safe logging only (RT_*() macros)

### Functionality
- [ ] All Winkeyer commands 0x00-0x0F implemented
- [ ] Status byte sent on changes
- [ ] Paddle priority (breaks into Winkeyer transmission)
- [ ] BREAKIN status sent on paddle interrupt
- [ ] XOFF/XON flow control working
- [ ] Compatible with N1MM+ and fldigi

### Safety
- [ ] Zero compiler warnings (-Wall -Wextra -Werror)
- [ ] PVS-Studio clean
- [ ] RT latency <80μs in all scenarios (measurement-verified)
- [ ] 300-second stress test passes
- [ ] Paddle interrupt tests pass (hardware/simulated)

### Quality
- [ ] Host tests pass with sanitizers
- [ ] Protocol source cited
- [ ] Memory ordering documented in code
- [ ] Overflow behavior specified and tested
- [ ] Integration test with N1MM+ or fldigi

---

## Next Steps

**Immediate (Before implementation starts):**
1. Find and read official Winkeyer v3 protocol documentation
2. Verify sidetone frequency formula: cite source in code
3. Measure baseline RT tick duration on device (gate for Phase 5)

**Phase 1-2 (Low risk, proceed with confidence):**
4. Implement morse queue with correct memory ordering
5. Implement protocol parser with comprehensive tests
6. Create host test suite (no hardware needed)

**Before Phase 5 (High risk, gated by measurement):**
7. Instrument rt_task.c with timing macros
8. Verify baseline measurement meets acceptance criteria
9. Build stress test harness on device

**Phase 5 (Critical, measurement-verified):**
10. Implement morse queue consumption in RT task
11. Implement paddle priority arbitration
12. Verify <80μs latency ceiling with stress test
13. Code review focus on memory ordering and latency

---

## Document References

- Full validation report: `validation-winkeyer-server.md`
- Implementation plan: `PLAN-winkeyer-server.md`
- Specification: `2026-01-22-winkeyer-server-spec.md`
- Architecture constraints: `ARCHITECTURE.md`
- Code style guide: `CLAUDE.md`

---

Generated: 2026-01-22
Validated by: Validate Agent (Haiku 4.5)
