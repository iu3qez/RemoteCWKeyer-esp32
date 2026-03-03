# Iambic Timeline Markers — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Visualize iambic FSM internal events (memory window, squeeze, memory armed, Mode B bonus) on the timeline as overlay markers.

**Architecture:** Widen `stream_sample_t.flags` from `uint8_t` to `uint16_t`, add 4 new flag bits set by the iambic FSM in the RT path, detect edges in bg_task, send via WebSocket, render as configurable overlay on the OUT track.

**Tech Stack:** C (ESP-IDF), TypeScript/Svelte (frontend), Unity (host tests)

**Design doc:** `docs/plans/2026-03-03-iambic-timeline-markers-design.md`

---

### Task 1: Widen `flags` to `uint16_t` in `stream_sample_t`

**Files:**
- Modify: `components/keyer_core/include/sample.h:75-122`

**Step 1: Update flag type and add new flag constants**

In `sample.h`, change the existing `uint8_t flags` field to `uint16_t` and add the 4 new flag constants after the existing ones (after line 91):

```c
/* Iambic FSM event flags (bits 6-9) */
#define FLAG_MEM_WINDOW     0x0040  /**< Memory window is open */
#define FLAG_SQUEEZE        0x0080  /**< Both paddles pressed (squeeze) */
#define FLAG_MEM_ARMED      0x0100  /**< Memory armed this tick */
#define FLAG_MODE_B_BONUS   0x0200  /**< Mode B bonus element active */
```

In the `stream_sample_t` struct (line 107-113), change:
```c
uint8_t      flags;       /**< Edge flags and markers */
```
to:
```c
uint16_t     flags;       /**< Edge flags and markers */
```

**Step 2: Update `STREAM_SAMPLE_EMPTY` if needed**

The empty macro already sets `.flags = 0` — no change needed there.

**Step 3: Fix any code that uses `uint8_t` for flags**

Search for casts or `uint8_t` variables holding flags values. The `sample_with_edges_from` function in `sample.c` likely sets flags — check and fix.

**Step 4: Build host tests to verify no compile errors**

Run:
```bash
cd test_host && cmake -B build && cmake --build build
```
Expected: compiles cleanly. Some tests may fail if they assert on `sizeof(stream_sample_t)` — fix those.

**Step 5: Run tests**

Run:
```bash
cd test_host && ./build/test_runner
```
Expected: all tests pass.

**Step 6: Commit**

```bash
git add components/keyer_core/include/sample.h
# + any files touched for compile fixes
git commit -m "refactor(sample): widen flags to uint16_t for iambic event bits

Add FLAG_MEM_WINDOW, FLAG_SQUEEZE, FLAG_MEM_ARMED, FLAG_MODE_B_BONUS
constants. Sample grows from 6 to 8 bytes. No behavioral change yet."
```

---

### Task 2: Add `event_flags` accumulator to `iambic_processor_t`

**Files:**
- Modify: `components/keyer_iambic/include/iambic.h:166-198`
- Modify: `components/keyer_iambic/src/iambic.c` (functions: `update_gpio`, `decide_next_element`, `iambic_tick`)

**Step 1: Write failing test — iambic tick sets FLAG_SQUEEZE on squeeze**

In `test_host/test_iambic.c`, add:

```c
void test_iambic_event_flags_squeeze(void) {
    iambic_config_t config = IAMBIC_CONFIG_DEFAULT;
    config.wpm = 20;
    iambic_init(&s_iambic, &config);

    /* Press both paddles — squeeze */
    gpio_state_t gpio = gpio_from_paddles(true, true);
    esp_timer_set_time(T0);
    stream_sample_t sample = iambic_tick(&s_iambic, T0, gpio);

    TEST_ASSERT_BITS(FLAG_SQUEEZE, FLAG_SQUEEZE, sample.flags);
}
```

Register in `test_host/test_main.c`: add declaration and `RUN_TEST(test_iambic_event_flags_squeeze)`.

**Step 2: Run test to verify it fails**

```bash
cd test_host && cmake --build build && ./build/test_runner
```
Expected: FAIL — `FLAG_SQUEEZE` bit not set.

**Step 3: Implement — add `event_flags` field and set FLAG_SQUEEZE**

In `iambic.h`, add to `iambic_processor_t` (after `key_down`):

```c
    /* Event flags for timeline visualization (cleared each tick) */
    uint16_t event_flags;      /**< Accumulated iambic event flags */
```

In `iambic.c`, function `iambic_tick()`:
- At the start (after `assert`), clear: `proc->event_flags = 0;`
- At the end, before `return sample;`, merge: `sample.flags |= proc->event_flags;`

In `update_gpio()`, where `is_squeeze` is computed (around line 186):
```c
    if (is_squeeze) {
        proc->event_flags |= FLAG_SQUEEZE;
    }
```

**Step 4: Run test to verify it passes**

```bash
cd test_host && cmake --build build && ./build/test_runner
```
Expected: PASS.

**Step 5: Commit**

```bash
git commit -m "feat(iambic): add event_flags accumulator, set FLAG_SQUEEZE

iambic_processor_t gains event_flags field, cleared each tick.
FLAG_SQUEEZE set when both paddles pressed."
```

---

### Task 3: Set FLAG_MEM_WINDOW in iambic FSM

**Step 1: Write failing test**

In `test_host/test_iambic.c`:

```c
void test_iambic_event_flags_mem_window(void) {
    iambic_config_t config = IAMBIC_CONFIG_DEFAULT;
    config.wpm = 20;
    config.mem_window_start_pct = 30;
    config.mem_window_end_pct = 70;
    iambic_init(&s_iambic, &config);

    /* Start DIT */
    gpio_state_t gpio = gpio_from_paddles(true, false);
    esp_timer_set_time(T0);
    iambic_tick(&s_iambic, T0, gpio);

    /* Tick at 50% of DIT — inside memory window */
    int64_t mid = T0 + DIT_DURATION_20WPM / 2;
    esp_timer_set_time(mid);
    stream_sample_t sample = iambic_tick(&s_iambic, mid, gpio);

    TEST_ASSERT_BITS(FLAG_MEM_WINDOW, FLAG_MEM_WINDOW, sample.flags);

    /* Tick at 10% of DIT — outside memory window */
    int64_t early = T0 + DIT_DURATION_20WPM / 10;
    esp_timer_set_time(early);
    /* Reset for clean state — re-init */
    iambic_init(&s_iambic, &config);
    esp_timer_set_time(T0);
    iambic_tick(&s_iambic, T0, gpio);
    esp_timer_set_time(early);
    stream_sample_t sample2 = iambic_tick(&s_iambic, early, gpio);

    TEST_ASSERT_BITS_LOW(FLAG_MEM_WINDOW, sample2.flags);
}
```

Register in `test_main.c`.

**Step 2: Run test — expect fail**

**Step 3: Implement**

In `update_gpio()`, where `in_window` is already computed (around line 205):

```c
    if (in_window) {
        proc->event_flags |= FLAG_MEM_WINDOW;
        /* ... existing memory arming code ... */
    }
```

**Step 4: Run test — expect pass**

**Step 5: Commit**

```bash
git commit -m "feat(iambic): set FLAG_MEM_WINDOW when memory window is open"
```

---

### Task 4: Set FLAG_MEM_ARMED in iambic FSM

**Step 1: Write failing test**

In `test_host/test_iambic.c`:

```c
void test_iambic_event_flags_mem_armed(void) {
    iambic_config_t config = IAMBIC_CONFIG_DEFAULT;
    config.wpm = 20;
    config.mode = IAMBIC_MODE_B;
    config.mem_window_start_pct = 30;
    config.mem_window_end_pct = 70;
    iambic_init(&s_iambic, &config);

    /* Start DIT (dit only) */
    gpio_state_t gpio = gpio_from_paddles(true, false);
    esp_timer_set_time(T0);
    iambic_tick(&s_iambic, T0, gpio);

    /* Press DAH at 50% of DIT — inside memory window, should arm */
    gpio = gpio_from_paddles(true, true);
    int64_t mid = T0 + DIT_DURATION_20WPM / 2;
    esp_timer_set_time(mid);
    stream_sample_t sample = iambic_tick(&s_iambic, mid, gpio);

    TEST_ASSERT_BITS(FLAG_MEM_ARMED, FLAG_MEM_ARMED, sample.flags);
    TEST_ASSERT_TRUE(s_iambic.dah_memory);
}
```

Register in `test_main.c`.

**Step 2: Run test — expect fail**

**Step 3: Implement**

In `update_gpio()`, after each memory arming line, set the flag:

```c
    if (can_arm_dit && check_dit && dit_is_fresh && iambic_dit_memory_enabled(proc->config.memory_mode)) {
        proc->dit_memory = true;
        proc->event_flags |= FLAG_MEM_ARMED;
    }
    if (can_arm_dah && check_dah && dah_is_fresh && iambic_dah_memory_enabled(proc->config.memory_mode)) {
        proc->dah_memory = true;
        proc->event_flags |= FLAG_MEM_ARMED;
    }
```

**Step 4: Run test — expect pass**

**Step 5: Commit**

```bash
git commit -m "feat(iambic): set FLAG_MEM_ARMED when memory is armed"
```

---

### Task 5: Set FLAG_MODE_B_BONUS in iambic FSM

**Step 1: Write failing test**

In `test_host/test_iambic.c`:

```c
void test_iambic_event_flags_mode_b_bonus(void) {
    iambic_config_t config = IAMBIC_CONFIG_DEFAULT;
    config.wpm = 20;
    config.mode = IAMBIC_MODE_B;
    iambic_init(&s_iambic, &config);

    int64_t time = T0;

    /* Squeeze: both paddles */
    gpio_state_t gpio = gpio_from_paddles(true, true);
    esp_timer_set_time(time);
    iambic_tick(&s_iambic, time, gpio);
    /* Now sending DIT with squeeze_seen=true */

    /* Complete DIT */
    time += DIT_DURATION_20WPM + 1000;
    esp_timer_set_time(time);
    iambic_tick(&s_iambic, time, gpio);
    /* Now in GAP */

    /* Release both paddles during gap */
    gpio = gpio_from_paddles(false, false);
    time += DIT_DURATION_20WPM + 1000;
    esp_timer_set_time(time);
    stream_sample_t sample = iambic_tick(&s_iambic, time, gpio);

    /* GAP complete -> decide_next_element should trigger Mode B bonus */
    /* The bonus element should have FLAG_MODE_B_BONUS set */
    TEST_ASSERT_BITS(FLAG_MODE_B_BONUS, FLAG_MODE_B_BONUS, sample.flags);
}
```

Register in `test_main.c`.

**Step 2: Run test — expect fail**

**Step 3: Implement**

In `decide_next_element()`, in the Mode B bonus path (around line 304):

```c
    if (proc->config.mode == IAMBIC_MODE_B && proc->squeeze_seen) {
        /* ... existing code ... */
        if (!current_squeeze) {
            proc->squeeze_seen = false;
            proc->event_flags |= FLAG_MODE_B_BONUS;
            *out = iambic_element_opposite(proc->last_element);
            return out;
        }
    }
```

**Step 4: Run test — expect pass. Also run ALL tests to verify no regressions.**

```bash
cd test_host && cmake --build build && ./build/test_runner
```

**Step 5: Commit**

```bash
git commit -m "feat(iambic): set FLAG_MODE_B_BONUS on Mode B bonus element"
```

---

### Task 6: Detect iambic flag edges in bg_task and send via WebSocket

**Files:**
- Modify: `main/bg_task.c:49-50` (add prev state), `main/bg_task.c:184-228` (add edge detection)

**Step 1: Add previous-flag state tracking**

Near the existing `s_tl_prev_*` variables (line 49-50), add:

```c
static uint16_t s_tl_prev_iambic_flags = 0;
```

**Step 2: Add edge detection in the timeline event loop**

After the keying edge detection block (after line 223), before "Update previous state":

```c
                /* Check for iambic event edges */
                static const struct {
                    uint16_t mask;
                    const char *name;
                } iambic_events[] = {
                    { FLAG_MEM_WINDOW,   "mem_window" },
                    { FLAG_SQUEEZE,      "squeeze" },
                    { FLAG_MEM_ARMED,    "mem_armed" },
                    { FLAG_MODE_B_BONUS, "mode_b_bonus" },
                };
                for (int e = 0; e < 4; e++) {
                    uint16_t cur = sample.flags & iambic_events[e].mask;
                    uint16_t prev = s_tl_prev_iambic_flags & iambic_events[e].mask;
                    if (cur != prev) {
                        snprintf(json, sizeof(json),
                            "{\"ts\":%lld,\"event\":\"%s\",\"state\":%d}",
                            (long long)(now_us / 1000),
                            iambic_events[e].name,
                            cur ? 1 : 0);
                        webui_timeline_push("iambic", json);
                    }
                }
```

Update the previous state tracking (line 226-227), add:

```c
                s_tl_prev_iambic_flags = sample.flags & (FLAG_MEM_WINDOW | FLAG_SQUEEZE | FLAG_MEM_ARMED | FLAG_MODE_B_BONUS);
```

**Step 3: Verify json buffer is large enough**

The existing `char json[80]` is tight. The longest iambic message is:
`{"ts":1234567890123,"event":"mode_b_bonus","state":1}` = 53 chars. Fits in 80.

**Step 4: Build ESP-IDF target to verify compile**

```bash
source /home/sf/esp/esp-idf/export.sh && idf.py build
```
Expected: compiles cleanly.

**Step 5: Commit**

```bash
git commit -m "feat(bg_task): detect iambic flag edges and broadcast via WebSocket

New 'iambic' event type with mem_window, squeeze, mem_armed,
mode_b_bonus sub-events sent on flag transitions."
```

---

### Task 7: Frontend — add iambic WS event handling in api.ts

**Files:**
- Modify: `components/keyer_webui/frontend/src/lib/api.ts:210-300`

**Step 1: Add WSMessageIambic interface**

After `WSMessageGap` (line 283-287):

```typescript
interface WSMessageIambic {
  type: 'iambic';
  ts: number;
  event: 'mem_window' | 'squeeze' | 'mem_armed' | 'mode_b_bonus';
  state: number;
}
```

**Step 2: Add to WSMessage union type**

```typescript
type WSMessage = WSMessageDecoded | WSMessageWord | WSMessagePattern | WSMessagePaddle | WSMessageKeying | WSMessageGap | WSMessageIambic;
```

**Step 3: Add callback to WSCallbacks**

```typescript
  onIambic?: (ts: number, event: string, state: number) => void;
```

**Step 4: Add case in handleMessage switch**

After the `case 'gap':` block:

```typescript
      case 'iambic':
        this.wsCallbacks.onIambic?.(ts, msg.event, msg.state);
        break;
```

**Step 5: Commit**

```bash
git commit -m "feat(webui): add iambic event type to WebSocket API client"
```

---

### Task 8: Frontend — add iambic overlay rendering to Timeline.svelte

**Files:**
- Modify: `components/keyer_webui/frontend/src/pages/Timeline.svelte`

**Step 1: Add iambic event buffer and toggle state**

In the `<script>` section, after the existing event buffer (line 34-35):

```typescript
  // Iambic event buffer
  interface IambicEvent {
    ts: number;
    event: 'mem_window' | 'squeeze' | 'mem_armed' | 'mode_b_bonus';
    state: number;
  }
  let iambicEvents = $state<IambicEvent[]>([]);
  let showIambicOverlay = $state(
    typeof localStorage !== 'undefined'
      ? localStorage.getItem('timeline_iambic_overlay') !== 'false'
      : true
  );
```

**Step 2: Add pushIambicEvent function**

After `pushEvent`:

```typescript
  function pushIambicEvent(event: IambicEvent) {
    if (paused) return;
    lastEventTime = Date.now();
    iambicEvents.push(event);
    if (iambicEvents.length > MAX_EVENTS) {
      iambicEvents = iambicEvents.slice(-MAX_EVENTS);
    }
    const cutoff = Date.now() - (duration * 2000);
    iambicEvents = iambicEvents.filter(e => e.ts > cutoff);
  }
```

**Step 3: Wire onIambic callback in onMount**

In the `api.connect()` call, add:

```typescript
      onIambic: (ts, event, state) => {
        pushIambicEvent({ ts, event: event as IambicEvent['event'], state });
      },
```

**Step 4: Add overlay drawing in drawTimeline**

After the "Draw ongoing pulses" block (after line 163) and before the "Draw current time marker" block:

```typescript
    // Draw iambic overlay (if enabled)
    if (showIambicOverlay) {
      const outTrackY = 2 * trackHeight;  // OUT is track index 2

      for (const evt of iambicEvents) {
        const x = (evt.ts - windowStart) * pxPerMs;
        if (x < -50 || x > width + 50) continue;

        if (evt.event === 'mem_window') {
          // Vertical line at window open/close edge
          ctx.strokeStyle = evt.state ? 'rgba(255, 200, 50, 0.5)' : 'rgba(255, 200, 50, 0.3)';
          ctx.lineWidth = 1;
          ctx.setLineDash([3, 3]);
          ctx.beginPath();
          ctx.moveTo(x, outTrackY);
          ctx.lineTo(x, outTrackY + trackHeight);
          ctx.stroke();
          ctx.setLineDash([]);
        } else if (evt.event === 'squeeze' && evt.state === 1) {
          // Small diamond marker
          ctx.fillStyle = 'rgba(255, 100, 255, 0.7)';
          ctx.beginPath();
          ctx.moveTo(x, outTrackY + 2);
          ctx.lineTo(x + 4, outTrackY + 6);
          ctx.lineTo(x, outTrackY + 10);
          ctx.lineTo(x - 4, outTrackY + 6);
          ctx.closePath();
          ctx.fill();
        } else if (evt.event === 'mem_armed' && evt.state === 1) {
          // Small upward triangle
          ctx.fillStyle = 'rgba(50, 255, 150, 0.7)';
          ctx.beginPath();
          ctx.moveTo(x, outTrackY + trackHeight - 10);
          ctx.lineTo(x + 4, outTrackY + trackHeight - 2);
          ctx.lineTo(x - 4, outTrackY + trackHeight - 2);
          ctx.closePath();
          ctx.fill();
        } else if (evt.event === 'mode_b_bonus' && evt.state === 1) {
          // "B" label
          ctx.fillStyle = 'rgba(255, 150, 50, 0.8)';
          ctx.font = 'bold 10px "JetBrains Mono", monospace';
          ctx.fillText('B', x - 3, outTrackY + 14);
        }
      }
    }
```

**NOTE: This visual treatment is EXPERIMENTAL. Shapes, colors, and sizes will need iteration with real keying data. The rendering code above is a starting point to get something on screen.**

**Step 5: Add toggle button in controls panel**

In the controls `<div class="control-buttons">` section (after the CLEAR button, line 367):

```svelte
        <button class="action-btn" class:active={showIambicOverlay}
                onclick={() => {
                  showIambicOverlay = !showIambicOverlay;
                  localStorage.setItem('timeline_iambic_overlay', String(showIambicOverlay));
                }}>
          <span class="btn-icon">&#9881;</span>
          <span>{showIambicOverlay ? 'FSM ON' : 'FSM OFF'}</span>
        </button>
```

**Step 6: Add iambic events to clearBuffer**

In `clearBuffer()`:

```typescript
    iambicEvents = [];
```

**Step 7: Add iambic legend items**

In the track legend section, add after the existing `{#each}` block:

```svelte
      {#if showIambicOverlay}
        <div class="legend-item">
          <span class="legend-color" style="background: rgba(255, 200, 50, 0.5)"></span>
          <span class="legend-name">MEM</span>
          <span class="legend-desc">Memory window edges</span>
        </div>
      {/if}
```

**Step 8: Build frontend**

```bash
cd components/keyer_webui/frontend && npm run build
```
Expected: compiles cleanly.

**Step 9: Commit**

```bash
git commit -m "feat(timeline): add iambic event overlay with toggle

Shows memory window, squeeze, memory armed, Mode B bonus as
overlay markers on OUT track. Toggle saved to localStorage.
Visual treatment is experimental and will be iterated."
```

---

### Task 9: Full integration test and cleanup

**Step 1: Run all host tests**

```bash
cd test_host && cmake -B build -DCMAKE_C_FLAGS="-fsanitize=address,undefined" && cmake --build build && ./build/test_runner
```
Expected: all tests pass, no sanitizer warnings.

**Step 2: Full ESP-IDF build**

```bash
source /home/sf/esp/esp-idf/export.sh && idf.py build
```
Expected: compiles cleanly.

**Step 3: Review all changes**

```bash
git log --oneline main..HEAD
git diff main..HEAD --stat
```

Verify: no unintended changes, no debug prints left.

**Step 4: Final commit if any fixups needed**

---

## Dependency Graph

```
Task 1 (widen flags)
  └─> Task 2 (event_flags + FLAG_SQUEEZE)
       ├─> Task 3 (FLAG_MEM_WINDOW)
       ├─> Task 4 (FLAG_MEM_ARMED)
       └─> Task 5 (FLAG_MODE_B_BONUS)
            └─> Task 6 (bg_task edge detection)
                 └─> Task 7 (frontend api.ts)
                      └─> Task 8 (frontend rendering)
                           └─> Task 9 (integration test)
```

Tasks 3, 4, 5 can be done in parallel after Task 2.
