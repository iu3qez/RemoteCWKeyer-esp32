<script lang="ts">
  import { onMount } from 'svelte';

  let canvasRef: HTMLCanvasElement;
  let demoActive = $state(false);

  const tracks = [
    { name: 'DIT', color: '#4169E1', label: 'Dit paddle input' },
    { name: 'DAH', color: '#DC143C', label: 'Dah paddle input' },
    { name: 'OUT', color: '#00ff41', label: 'Keying output' },
    { name: 'LOGIC', color: '#ffb000', label: 'FSM state' },
  ];

  onMount(() => {
    drawDemoTimeline();
  });

  function drawDemoTimeline() {
    const ctx = canvasRef.getContext('2d');
    if (!ctx) return;

    const width = canvasRef.width;
    const height = canvasRef.height;
    const trackHeight = height / 4;

    // Clear
    ctx.fillStyle = '#0d1117';
    ctx.fillRect(0, 0, width, height);

    // Draw grid
    ctx.strokeStyle = '#004411';
    ctx.lineWidth = 1;
    for (let x = 0; x < width; x += 50) {
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, height);
      ctx.stroke();
    }

    // Draw track backgrounds
    tracks.forEach((track, i) => {
      const y = i * trackHeight;

      // Track background
      ctx.fillStyle = '#161b22';
      ctx.fillRect(0, y, width, trackHeight - 2);

      // Track label
      ctx.fillStyle = '#006622';
      ctx.font = '10px JetBrains Mono, monospace';
      ctx.fillText(track.name, 5, y + 12);

      // Demo pulses
      ctx.fillStyle = track.color;
      const pulseCount = 3 + Math.floor(Math.random() * 5);
      for (let p = 0; p < pulseCount; p++) {
        const x = 80 + p * (width - 100) / pulseCount + Math.random() * 30;
        const w = 20 + Math.random() * 40;
        ctx.fillRect(x, y + 15, w, trackHeight - 30);
      }
    });

    // Draw decoded text line
    ctx.fillStyle = '#00ff41';
    ctx.font = '12px JetBrains Mono, monospace';
    ctx.fillText('C Q   C Q   D E   I U 3 Q E Z', 80, height - 5);
  }
</script>

<div class="timeline-page">
  <div class="page-header">
    <h1>/// TIMELINE VISUALIZER</h1>
    <div class="header-status">
      <span class="status-badge">COMING SOON</span>
    </div>
  </div>

  <div class="coming-soon-banner">
    <div class="banner-icon">⚠</div>
    <div class="banner-text">
      <strong>Feature Under Development</strong>
      <span>Real-time timeline visualization will be available in a future update</span>
    </div>
  </div>

  <div class="preview-panel">
    <div class="panel-header">
      <span class="panel-icon">[V]</span>
      <span class="panel-title">TIMELINE PREVIEW</span>
      <span class="panel-badge">DEMO</span>
    </div>

    <div class="canvas-container">
      <canvas bind:this={canvasRef} width="800" height="200"></canvas>
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

  <div class="controls-preview">
    <div class="panel-header">
      <span class="panel-icon">[C]</span>
      <span class="panel-title">CONTROLS</span>
    </div>

    <div class="control-grid">
      <div class="control-item">
        <label class="control-label">Duration</label>
        <input type="range" min="1" max="10" value="3" disabled />
        <span class="control-value">3.0s</span>
      </div>
      <div class="control-item">
        <label class="control-label">Zoom</label>
        <input type="range" min="1" max="5" value="1" disabled />
        <span class="control-value">1x</span>
      </div>
    </div>

    <div class="checkbox-row">
      <label class="checkbox-item">
        <input type="checkbox" checked disabled />
        <span>Show Memory Window</span>
      </label>
      <label class="checkbox-item">
        <input type="checkbox" checked disabled />
        <span>Show Latch Events</span>
      </label>
      <label class="checkbox-item">
        <input type="checkbox" checked disabled />
        <span>Show Gap Markers</span>
      </label>
      <label class="checkbox-item">
        <input type="checkbox" checked disabled />
        <span>Align Decoded Text</span>
      </label>
    </div>
  </div>

  <div class="features-panel">
    <div class="panel-header">
      <span class="panel-icon">[?]</span>
      <span class="panel-title">PLANNED FEATURES</span>
    </div>

    <ul class="feature-list">
      <li>
        <span class="feature-icon">○</span>
        <span>Real-time paddle input visualization</span>
      </li>
      <li>
        <span class="feature-icon">○</span>
        <span>Keying output waveform display</span>
      </li>
      <li>
        <span class="feature-icon">○</span>
        <span>Iambic FSM state tracking</span>
      </li>
      <li>
        <span class="feature-icon">○</span>
        <span>Memory window visualization</span>
      </li>
      <li>
        <span class="feature-icon">○</span>
        <span>Squeeze detection markers</span>
      </li>
      <li>
        <span class="feature-icon">○</span>
        <span>Decoded character alignment</span>
      </li>
      <li>
        <span class="feature-icon">○</span>
        <span>WPM-calibrated grid overlay</span>
      </li>
      <li>
        <span class="feature-icon">○</span>
        <span>Export timeline as SVG/PNG</span>
      </li>
    </ul>
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

  .status-badge {
    padding: 0.25rem 0.75rem;
    background: var(--accent-amber);
    color: var(--bg-primary);
    font-size: 0.7rem;
    font-weight: 700;
    letter-spacing: 1px;
  }

  .coming-soon-banner {
    display: flex;
    align-items: center;
    gap: 1rem;
    padding: 1rem;
    background: rgba(255, 176, 0, 0.1);
    border: 1px solid var(--accent-amber);
    margin-bottom: 1.5rem;
  }

  .banner-icon {
    font-size: 1.5rem;
    color: var(--accent-amber);
  }

  .banner-text {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
  }

  .banner-text strong {
    color: var(--accent-amber);
    font-size: 0.95rem;
  }

  .banner-text span {
    color: var(--text-dim);
    font-size: 0.8rem;
  }

  .preview-panel,
  .controls-preview,
  .features-panel {
    background: var(--bg-secondary);
    border: 1px solid var(--border-dim);
    padding: 1rem;
    margin-bottom: 1rem;
  }

  .preview-panel {
    opacity: 0.8;
  }

  .controls-preview {
    opacity: 0.6;
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
    background: var(--bg-tertiary);
    border: 1px solid var(--border-dim);
    font-size: 0.65rem;
    color: var(--text-dim);
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
    max-width: 100%;
    height: auto;
  }

  .track-legend {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
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
    min-width: 40px;
  }

  .legend-desc {
    font-size: 0.7rem;
    color: var(--text-dim);
  }

  .control-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
    gap: 1rem;
    margin-bottom: 1rem;
  }

  .control-item {
    display: flex;
    align-items: center;
    gap: 0.75rem;
  }

  .control-label {
    font-size: 0.8rem;
    color: var(--text-secondary);
    min-width: 70px;
  }

  .control-item input[type="range"] {
    flex: 1;
    height: 4px;
    background: var(--bg-tertiary);
    border: none;
    -webkit-appearance: none;
  }

  .control-item input[type="range"]:disabled {
    opacity: 0.4;
  }

  .control-value {
    font-size: 0.8rem;
    color: var(--text-dim);
    min-width: 40px;
  }

  .checkbox-row {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
    gap: 0.5rem;
  }

  .checkbox-item {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    font-size: 0.8rem;
    color: var(--text-secondary);
    cursor: not-allowed;
    opacity: 0.5;
  }

  .checkbox-item input {
    width: 16px;
    height: 16px;
  }

  .feature-list {
    list-style: none;
    padding: 0;
    margin: 0;
  }

  .feature-list li {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    padding: 0.5rem 0;
    border-bottom: 1px dashed var(--border-dim);
    color: var(--text-secondary);
    font-size: 0.85rem;
  }

  .feature-list li:last-child {
    border-bottom: none;
  }

  .feature-icon {
    color: var(--text-dim);
    font-size: 0.75rem;
  }

  @media (max-width: 768px) {
    .track-legend {
      grid-template-columns: 1fr 1fr;
    }

    .control-grid {
      grid-template-columns: 1fr;
    }

    .checkbox-row {
      grid-template-columns: 1fr;
    }
  }
</style>
