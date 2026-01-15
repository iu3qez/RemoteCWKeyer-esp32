<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { api } from '../lib/api';
  import type { DecoderStatus } from '../lib/types';

  let status = $state<DecoderStatus | null>(null);
  let decodedText = $state('');
  let currentWpm = $state(0);
  let currentPattern = $state('');
  let error = $state<string | null>(null);
  let connected = $state(false);
  let charCount = $state(0);

  async function refresh() {
    try {
      status = await api.getDecoderStatus();
      error = null;
    } catch (e) {
      error = e instanceof Error ? e.message : 'Unknown error';
    }
  }

  async function toggleEnabled() {
    if (!status) return;
    try {
      await api.setDecoderEnabled(!status.enabled);
      await refresh();
    } catch (e) {
      error = e instanceof Error ? e.message : 'Failed to toggle decoder';
    }
  }

  function scrollTerminal() {
    setTimeout(() => {
      const terminal = document.getElementById('decoder-terminal');
      if (terminal) terminal.scrollTop = terminal.scrollHeight;
    }, 10);
  }

  function clearText() {
    decodedText = '';
    charCount = 0;
  }

  onMount(() => {
    refresh();
    api.connect({
      onDecodedChar: (char, wpm) => {
        decodedText += char;
        currentWpm = wpm;
        charCount++;
        // Keep last 500 chars
        if (decodedText.length > 500) {
          decodedText = decodedText.slice(-500);
        }
        scrollTerminal();
      },
      onWord: () => {
        decodedText += ' ';
      },
      onConnect: () => {
        connected = true;
        error = null;
      },
      onDisconnect: () => {
        connected = false;
        error = 'WebSocket disconnected. Reconnecting...';
      }
    });
  });

  onDestroy(() => {
    api.disconnect();
  });

  let wpmDisplay = $derived(currentWpm || status?.wpm || 0);
  let wpmPercent = $derived(Math.min(100, Math.max(0, ((wpmDisplay - 5) / 55) * 100)));
  let patternDisplay = $derived(status?.pattern || currentPattern || '');
</script>

<div class="decoder-page">
  <div class="page-header">
    <h1>/// MORSE DECODER</h1>
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

  <div class="grid-layout">
    <!-- Control Panel -->
    <div class="panel control-panel">
      <div class="panel-header">
        <span class="panel-icon">[C]</span>
        <span class="panel-title">CONTROL</span>
      </div>
      {#if status}
        <div class="toggle-row">
          <label class="toggle-control" onclick={toggleEnabled}>
            <span class="toggle-track" class:active={status.enabled}>
              <span class="toggle-thumb"></span>
            </span>
            <span class="toggle-text">
              {status.enabled ? 'DECODER ACTIVE' : 'DECODER OFF'}
            </span>
          </label>
        </div>
        <div class="action-buttons">
          <button class="action-btn" onclick={clearText}>
            <span class="btn-icon">⌫</span>
            <span>CLEAR BUFFER</span>
          </button>
        </div>
      {:else}
        <div class="loading">Initializing...</div>
      {/if}
    </div>

    <!-- WPM Display -->
    <div class="panel wpm-panel">
      <div class="panel-header">
        <span class="panel-icon">[W]</span>
        <span class="panel-title">SPEED</span>
      </div>
      <div class="wpm-display">
        <span class="wpm-value">{wpmDisplay}</span>
        <span class="wpm-unit">WPM</span>
      </div>
      <div class="wpm-bar">
        <div class="wpm-bar-fill" style="width: {wpmPercent}%"></div>
      </div>
      <div class="wpm-scale">
        <span>5</span>
        <span>20</span>
        <span>35</span>
        <span>50</span>
        <span>60</span>
      </div>
    </div>

    <!-- Pattern Display -->
    <div class="panel pattern-panel">
      <div class="panel-header">
        <span class="panel-icon">[P]</span>
        <span class="panel-title">CURRENT PATTERN</span>
      </div>
      <div class="pattern-display">
        {#if patternDisplay}
          <span class="pattern-chars">{patternDisplay}</span>
        {:else}
          <span class="pattern-empty">---</span>
        {/if}
      </div>
      <div class="pattern-hint">
        <span class="dit">●</span> = dit
        <span class="dah">━</span> = dah
      </div>
    </div>
  </div>

  <!-- Terminal Output -->
  <div class="panel terminal-panel">
    <div class="panel-header">
      <span class="panel-icon">[T]</span>
      <span class="panel-title">DECODED OUTPUT</span>
      <span class="char-count">{charCount} chars</span>
    </div>
    <div class="terminal" id="decoder-terminal">
      <div class="terminal-content">
        {#if decodedText}
          <span class="decoded-text">{decodedText}</span>
        {:else}
          <span class="terminal-waiting">
            <span class="waiting-text">AWAITING CW INPUT</span>
            <span class="waiting-cursor">█</span>
          </span>
        {/if}
        <span class="terminal-cursor">▌</span>
      </div>
    </div>
  </div>

  <!-- Stats Panel -->
  <div class="panel stats-panel">
    <div class="stats-row">
      <div class="stat">
        <span class="stat-label">STATUS</span>
        <span class="stat-value" class:active={status?.enabled}>
          {status?.enabled ? 'ACTIVE' : 'STANDBY'}
        </span>
      </div>
      <div class="stat">
        <span class="stat-label">STREAM</span>
        <span class="stat-value" class:connected>{connected ? 'CONNECTED' : 'WAITING'}</span>
      </div>
      <div class="stat">
        <span class="stat-label">BUFFER</span>
        <span class="stat-value">{decodedText.length}/500</span>
      </div>
    </div>
  </div>
</div>

<style>
  .decoder-page {
    max-width: 1000px;
  }

  .page-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 1.5rem;
    flex-wrap: wrap;
    gap: 1rem;
  }

  .page-header h1 {
    color: var(--accent-cyan);
    font-size: 1.2rem;
    font-weight: 600;
    letter-spacing: 1px;
  }

  .connection-status {
    font-size: 0.8rem;
    color: var(--accent-red);
    letter-spacing: 0.5px;
  }

  .connection-status.connected {
    color: var(--text-primary);
    text-shadow: var(--glow-green);
  }

  .error-box {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    padding: 0.75rem 1rem;
    background: rgba(255, 51, 51, 0.1);
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

  .grid-layout {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
    gap: 1rem;
    margin-bottom: 1rem;
  }

  /* Panels */
  .panel {
    background: var(--bg-secondary);
    border: 1px solid var(--border-dim);
    padding: 1rem;
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

  .char-count {
    margin-left: auto;
    font-size: 0.7rem;
    color: var(--text-dim);
  }

  /* Control Panel */
  .toggle-row {
    margin-bottom: 1rem;
  }

  .toggle-control {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    cursor: pointer;
    padding: 0.5rem;
    background: var(--bg-tertiary);
    border: 1px solid var(--border-dim);
    transition: all 0.2s;
  }

  .toggle-control:hover {
    border-color: var(--text-secondary);
  }

  .toggle-track {
    width: 48px;
    height: 24px;
    background: var(--bg-primary);
    border: 1px solid var(--border-dim);
    position: relative;
    transition: all 0.2s;
  }

  .toggle-track.active {
    background: var(--text-primary);
    border-color: var(--text-primary);
    box-shadow: var(--glow-green);
  }

  .toggle-thumb {
    position: absolute;
    width: 18px;
    height: 18px;
    background: var(--text-dim);
    top: 2px;
    left: 2px;
    transition: all 0.2s;
  }

  .toggle-track.active .toggle-thumb {
    left: 26px;
    background: var(--bg-primary);
  }

  .toggle-text {
    font-size: 0.85rem;
    font-weight: 600;
    color: var(--text-secondary);
    letter-spacing: 0.5px;
  }

  .action-buttons {
    display: flex;
    gap: 0.5rem;
  }

  .action-btn {
    flex: 1;
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 0.4rem;
    padding: 0.5rem;
    font-size: 0.75rem;
  }

  .btn-icon {
    font-size: 0.9rem;
  }

  /* WPM Panel */
  .wpm-display {
    text-align: center;
    margin-bottom: 0.75rem;
  }

  .wpm-value {
    font-size: 3rem;
    font-weight: 700;
    color: var(--accent-cyan);
    text-shadow: var(--glow-cyan);
    display: block;
    line-height: 1;
  }

  .wpm-unit {
    font-size: 0.85rem;
    color: var(--text-dim);
    letter-spacing: 2px;
  }

  .wpm-bar {
    height: 8px;
    background: var(--bg-tertiary);
    border: 1px solid var(--border-dim);
    overflow: hidden;
    margin-bottom: 0.25rem;
  }

  .wpm-bar-fill {
    height: 100%;
    background: linear-gradient(90deg, var(--text-primary), var(--accent-cyan));
    transition: width 0.3s ease;
    box-shadow: var(--glow-green);
  }

  .wpm-scale {
    display: flex;
    justify-content: space-between;
    font-size: 0.65rem;
    color: var(--text-dim);
  }

  /* Pattern Panel */
  .pattern-display {
    text-align: center;
    padding: 1rem;
    background: var(--bg-tertiary);
    border: 1px solid var(--border-dim);
    margin-bottom: 0.75rem;
  }

  .pattern-chars {
    font-size: 2rem;
    font-weight: 700;
    color: var(--accent-amber);
    letter-spacing: 4px;
    text-shadow: var(--glow-amber);
  }

  .pattern-empty {
    font-size: 1.5rem;
    color: var(--text-dim);
  }

  .pattern-hint {
    text-align: center;
    font-size: 0.7rem;
    color: var(--text-dim);
  }

  .pattern-hint .dit {
    color: var(--text-primary);
    margin-right: 0.25rem;
  }

  .pattern-hint .dah {
    color: var(--accent-amber);
    margin-left: 0.75rem;
    margin-right: 0.25rem;
  }

  /* Terminal */
  .terminal-panel {
    margin-bottom: 1rem;
  }

  .terminal {
    background: var(--bg-primary);
    border: 1px solid var(--text-primary);
    padding: 1rem;
    min-height: 200px;
    max-height: 400px;
    overflow-y: auto;
    box-shadow: inset 0 0 30px rgba(0, 255, 65, 0.03);
  }

  .terminal-content {
    font-size: 1.1rem;
    line-height: 1.6;
    word-wrap: break-word;
    white-space: pre-wrap;
  }

  .decoded-text {
    color: var(--text-primary);
    text-shadow: 0 0 8px rgba(0, 255, 65, 0.5);
  }

  .terminal-waiting {
    color: var(--text-dim);
    display: flex;
    align-items: center;
    gap: 0.5rem;
  }

  .waiting-text {
    letter-spacing: 1px;
  }

  .waiting-cursor {
    animation: blink 1s step-end infinite;
  }

  .terminal-cursor {
    color: var(--text-primary);
    animation: blink 0.7s step-end infinite;
  }

  @keyframes blink {
    0%, 50% { opacity: 1; }
    51%, 100% { opacity: 0; }
  }

  /* Stats Panel */
  .stats-panel {
    padding: 0.75rem 1rem;
  }

  .stats-row {
    display: flex;
    justify-content: space-around;
    flex-wrap: wrap;
    gap: 1rem;
  }

  .stat {
    text-align: center;
  }

  .stat-label {
    display: block;
    font-size: 0.65rem;
    color: var(--text-dim);
    letter-spacing: 1px;
    margin-bottom: 0.25rem;
  }

  .stat-value {
    font-size: 0.85rem;
    font-weight: 600;
    color: var(--text-secondary);
  }

  .stat-value.active,
  .stat-value.connected {
    color: var(--text-primary);
  }

  .loading {
    color: var(--text-dim);
    text-align: center;
    font-style: italic;
  }

  @media (max-width: 768px) {
    .grid-layout {
      grid-template-columns: 1fr;
    }

    .wpm-value {
      font-size: 2.5rem;
    }

    .terminal {
      min-height: 150px;
    }

    .terminal-content {
      font-size: 1rem;
    }

    .stats-row {
      justify-content: space-between;
    }
  }
</style>
