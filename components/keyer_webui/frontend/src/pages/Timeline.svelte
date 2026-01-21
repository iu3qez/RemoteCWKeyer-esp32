<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { api } from '../lib/api';
  import type { TimelineConfig } from '../lib/types';

  // Build info (injected by vite)
  declare const __GIT_HASH__: string;
  declare const __BUILD_TIME__: string;
  const buildInfo = `${__GIT_HASH__} @ ${__BUILD_TIME__}`;

  // Connection state
  let connected = $state(false);
  let error = $state<string | null>(null);

  // Timeline configuration
  let config = $state<TimelineConfig | null>(null);
  let duration = $state(3.0);  // seconds visible
  let paused = $state(false);

  // Track definitions
  const tracks = [
    { name: 'DIT', color: '#4169E1', label: 'Dit paddle input' },
    { name: 'DAH', color: '#DC143C', label: 'Dah paddle input' },
    { name: 'OUT', color: '#00ff41', label: 'Keying output' },
  ];

  // Event buffer
  interface TimelineEvent {
    type: 'paddle' | 'keying';
    ts: number;
    track: number;  // 0=DIT, 1=DAH, 2=OUT
    state: number;  // 1=on, 0=off
  }
  let events = $state<TimelineEvent[]>([]);
  const MAX_EVENTS = 2000;

  // Canvas
  let canvasRef: HTMLCanvasElement;
  let animationFrame: number | null = null;

  // Time tracking
  let baseTime = $state(0);  // First event timestamp (for relative display)
  let lastEventTime = $state(0);  // Last event arrival time (for auto-pause)
  const AUTO_PAUSE_DELAY = 500;  // Stop scrolling after 500ms of inactivity

  function pushEvent(event: TimelineEvent) {
    if (paused) return;

    // Set base time from first event
    if (baseTime === 0) {
      baseTime = event.ts;
    }

    // Track last event time for auto-pause
    lastEventTime = Date.now();

    events.push(event);

    // Prune old events
    if (events.length > MAX_EVENTS) {
      events = events.slice(-MAX_EVENTS);
    }

    // Also prune by time window (keep 2x visible duration)
    const now = Date.now();
    const cutoff = now - (duration * 2000);
    events = events.filter(e => e.ts > cutoff);
  }

  function startRenderLoop() {
    if (animationFrame !== null) return;

    const render = () => {
      if (!paused) {
        drawTimeline();
      }
      animationFrame = requestAnimationFrame(render);
    };
    render();
  }

  function stopRenderLoop() {
    if (animationFrame !== null) {
      cancelAnimationFrame(animationFrame);
      animationFrame = null;
    }
  }

  function drawTimeline() {
    const ctx = canvasRef?.getContext('2d');
    if (!ctx) return;

    const width = canvasRef.width;
    const height = canvasRef.height;
    const trackHeight = height / 3;

    // Auto-pause: freeze scrolling when no recent events
    const realNow = Date.now();
    const timeSinceLastEvent = lastEventTime > 0 ? realNow - lastEventTime : 0;
    const now = timeSinceLastEvent > AUTO_PAUSE_DELAY && lastEventTime > 0
      ? lastEventTime + AUTO_PAUSE_DELAY  // Freeze at last event + delay
      : realNow;  // Keep scrolling

    const windowStart = now - (duration * 1000);
    const pxPerMs = width / (duration * 1000);

    // Clear
    ctx.fillStyle = '#0d1117';
    ctx.fillRect(0, 0, width, height);

    // Draw track backgrounds and labels FIRST (so grid draws on top)
    for (let i = 0; i < 3; i++) {
      const y = i * trackHeight;

      // Track background
      ctx.fillStyle = '#161b22';
      ctx.fillRect(0, y, width, trackHeight - 2);

      // Track label
      ctx.fillStyle = '#6b8f71';  // --text-dim
      ctx.font = '11px "JetBrains Mono", monospace';
      ctx.fillText(tracks[i].name, 5, y + 14);
    }

    // Draw grid AFTER backgrounds so lines are visible
    drawGrid(ctx, width, height, trackHeight, pxPerMs);

    // Track current state for drawing pulses
    const trackStates = [false, false, false];
    const trackStartX = [0, 0, 0];

    // Sort events by timestamp
    const sortedEvents = [...events].sort((a, b) => a.ts - b.ts);

    for (const event of sortedEvents) {
      const x = (event.ts - windowStart) * pxPerMs;
      const track = event.track;

      if (event.state === 1) {
        // Start of pulse
        trackStates[track] = true;
        trackStartX[track] = Math.max(0, x);
      } else if (trackStates[track]) {
        // End of pulse - draw rectangle
        const startX = trackStartX[track];
        const endX = Math.min(width, x);
        if (endX > startX && endX > 0) {
          ctx.fillStyle = tracks[track].color;
          const y = track * trackHeight;
          ctx.fillRect(startX, y + 8, endX - startX, trackHeight - 16);
        }
        trackStates[track] = false;
      }
    }

    // Draw ongoing pulses to right edge
    for (let i = 0; i < 3; i++) {
      if (trackStates[i]) {
        ctx.fillStyle = tracks[i].color;
        const startX = Math.max(0, trackStartX[i]);
        ctx.fillRect(startX, i * trackHeight + 8, width - startX, trackHeight - 16);
      }
    }

    // Draw current time marker
    ctx.strokeStyle = '#ffb000';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(width - 2, 0);
    ctx.lineTo(width - 2, height);
    ctx.stroke();
  }

  function drawGrid(ctx: CanvasRenderingContext2D, width: number, height: number, trackHeight: number, pxPerMs: number) {
    const wpm = config?.wpm || 20;
    const ditMs = 1200 / wpm;  // ITU timing

    // Dit grid lines - subtle
    ctx.strokeStyle = '#1f3f2f';
    ctx.lineWidth = 1;

    const gridStep = ditMs * pxPerMs;
    if (gridStep > 3) {
      for (let x = width; x >= 0; x -= gridStep) {
        ctx.beginPath();
        ctx.moveTo(x, 0);
        ctx.lineTo(x, height);
        ctx.stroke();
      }
    }

    // Dah grid lines (every 3 dits) - more visible
    ctx.strokeStyle = '#2f5f3f';
    const dahStep = gridStep * 3;
    if (dahStep > 10) {
      for (let x = width; x >= 0; x -= dahStep) {
        ctx.beginPath();
        ctx.moveTo(x, 0);
        ctx.lineTo(x, height);
        ctx.stroke();
      }
    }

    // Time scale labels
    ctx.fillStyle = '#6b8f71';  // --text-dim
    ctx.font = '10px "JetBrains Mono", monospace';
    const labelStep = 500;
    const labelStepPx = labelStep * pxPerMs;
    if (labelStepPx > 30) {
      for (let i = 0; i <= Math.ceil(duration * 1000 / labelStep); i++) {
        const msFromRight = i * labelStep;
        const x = width - (msFromRight * pxPerMs);
        if (x >= 0) {
          // Time marker line
          ctx.strokeStyle = '#4ec968';  // --text-secondary
          ctx.lineWidth = 1.5;
          ctx.beginPath();
          ctx.moveTo(x, 0);
          ctx.lineTo(x, height);
          ctx.stroke();

          // Time label
          const label = msFromRight === 0 ? 'NOW' : `-${msFromRight}ms`;
          const textWidth = ctx.measureText(label).width;
          ctx.fillText(label, x - textWidth / 2, height - 3);
        }
      }
    }

    // Horizontal track separators
    ctx.strokeStyle = '#2f5f3f';  // --border-dim
    ctx.lineWidth = 1;
    for (let i = 1; i < 3; i++) {
      ctx.beginPath();
      ctx.moveTo(0, i * trackHeight);
      ctx.lineTo(width, i * trackHeight);
      ctx.stroke();
    }
  }

  function togglePause() {
    paused = !paused;
    if (!paused && connected) {
      // Resume - trigger immediate redraw
      drawTimeline();
    }
  }

  function clearBuffer() {
    events = [];
    baseTime = 0;
    lastEventTime = 0;
    drawTimeline();
  }

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
      onKeying: (ts, _element, state) => {
        pushEvent({
          type: 'keying',
          ts,
          track: 2,  // OUT track
          state
        });
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

    // Start render loop even if not connected (shows empty timeline)
    startRenderLoop();
  });

  onDestroy(() => {
    api.disconnect();
    stopRenderLoop();
  });

  // Derived
  let wpmDisplay = $derived(config?.wpm || 20);
</script>

<div class="timeline-page">
  <div class="page-header">
    <h1>/// TIMELINE VISUALIZER</h1>
    <span class="build-info">{buildInfo}</span>
    <div class="header-status">
      <span class="connection-status" class:connected>
        {connected ? '● STREAM ACTIVE' : '○ DISCONNECTED'}
      </span>
    </div>
  </div>

  {#if error}
    <div class="error-box">
      <span class="error-icon">[!]</span>
      <span class="error-text">{error}</span>
    </div>
  {/if}

  <div class="timeline-panel">
    <div class="panel-header">
      <span class="panel-icon">[T]</span>
      <span class="panel-title">KEYING TIMELINE</span>
      <span class="panel-badge">{wpmDisplay} WPM</span>
    </div>

    <div class="canvas-container">
      <canvas bind:this={canvasRef} width="900" height="180"></canvas>
    </div>

    <div class="track-legend">
      {#each tracks as track}
        <div class="legend-item">
          <span class="legend-color" style="background: {track.color}"></span>
          <span class="legend-name">{track.name}</span>
          <span class="legend-desc">{track.label}</span>
        </div>
      {/each}
    </div>
  </div>

  <div class="controls-panel">
    <div class="panel-header">
      <span class="panel-icon">[C]</span>
      <span class="panel-title">CONTROLS</span>
    </div>

    <div class="control-grid">
      <div class="control-item">
        <label class="control-label" for="duration-range">Window</label>
        <input id="duration-range" type="range" min="1" max="10" step="0.5"
               bind:value={duration} />
        <span class="control-value">{duration.toFixed(1)}s</span>
      </div>

      <div class="control-buttons">
        <button class="action-btn" class:active={paused} onclick={togglePause}>
          <span class="btn-icon">{paused ? '▶' : '⏸'}</span>
          <span>{paused ? 'RESUME' : 'PAUSE'}</span>
        </button>
        <button class="action-btn" onclick={clearBuffer}>
          <span class="btn-icon">⌫</span>
          <span>CLEAR</span>
        </button>
      </div>
    </div>

    <div class="stats-row">
      <span class="stat-item">Events: {events.length}</span>
      <span class="stat-item">Duration: {duration}s</span>
    </div>
  </div>
</div>

<style>
  .timeline-page {
    max-width: 1000px;
  }

  .page-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 1.5rem;
  }

  .page-header h1 {
    color: var(--accent-cyan);
    font-size: 1.2rem;
    font-weight: 600;
    letter-spacing: 1px;
  }

  .build-info {
    font-size: 0.7rem;
    color: var(--accent-cyan);
    font-family: "JetBrains Mono", monospace;
    background: var(--bg-tertiary);
    padding: 0.2rem 0.5rem;
    border: 1px solid var(--border-dim);
  }

  .connection-status {
    padding: 0.25rem 0.75rem;
    background: var(--bg-tertiary);
    border: 1px solid var(--border-dim);
    font-size: 0.75rem;
    color: var(--text-dim);
    letter-spacing: 0.5px;
  }

  .connection-status.connected {
    color: var(--accent-green);
    border-color: var(--accent-green);
  }

  .error-box {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    padding: 0.75rem 1rem;
    background: rgba(220, 20, 60, 0.1);
    border: 1px solid var(--accent-red);
    margin-bottom: 1rem;
  }

  .error-icon {
    color: var(--accent-red);
    font-weight: 700;
  }

  .error-text {
    color: var(--accent-red);
    font-size: 0.85rem;
  }

  .timeline-panel,
  .controls-panel {
    background: var(--bg-secondary);
    border: 1px solid var(--border-dim);
    padding: 1rem;
    margin-bottom: 1rem;
  }

  .panel-header {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    margin-bottom: 1rem;
    padding-bottom: 0.5rem;
    border-bottom: 1px solid var(--border-dim);
  }

  .panel-icon {
    color: var(--accent-amber);
    font-weight: 700;
    font-size: 0.85rem;
  }

  .panel-title {
    color: var(--text-primary);
    font-size: 0.85rem;
    font-weight: 600;
    letter-spacing: 0.5px;
  }

  .panel-badge {
    margin-left: auto;
    padding: 0.15rem 0.5rem;
    background: var(--accent-green);
    color: var(--bg-primary);
    font-size: 0.7rem;
    font-weight: 700;
    letter-spacing: 0.5px;
  }

  .canvas-container {
    background: var(--bg-primary);
    border: 1px solid var(--border-dim);
    overflow-x: auto;
    margin-bottom: 1rem;
  }

  canvas {
    display: block;
    width: 100%;
    height: auto;
  }

  .track-legend {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 0.5rem;
  }

  .legend-item {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    padding: 0.4rem;
    background: var(--bg-tertiary);
  }

  .legend-color {
    width: 16px;
    height: 16px;
    flex-shrink: 0;
  }

  .legend-name {
    font-size: 0.75rem;
    font-weight: 600;
    color: var(--text-primary);
    min-width: 35px;
  }

  .legend-desc {
    font-size: 0.7rem;
    color: var(--text-dim);
  }

  .control-grid {
    display: flex;
    align-items: center;
    gap: 2rem;
    margin-bottom: 1rem;
  }

  .control-item {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    flex: 1;
  }

  .control-label {
    font-size: 0.8rem;
    color: var(--text-secondary);
    min-width: 55px;
  }

  .control-item input[type="range"] {
    flex: 1;
    height: 4px;
    background: var(--bg-tertiary);
    border: none;
    -webkit-appearance: none;
    cursor: pointer;
  }

  .control-item input[type="range"]::-webkit-slider-thumb {
    -webkit-appearance: none;
    width: 14px;
    height: 14px;
    background: var(--accent-green);
    border-radius: 0;
    cursor: pointer;
  }

  .control-value {
    font-size: 0.8rem;
    color: var(--accent-green);
    min-width: 40px;
    font-weight: 600;
  }

  .control-buttons {
    display: flex;
    gap: 0.5rem;
  }

  .action-btn {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    padding: 0.5rem 0.75rem;
    background: var(--bg-tertiary);
    border: 1px solid var(--border-dim);
    color: var(--text-secondary);
    font-size: 0.75rem;
    font-family: inherit;
    cursor: pointer;
    transition: all 0.15s ease;
  }

  .action-btn:hover {
    background: var(--bg-hover);
    border-color: var(--accent-green);
    color: var(--accent-green);
  }

  .action-btn.active {
    background: var(--accent-amber);
    color: var(--bg-primary);
    border-color: var(--accent-amber);
  }

  .btn-icon {
    font-size: 0.85rem;
  }

  .stats-row {
    display: flex;
    gap: 2rem;
    font-size: 0.75rem;
    color: var(--text-dim);
  }

  .stat-item {
    display: flex;
    gap: 0.5rem;
  }

  @media (max-width: 768px) {
    .track-legend {
      grid-template-columns: 1fr;
    }

    .control-grid {
      flex-direction: column;
      align-items: stretch;
      gap: 1rem;
    }

    .control-buttons {
      justify-content: center;
    }
  }
</style>
