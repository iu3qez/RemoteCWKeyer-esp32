<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { api } from '../lib/api';
  import type { DeviceStatus } from '../lib/types';

  let status = $state<DeviceStatus | null>(null);
  let wpm = $state('--');
  let mode = $state('---');
  let pollInterval: number | null = null;
  let bootText = $state('');
  let showContent = $state(false);

  const bootSequence = [
    '[    0.000000] Remote CW Keyer System v1.0',
    '[    0.001234] Initializing ESP32-S3 @ 240MHz...',
    '[    0.002456] Memory: 320KB SRAM available',
    '[    0.003789] Loading iambic FSM module...',
    '[    0.005012] Audio subsystem: ES8311 codec ready',
    '[    0.006345] GPIO: Paddle inputs configured',
    '[    0.007678] WiFi: Connecting to network...',
    '[    0.008901] System ready. Welcome, operator.',
    ''
  ];

  async function loadStatus() {
    try {
      const [statusData, config] = await Promise.all([
        api.getStatus(),
        api.getConfig(),
      ]);
      status = statusData;
      if (config.keyer) {
        wpm = config.keyer.wpm?.toString() || '--';
        mode = config.keyer.mode || '---';
      }
    } catch (error) {
      console.error('Failed to load status:', error);
    }
  }

  onMount(async () => {
    // Boot sequence animation
    for (let i = 0; i < bootSequence.length; i++) {
      await new Promise(r => setTimeout(r, 80));
      bootText += bootSequence[i] + '\n';
    }
    await new Promise(r => setTimeout(r, 300));
    showContent = true;

    loadStatus();
    pollInterval = setInterval(loadStatus, 5000) as unknown as number;
  });

  onDestroy(() => {
    if (pollInterval) clearInterval(pollInterval);
  });

  const menuItems = [
    { key: '1', label: 'Configuration', desc: 'Device parameters & settings', path: '/config', icon: '[CFG]' },
    { key: '2', label: 'System Monitor', desc: 'CPU, memory, tasks status', path: '/system', icon: '[SYS]' },
    { key: '3', label: 'Text Keyer', desc: 'Send CW from keyboard', path: '/keyer', icon: '[KEY]' },
    { key: '4', label: 'Morse Decoder', desc: 'Real-time CW decoding', path: '/decoder', icon: '[DEC]' },
    { key: '5', label: 'Timeline', desc: 'Keying event visualization', path: '/timeline', icon: '[TML]' },
  ];
</script>

<div class="home-page">
  <div class="boot-screen" class:hidden={showContent}>
    <pre class="boot-text">{bootText}</pre>
  </div>

  {#if showContent}
    <div class="content" class:visible={showContent}>
      <div class="ascii-header">
        <pre class="ascii-art">
██████╗ ███████╗███╗   ███╗ ██████╗ ████████╗███████╗
██╔══██╗██╔════╝████╗ ████║██╔═══██╗╚══██╔══╝██╔════╝
██████╔╝█████╗  ██╔████╔██║██║   ██║   ██║   █████╗
██╔══██╗██╔══╝  ██║╚██╔╝██║██║   ██║   ██║   ██╔══╝
██║  ██║███████╗██║ ╚═╝ ██║╚██████╔╝   ██║   ███████╗
╚═╝  ╚═╝╚══════╝╚═╝     ╚═╝ ╚═════╝    ╚═╝   ╚══════╝
 ██████╗██╗    ██╗    ██╗  ██╗███████╗██╗   ██╗███████╗██████╗
██╔════╝██║    ██║    ██║ ██╔╝██╔════╝╚██╗ ██╔╝██╔════╝██╔══██╗
██║     ██║ █╗ ██║    █████╔╝ █████╗   ╚████╔╝ █████╗  ██████╔╝
██║     ██║███╗██║    ██╔═██╗ ██╔══╝    ╚██╔╝  ██╔══╝  ██╔══██╗
╚██████╗╚███╔███╔╝    ██║  ██╗███████╗   ██║   ███████╗██║  ██║
 ╚═════╝ ╚══╝╚══╝     ╚═╝  ╚═╝╚══════╝   ╚═╝   ╚══════╝╚═╝  ╚═╝</pre>
        <div class="subtitle">[ RemoteKeyer IU3QEZ // Precision Iambic Keyer ]</div>
      </div>

      <div class="status-panel">
        <div class="panel-header">
          <span class="panel-title">/// SYSTEM STATUS</span>
          <span class="panel-line"></span>
        </div>
        <div class="status-grid">
          <div class="status-item">
            <span class="status-label">NETWORK</span>
            <span class="status-value" class:online={status?.ready}>{status?.mode || '---'}</span>
          </div>
          <div class="status-item">
            <span class="status-label">IP_ADDR</span>
            <span class="status-value ip">{status?.ip || '0.0.0.0'}</span>
          </div>
          <div class="status-item">
            <span class="status-label">WPM</span>
            <span class="status-value wpm">{wpm}</span>
          </div>
          <div class="status-item">
            <span class="status-label">MODE</span>
            <span class="status-value mode">{mode}</span>
          </div>
          <div class="status-item">
            <span class="status-label">STATUS</span>
            <span class="status-value" class:online={status?.ready}>
              {status?.ready ? 'READY' : 'INIT...'}
            </span>
          </div>
        </div>
      </div>

      <div class="menu-panel">
        <div class="panel-header">
          <span class="panel-title">/// MAIN MENU</span>
          <span class="panel-line"></span>
        </div>
        <div class="menu-list">
          {#each menuItems as item}
            <a href={item.path} class="menu-item">
              <span class="menu-key">[{item.key}]</span>
              <span class="menu-icon">{item.icon}</span>
              <span class="menu-content">
                <span class="menu-label">{item.label}</span>
                <span class="menu-desc">{item.desc}</span>
              </span>
              <span class="menu-arrow">→</span>
            </a>
          {/each}
        </div>
      </div>

      <div class="info-panel">
        <div class="info-row">
          <span class="info-label">Build:</span>
          <span class="info-value">ESP-IDF v5.x // Svelte 5</span>
        </div>
        <div class="info-row">
          <span class="info-label">Target:</span>
          <span class="info-value">ESP32-S3 @ 240MHz // 16MB Flash</span>
        </div>
        <div class="morse-demo">
          .-. . -- --- - . -.- . -.-- . .-.
        </div>
      </div>
    </div>
  {/if}
</div>

<style>
  .home-page {
    min-height: calc(100vh - 200px);
  }

  .boot-screen {
    padding: 1rem;
    transition: opacity 0.3s ease;
  }

  .boot-screen.hidden {
    display: none;
  }

  .boot-text {
    font-size: 0.8rem;
    color: var(--text-dim);
    line-height: 1.4;
    white-space: pre-wrap;
  }

  .content {
    opacity: 0;
    animation: fadeIn 0.5s ease forwards;
  }

  .content.visible {
    opacity: 1;
  }

  @keyframes fadeIn {
    from { opacity: 0; transform: translateY(10px); }
    to { opacity: 1; transform: translateY(0); }
  }

  .ascii-header {
    text-align: center;
    margin-bottom: 2rem;
  }

  .ascii-art {
    font-size: 0.55rem;
    line-height: 1.1;
    color: var(--text-primary);
    text-shadow: var(--glow-green);
    margin: 0;
    overflow-x: auto;
  }

  @media (min-width: 768px) {
    .ascii-art {
      font-size: 0.7rem;
    }
  }

  .subtitle {
    margin-top: 1rem;
    color: var(--text-secondary);
    font-size: 0.85rem;
    letter-spacing: 2px;
  }

  /* Panels */
  .status-panel,
  .menu-panel,
  .info-panel {
    background: var(--bg-secondary);
    border: 1px solid var(--border-dim);
    margin-bottom: 1.5rem;
    padding: 1rem;
  }

  .panel-header {
    display: flex;
    align-items: center;
    gap: 1rem;
    margin-bottom: 1rem;
  }

  .panel-title {
    color: var(--accent-cyan);
    font-size: 0.85rem;
    font-weight: 600;
    letter-spacing: 1px;
    white-space: nowrap;
  }

  .panel-line {
    flex: 1;
    height: 1px;
    background: linear-gradient(90deg, var(--border-dim), transparent);
  }

  /* Status Grid */
  .status-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
    gap: 1rem;
  }

  .status-item {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
    padding: 0.75rem;
    background: var(--bg-tertiary);
    border-left: 2px solid var(--border-dim);
  }

  .status-label {
    font-size: 0.7rem;
    color: var(--text-dim);
    letter-spacing: 1px;
  }

  .status-value {
    font-size: 1.1rem;
    font-weight: 600;
    color: var(--text-secondary);
  }

  .status-value.online {
    color: var(--text-primary);
    text-shadow: var(--glow-green);
  }

  .status-value.ip {
    color: var(--accent-amber);
    font-size: 0.95rem;
  }

  .status-value.wpm {
    color: var(--accent-cyan);
  }

  .status-value.mode {
    color: var(--accent-magenta);
    text-transform: uppercase;
  }

  /* Menu */
  .menu-list {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }

  .menu-item {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    padding: 0.75rem 1rem;
    background: var(--bg-tertiary);
    border: 1px solid transparent;
    color: var(--text-secondary);
    text-decoration: none;
    transition: all 0.15s ease;
  }

  .menu-item:hover {
    border-color: var(--text-primary);
    background: var(--bg-primary);
    box-shadow: inset 0 0 20px rgba(0, 255, 65, 0.05);
  }

  .menu-item:hover .menu-label {
    color: var(--text-bright);
  }

  .menu-item:hover .menu-arrow {
    color: var(--text-primary);
    transform: translateX(4px);
  }

  .menu-key {
    font-size: 0.8rem;
    color: var(--accent-amber);
    font-weight: 600;
  }

  .menu-icon {
    font-size: 0.75rem;
    color: var(--text-dim);
    min-width: 40px;
  }

  .menu-content {
    flex: 1;
    display: flex;
    flex-direction: column;
    gap: 0.1rem;
  }

  .menu-label {
    font-weight: 500;
    color: var(--text-primary);
  }

  .menu-desc {
    font-size: 0.75rem;
    color: var(--text-dim);
  }

  .menu-arrow {
    color: var(--text-dim);
    transition: all 0.15s ease;
  }

  /* Info Panel */
  .info-panel {
    font-size: 0.8rem;
    border-color: var(--border-dim);
    background: transparent;
  }

  .info-row {
    display: flex;
    gap: 0.5rem;
    margin-bottom: 0.25rem;
  }

  .info-label {
    color: var(--text-dim);
    min-width: 60px;
  }

  .info-value {
    color: var(--text-secondary);
  }

  .morse-demo {
    margin-top: 1rem;
    padding-top: 1rem;
    border-top: 1px solid var(--border-dim);
    color: var(--text-dim);
    font-size: 0.85rem;
    letter-spacing: 2px;
    text-align: center;
    animation: morsePulse 3s ease-in-out infinite;
  }

  @keyframes morsePulse {
    0%, 100% { opacity: 0.3; }
    50% { opacity: 0.7; }
  }
</style>
