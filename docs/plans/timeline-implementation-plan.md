# Timeline Implementation Plan

## Overview

Real-time visualization of CW keying events: paddle inputs, keyer output, FSM states, decoded characters.

**Architecture**: Best-effort consumer on Core 1 reads from `keying_stream_t`, formats events as JSON, broadcasts via WebSocket to browser Timeline.svelte component.

```
keying_stream_t (RT, Core 0)
  → timeline_consumer (best-effort, Core 1, bg_task)
  → webui_timeline_push()
  → ws_broadcast_timeline()
  → WebSocket /ws
  → Timeline.svelte canvas
```

## Current State

### Already Implemented

**Backend:**
- `ws_broadcast_timeline(event_type, json_data)` in `ws_server.c`
- `webui_timeline_push(event_type, json_data)` in `http_server.c`
- WebSocket server with multi-client support (max 8)
- `/api/timeline/config` endpoint (returns WPM)

**Frontend:**
- `Timeline.svelte` skeleton with demo canvas (4 tracks)
- `api.ts` WebSocket client with handlers for `paddle`, `keying`, `gap` messages
- TypeScript types: `WSMessagePaddle`, `WSMessageKeying`, `WSMessageGap`

### Missing

**Backend:**
- Timeline consumer (`best_effort_consumer_t`) in `bg_task.c`
- Event formatting logic (sample → JSON)

**Frontend:**
- Live WebSocket connection in Timeline.svelte
- Real-time canvas rendering
- Event buffering and scroll

---

## Implementation Plan

### Phase 1: Backend - Timeline Consumer

**Goal**: Push paddle/keying events to WebSocket when they change.

#### 1.1 Add Timeline Consumer to bg_task.c

```c
// In bg_task.c static section
static best_effort_consumer_t s_timeline_consumer;
static bool s_timeline_initialized = false;

// Track previous state for edge detection
static gpio_state_t s_prev_gpio = GPIO_IDLE;
static uint8_t s_prev_local_key = 0;
```

#### 1.2 Initialize in bg_task

```c
void bg_task(void *arg) {
    // Initialize timeline consumer (skip_threshold=0 means never auto-skip)
    best_effort_consumer_init(&s_timeline_consumer, &g_keying_stream, 0);
    s_timeline_initialized = true;
    // ...
}
```

#### 1.3 Process Timeline Events in Loop

```c
// In bg_task main loop, after decoder processing

// Process timeline events (only if WebSocket clients connected)
if (ws_get_client_count() > 0) {
    stream_sample_t sample;
    while (best_effort_consumer_tick(&s_timeline_consumer, &sample)) {
        // Skip silence markers
        if (sample_is_silence(&sample)) {
            continue;
        }

        // Check for paddle edge
        if (sample.gpio.bits != s_prev_gpio.bits) {
            // DIT paddle changed
            if (gpio_dit(sample.gpio) != gpio_dit(s_prev_gpio)) {
                char json[64];
                snprintf(json, sizeof(json),
                    "{\"ts\":%lld,\"paddle\":0,\"state\":%d}",
                    (long long)now_us, gpio_dit(sample.gpio) ? 1 : 0);
                webui_timeline_push("paddle", json);
            }
            // DAH paddle changed
            if (gpio_dah(sample.gpio) != gpio_dah(s_prev_gpio)) {
                char json[64];
                snprintf(json, sizeof(json),
                    "{\"ts\":%lld,\"paddle\":1,\"state\":%d}",
                    (long long)now_us, gpio_dah(sample.gpio) ? 1 : 0);
                webui_timeline_push("paddle", json);
            }
            s_prev_gpio = sample.gpio;
        }

        // Check for keying output edge
        if (sample.local_key != s_prev_local_key) {
            char json[64];
            snprintf(json, sizeof(json),
                "{\"ts\":%lld,\"element\":0,\"state\":%d}",
                (long long)now_us, sample.local_key ? 1 : 0);
            webui_timeline_push("keying", json);
            s_prev_local_key = sample.local_key;
        }
    }
}
```

#### 1.4 Dependencies

Add to `main/CMakeLists.txt`:
```cmake
REQUIRES ... keyer_webui
```

Expose `ws_get_client_count()` in webui.h:
```c
int webui_get_ws_client_count(void);
```

---

### Phase 2: Frontend - Live Timeline

**Goal**: Display real-time paddle and keying events on canvas.

#### 2.1 Timeline.svelte State

```typescript
// Connection state
let connected = $state(false);
let error = $state<string | null>(null);

// Timeline configuration
let config = $state<TimelineConfig | null>(null);
let duration = $state(3.0);  // seconds visible
let zoom = $state(1);

// Event buffer (circular, time-windowed)
interface TimelineEvent {
  type: 'paddle' | 'keying' | 'gap';
  ts: number;
  track: number;  // 0=DIT, 1=DAH, 2=OUT, 3=LOGIC
  state: number;  // 1=on, 0=off
}
let events = $state<TimelineEvent[]>([]);
const MAX_EVENTS = 1000;

// Canvas reference
let canvasRef: HTMLCanvasElement;
let animationFrame: number;
```

#### 2.2 WebSocket Connection

```typescript
onMount(async () => {
  // Load config
  try {
    config = await api.getTimelineConfig();
  } catch (e) {
    error = 'Failed to load config';
  }

  // Connect WebSocket
  api.connect({
    onPaddle: (ts, paddle, state) => {
      pushEvent({
        type: 'paddle',
        ts,
        track: paddle,  // 0=DIT, 1=DAH
        state
      });
    },
    onKeying: (ts, element, state) => {
      pushEvent({
        type: 'keying',
        ts,
        track: 2,  // OUT track
        state
      });
    },
    onGap: (ts, gapType) => {
      // Optional: mark gaps on LOGIC track
    },
    onConnect: () => {
      connected = true;
      error = null;
      startRenderLoop();
    },
    onDisconnect: () => {
      connected = false;
      error = 'Disconnected. Reconnecting...';
    }
  });
});

onDestroy(() => {
  api.disconnect();
  if (animationFrame) cancelAnimationFrame(animationFrame);
});
```

#### 2.3 Event Buffer Management

```typescript
function pushEvent(event: TimelineEvent) {
  events.push(event);

  // Prune old events (keep last MAX_EVENTS)
  if (events.length > MAX_EVENTS) {
    events = events.slice(-MAX_EVENTS);
  }

  // Alternatively: prune by time window
  const cutoff = Date.now() - (duration * 1000 * 2);
  events = events.filter(e => e.ts > cutoff);
}
```

#### 2.4 Canvas Rendering Loop

```typescript
function startRenderLoop() {
  const render = () => {
    drawTimeline();
    animationFrame = requestAnimationFrame(render);
  };
  render();
}

function drawTimeline() {
  const ctx = canvasRef.getContext('2d');
  if (!ctx) return;

  const width = canvasRef.width;
  const height = canvasRef.height;
  const trackHeight = height / 4;
  const now = Date.now();
  const windowStart = now - (duration * 1000);
  const pxPerMs = width / (duration * 1000);

  // Clear
  ctx.fillStyle = '#0d1117';
  ctx.fillRect(0, 0, width, height);

  // Draw grid (WPM calibrated)
  drawGrid(ctx, width, height, trackHeight);

  // Draw track labels
  tracks.forEach((track, i) => {
    ctx.fillStyle = '#006622';
    ctx.font = '10px monospace';
    ctx.fillText(track.name, 5, i * trackHeight + 12);
  });

  // Draw events as pulses
  let trackStates = [false, false, false, false];
  let trackStartX = [0, 0, 0, 0];

  // Sort events by timestamp
  const sortedEvents = [...events].sort((a, b) => a.ts - b.ts);

  for (const event of sortedEvents) {
    const x = (event.ts - windowStart) * pxPerMs;
    if (x < 0 || x > width) continue;

    const track = event.track;
    const y = track * trackHeight;

    if (event.state === 1) {
      // Start of pulse
      trackStates[track] = true;
      trackStartX[track] = x;
    } else if (trackStates[track]) {
      // End of pulse - draw rectangle
      ctx.fillStyle = tracks[track].color;
      const pulseWidth = x - trackStartX[track];
      ctx.fillRect(trackStartX[track], y + 15, pulseWidth, trackHeight - 30);
      trackStates[track] = false;
    }
  }

  // Draw ongoing pulses to current time
  const nowX = width;
  for (let i = 0; i < 4; i++) {
    if (trackStates[i]) {
      ctx.fillStyle = tracks[i].color;
      ctx.fillRect(trackStartX[i], i * trackHeight + 15,
                   nowX - trackStartX[i], trackHeight - 30);
    }
  }
}
```

#### 2.5 Grid Drawing (WPM calibrated)

```typescript
function drawGrid(ctx: CanvasRenderingContext2D,
                  width: number, height: number, trackHeight: number) {
  const wpm = config?.wpm || 20;
  const ditMs = 1200 / wpm;  // ITU timing
  const dahMs = ditMs * 3;

  ctx.strokeStyle = '#004411';
  ctx.lineWidth = 1;

  // Vertical lines every dit duration
  const pxPerMs = width / (duration * 1000);
  const gridStep = ditMs * pxPerMs;

  for (let x = 0; x < width; x += gridStep) {
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, height);
    ctx.stroke();
  }

  // Horizontal track separators
  for (let i = 1; i < 4; i++) {
    ctx.beginPath();
    ctx.moveTo(0, i * trackHeight);
    ctx.lineTo(width, i * trackHeight);
    ctx.stroke();
  }
}
```

---

### Phase 3: Controls and Features

#### 3.1 Duration Slider

```svelte
<div class="control-item">
  <label>Duration</label>
  <input type="range" min="1" max="10" step="0.5"
         bind:value={duration} />
  <span>{duration.toFixed(1)}s</span>
</div>
```

#### 3.2 Zoom Control

```svelte
<div class="control-item">
  <label>Zoom</label>
  <input type="range" min="0.5" max="4" step="0.5"
         bind:value={zoom} />
  <span>{zoom}x</span>
</div>
```

Canvas width scales with zoom:
```typescript
$effect(() => {
  canvasRef.width = 800 * zoom;
});
```

#### 3.3 Pause/Resume

```typescript
let paused = $state(false);

function togglePause() {
  paused = !paused;
  if (!paused) {
    startRenderLoop();
  }
}
```

#### 3.4 Clear Buffer

```typescript
function clearBuffer() {
  events = [];
}
```

---

### Phase 4: Polish

#### 4.1 Responsive Canvas

```typescript
function resizeCanvas() {
  const container = canvasRef.parentElement;
  if (container) {
    canvasRef.width = container.clientWidth * zoom;
  }
}

onMount(() => {
  window.addEventListener('resize', resizeCanvas);
  resizeCanvas();
});
```

#### 4.2 Decoded Text Overlay

Show decoded characters aligned with keying events:
```typescript
// Subscribe to decoder events too
onDecodedChar: (char, wpm) => {
  // Add to overlay buffer with timestamp
  decodedChars.push({ char, ts: Date.now() });
}

// In drawTimeline():
// Draw decoded chars at bottom, aligned with their timestamps
```

#### 4.3 Export as SVG/PNG

```typescript
function exportAsPNG() {
  const dataUrl = canvasRef.toDataURL('image/png');
  const link = document.createElement('a');
  link.download = 'timeline.png';
  link.href = dataUrl;
  link.click();
}
```

---

## File Changes Summary

### Backend

| File | Change |
|------|--------|
| `main/bg_task.c` | Add timeline consumer, process events, push to WebSocket |
| `components/keyer_webui/include/webui.h` | Add `webui_get_ws_client_count()` |
| `components/keyer_webui/src/http_server.c` | Implement `webui_get_ws_client_count()` |

### Frontend

| File | Change |
|------|--------|
| `frontend/src/pages/Timeline.svelte` | Complete rewrite with live canvas |
| `frontend/src/lib/api.ts` | No changes needed (handlers exist) |

---

## Testing

### Backend

1. Connect to WebSocket via browser dev tools
2. Press paddles, verify JSON messages received:
   ```json
   {"type":"paddle","ts":123456789,"paddle":0,"state":1}
   {"type":"keying","ts":123456789,"element":0,"state":1}
   ```

### Frontend

1. Open Timeline page
2. Verify "STREAM ACTIVE" indicator
3. Press paddles, verify pulses appear on DIT/DAH tracks
4. Verify OUT track shows keyer output
5. Test duration slider (1-10s range)
6. Test pause/resume

---

## Design Decisions

### Timestamp
Indifferent - what matters is visualizing event **duration** correctly. Use relative timestamps from stream.

### Visual Design

**3 Tracks (stacked rectangles showing duration):**
```
┌─────────────────────────────────────────────────────────┐
│ DIT   ████░░░░████░░░░░░░░████░░░░                      │  ← Dit paddle input
├─────────────────────────────────────────────────────────┤
│ DAH   ░░░░░░░░░░░░████████████░░░░░░████████            │  ← Dah paddle input
├─────────────────────────────────────────────────────────┤
│ OUT   ████░░░████░░░████████████░░░░████████            │  ← Keyer output
│           M     S                                        │  ← Icons overlay
└─────────────────────────────────────────────────────────┘
        time →
```

**Event Icons (overlaid on rectangles):**
- **M** = Memory window open (appears on DIT/DAH track where memory captured)
- **S** = Squeeze detected (appears on OUT track where FSM processed it)
- Memory window close = different icon or color change

**Spaces are implicit** - gaps between rectangles show element/character/word spacing.

### Data Flow
```
stream_sample_t contains:
  - gpio.bits     → DIT/DAH tracks (paddle rectangles)
  - local_key     → OUT track (keyer output rectangles)
  - flags         → Icons (memory, squeeze events)
```

### Event Types to Backend

| Event | JSON | Purpose |
|-------|------|---------|
| paddle | `{ts, paddle:0/1, state:0/1}` | DIT/DAH track rectangles |
| keying | `{ts, state:0/1}` | OUT track rectangles |
| memory | `{ts, paddle:0/1}` | Memory capture icon on paddle track |
| squeeze | `{ts}` | Squeeze detected icon on OUT track |

### Backend: New Sample Flags

The iambic FSM already tracks memory and squeeze internally (`iambic_processor_t`):
- `dit_memory`, `dah_memory` - memory captured flags
- `squeeze_seen` - squeeze detected this element

**Need to add event flags to `sample.h`:**

```c
/** Memory captured for dit paddle */
#define FLAG_MEMORY_DIT     0x40

/** Memory captured for dah paddle */
#define FLAG_MEMORY_DAH     0x80

/** Squeeze detected (both paddles during element) */
#define FLAG_SQUEEZE        0x100  // Need to expand flags to uint16_t or use separate byte
```

**Alternative (simpler for Phase 1):** Query FSM state changes from bg_task:
- Compare `proc->dit_memory` / `proc->dah_memory` / `proc->squeeze_seen` each tick
- Emit event on rising edge

This avoids modifying the core sample structure for timeline-only features

---

## Implementation Order

### Phase 1: Core Timeline (MVP)

**Backend:**
1. Add `best_effort_consumer_t` to `bg_task.c`
2. Detect paddle edges → emit `paddle` events
3. Detect keying edges → emit `keying` events
4. Gate on `ws_get_client_count() > 0`

**Frontend:**
1. WebSocket connection with auto-reconnect
2. Event buffer (time-windowed circular buffer)
3. Canvas with 3 tracks: DIT, DAH, OUT
4. Real-time rendering loop (`requestAnimationFrame`)
5. Duration slider (1-10s)

### Phase 2: FSM Events

**Backend:**
1. Expose `iambic_processor_t` state to bg_task
2. Detect memory capture edges → emit `memory` events
3. Detect squeeze edges → emit `squeeze` events

**Frontend:**
1. Render memory icons (M) on paddle tracks
2. Render squeeze icons (S) on OUT track
3. Icon positioning aligned with timestamp

### Phase 3: Polish

- Zoom control
- Pause/resume
- Clear buffer
- WPM-calibrated grid
- Decoded character overlay (optional)
- Export PNG

### File Changes Summary

| Phase | File | Change |
|-------|------|--------|
| 1 | `main/bg_task.c` | Timeline consumer, paddle/keying events |
| 1 | `components/keyer_webui/include/webui.h` | Add `webui_get_ws_client_count()` |
| 1 | `components/keyer_webui/src/http_server.c` | Implement client count getter |
| 1 | `frontend/src/pages/Timeline.svelte` | Full rewrite with live canvas |
| 1 | `frontend/src/lib/api.ts` | No changes (handlers exist) |
| 2 | `main/bg_task.c` | Memory/squeeze event detection |
| 2 | `main/rt_task.c` or expose | Access to iambic processor state |
| 2 | `frontend/src/pages/Timeline.svelte` | Icon rendering |
| 2 | `frontend/src/lib/api.ts` | Add memory/squeeze handlers |
