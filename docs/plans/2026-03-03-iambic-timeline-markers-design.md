# Iambic Event Markers on Timeline

**Date**: 2026-03-03
**Status**: Approved

## Problem

The timeline visualizer shows paddle inputs (DIT/DAH) and keying output (OUT) but provides no visibility into the iambic FSM's internal decisions: when the memory window opens/closes, when squeeze is detected, when memory gets armed, and when Mode B bonus elements fire. These are critical for understanding and debugging iambic behavior.

## Design Decisions

1. **No logic duplication** — the iambic FSM is the sole source of truth. Events are annotated as flag bits in `stream_sample_t` by the RT path. The consumer reads them directly.
2. **Flags field widened to uint16_t** — ESP32 is 32-bit with ample RAM; the sample grows from 6 to 8 bytes. This gives 10 free bits for current and future use.
3. **Overlay on existing tracks** — no new track row. Markers are drawn on top of the OUT track, toggled via frontend switch saved in localStorage.
4. **Visual treatment requires iteration** — initial direction is vertical lines for event edges + subtle color shift on OUT band during memory window. This must be tested and refined empirically.

## Stream Sample Changes

### Current `stream_sample_t` (6 bytes packed)

```c
typedef struct __attribute__((packed)) {
    gpio_state_t gpio;        // 1 byte
    uint8_t      local_key;   // 1 byte
    uint8_t      audio_level; // 1 byte
    uint8_t      flags;       // 1 byte  <-- becomes uint16_t
    uint16_t     config_gen;  // 2 bytes
} stream_sample_t;
```

### New `stream_sample_t` (8 bytes packed)

```c
typedef struct __attribute__((packed)) {
    gpio_state_t gpio;        // 1 byte
    uint8_t      local_key;   // 1 byte
    uint8_t      audio_level; // 1 byte
    uint16_t     flags;       // 2 bytes  <-- widened
    uint16_t     config_gen;  // 2 bytes
    uint8_t      _pad;        // 1 byte (explicit padding to 8)
} stream_sample_t;
```

### New Flag Bits

Existing flags (bits 0-5) unchanged:

| Bit | Mask   | Name                |
|-----|--------|---------------------|
| 0   | 0x0001 | FLAG_GPIO_EDGE      |
| 1   | 0x0002 | FLAG_CONFIG_CHANGE  |
| 2   | 0x0004 | FLAG_TX_START       |
| 3   | 0x0008 | FLAG_RX_START       |
| 4   | 0x0010 | FLAG_SILENCE        |
| 5   | 0x0020 | FLAG_LOCAL_EDGE     |

New flags (bits 6-9):

| Bit | Mask   | Name                | Semantics                                      |
|-----|--------|---------------------|-------------------------------------------------|
| 6   | 0x0040 | FLAG_MEM_WINDOW     | Memory window is open (set every tick while in window) |
| 7   | 0x0080 | FLAG_SQUEEZE        | Both paddles pressed simultaneously             |
| 8   | 0x0100 | FLAG_MEM_ARMED      | Memory was armed this tick (dit or dah)         |
| 9   | 0x0200 | FLAG_MODE_B_BONUS   | Mode B bonus element is being sent              |

Bits 10-15 reserved for future use.

## Where Flags Are Set (RT Path)

All flag-setting happens in `iambic.c` functions that already execute in the RT tick:

- **FLAG_MEM_WINDOW**: set in `update_gpio()` where `is_in_memory_window()` is already called. When `in_window` is true, set the bit on the sample.
- **FLAG_SQUEEZE**: set in `update_gpio()` where `is_squeeze` is already computed. When both paddles pressed, set the bit.
- **FLAG_MEM_ARMED**: set in `update_gpio()` immediately after `proc->dit_memory = true` or `proc->dah_memory = true`.
- **FLAG_MODE_B_BONUS**: set in `decide_next_element()` when the Mode B bonus path is taken (squeeze_seen && !current_squeeze).

**Cost**: each flag is a single bitwise OR on a local variable. Well under 1µs total.

**Integration point**: the iambic tick currently operates on `iambic_processor_t` which doesn't touch the sample directly. The RT task (`rt_task.c`) calls `iambic_tick()` and then builds the sample. The flags need to flow from the iambic processor to the sample. Options:
- Add a `uint16_t event_flags` field to `iambic_processor_t` that accumulates per-tick, cleared after sample is built.
- Or return flags from `iambic_tick()`.

The accumulator field approach is simpler and doesn't change the API signature.

## WebSocket Protocol

New event type `iambic` broadcast by bg_task when edge detected on iambic flags:

```json
{"type":"iambic","ts":123456,"event":"mem_window","state":1}
{"type":"iambic","ts":123456,"event":"squeeze","state":0}
{"type":"iambic","ts":123456,"event":"mem_armed","state":1}
{"type":"iambic","ts":123456,"event":"mode_b_bonus","state":1}
```

bg_task detects edges on the 4 new flag bits the same way it detects edges on GPIO and keying today.

## Frontend Rendering

### Toggle
- Checkbox/switch in the timeline controls panel: "Show iambic events"
- State persisted in `localStorage` under key `timeline_iambic_overlay`
- When off, iambic WS events are received but not rendered

### Visual Treatment (EXPERIMENTAL — needs iteration)

Initial approach to test:

1. **Memory window**: pair of thin vertical lines (light color, e.g. `rgba(255,255,255,0.3)`) at open/close edges. Optionally: subtle color tint on OUT band while window is active (e.g. slight green/amber shift).
2. **Squeeze**: vertical dashed line or small marker icon at the moment both paddles are pressed.
3. **Memory armed**: small triangle or dot marker on the OUT track at the moment memory is armed.
4. **Mode B bonus**: different tint/outline on the OUT pulse for the bonus element.

**This visual design is provisional.** The final rendering must be tested with real keying at various WPM to find what's readable without being cluttered. Multiple iterations expected.

## Test Impact

- Host tests that use `stream_sample_t` need updating for the new size (8 bytes)
- Existing flag checks use `uint8_t` masks — must be updated to `uint16_t`
- New test: verify iambic flag bits are correctly set during known keying sequences
- Silence compression logic uses `sample_has_change_from()` which compares gpio, local_key, audio_level — flags are not compared, so no impact on RLE behavior

## Migration / Compatibility

- No persistent storage of samples — stream is in-memory only. No migration needed.
- WebSocket protocol is additive (new event type). Old frontends ignore unknown types.
