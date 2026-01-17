<script lang="ts">
  import { onMount } from 'svelte';
  import System from './pages/System.svelte';
  import Config from './pages/Config.svelte';
  import Decoder from './pages/Decoder.svelte';
  import Home from './pages/Home.svelte';
  import Keyer from './pages/Keyer.svelte';
  import Timeline from './pages/Timeline.svelte';

  let currentPage = $state('/');
  let currentTime = $state('00:00:00');

  onMount(() => {
    currentPage = window.location.pathname;

    window.addEventListener('popstate', () => {
      currentPage = window.location.pathname;
    });

    // Update clock
    const updateClock = () => {
      const now = new Date();
      currentTime = now.toTimeString().split(' ')[0];
    };
    updateClock();
    const clockInterval = setInterval(updateClock, 1000);

    return () => clearInterval(clockInterval);
  });

  function navigate(path: string) {
    history.pushState({}, '', path);
    currentPage = path;
  }

  const navItems = [
    { path: '/', label: 'HOME', key: 'F1' },
    { path: '/config', label: 'CONFIG', key: 'F2' },
    { path: '/system', label: 'SYSTEM', key: 'F3' },
    { path: '/keyer', label: 'KEYER', key: 'F4' },
    { path: '/decoder', label: 'DECODER', key: 'F5' },
    { path: '/timeline', label: 'TIMELINE', key: 'F6' },
  ];
</script>

<div class="terminal">
  <div class="scanlines"></div>

  <header class="terminal-header">
    <div class="header-left">
      <span class="prompt">root@cwkeyer</span>
      <span class="separator">:</span>
      <span class="path">~{currentPage}</span>
      <span class="cursor">_</span>
    </div>
    <div class="header-right">
      <span class="status-indicator"></span>
      <span class="status-text">ONLINE</span>
      <span class="clock">[{currentTime}]</span>
    </div>
  </header>

  <nav class="terminal-nav">
    <div class="nav-border-top"></div>
    <div class="nav-items">
      {#each navItems as item}
        <a
          href={item.path}
          class="nav-item"
          class:active={currentPage === item.path}
          onclick={(e) => { e.preventDefault(); navigate(item.path); }}
        >
          <span class="nav-key">{item.key}</span>
          <span class="nav-label">{item.label}</span>
        </a>
      {/each}
    </div>
    <div class="nav-border-bottom"></div>
  </nav>

  <main class="terminal-main">
    {#if currentPage === '/system'}
      <System />
    {:else if currentPage === '/config'}
      <Config />
    {:else if currentPage === '/keyer'}
      <Keyer />
    {:else if currentPage === '/decoder'}
      <Decoder />
    {:else if currentPage === '/timeline'}
      <Timeline />
    {:else}
      <Home />
    {/if}
  </main>

  <footer class="terminal-footer">
    <span class="footer-left">RemoteKeyer IU3QEZ v1.0</span>
    <span class="footer-center">ESC:Back | TAB:Next | ENTER:Select</span>
    <span class="footer-right">ESP32-S3 | 240MHz</span>
  </footer>
</div>

<style>
  @import url('https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;500;600;700&display=swap');

  :global(*) {
    box-sizing: border-box;
    margin: 0;
    padding: 0;
  }

  :global(body) {
    font-family: 'JetBrains Mono', 'Fira Code', 'Consolas', monospace;
    background: #0a0a0a;
    color: #00ff41;
    line-height: 1.5;
    overflow-x: hidden;
  }

  :global(:root) {
    /* ══════════════════════════════════════════
       BACKGROUNDS - Dark scale
       ══════════════════════════════════════════ */
    --bg-primary: #0a0a0a;
    --bg-secondary: #0d1117;
    --bg-tertiary: #161b22;
    --bg-hover: #1c2128;
    --bg-card: #0d1117;

    /* ══════════════════════════════════════════
       TEXT - Green phosphor hierarchy
       ══════════════════════════════════════════ */
    --text-bright: #7fff7f;
    --text-primary: #00ff41;
    --text-secondary: #4ec968;
    --text-dim: #6b8f71;

    /* ══════════════════════════════════════════
       BORDERS - Structural elements
       ══════════════════════════════════════════ */
    --border-color: #00ff41;
    --border-dim: #2f5f3f;

    /* ══════════════════════════════════════════
       ACCENTS - Semantic colors
       ══════════════════════════════════════════ */
    --accent-green: #00ff41;
    --accent-amber: #ffb000;
    --accent-cyan: #00d4ff;
    --accent-red: #ff4757;
    --accent-magenta: #ff6bcb;

    /* ══════════════════════════════════════════
       GLOWS - CRT phosphor effects
       ══════════════════════════════════════════ */
    --glow-green: 0 0 10px #00ff41, 0 0 20px #00ff4133;
    --glow-amber: 0 0 10px #ffb000, 0 0 20px #ffb00033;
    --glow-cyan: 0 0 10px #00d4ff, 0 0 20px #00d4ff33;
  }

  .terminal {
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    position: relative;
    background:
      radial-gradient(ellipse at center, #0d1117 0%, #0a0a0a 100%);
  }

  /* CRT Scanlines Effect */
  .scanlines {
    position: fixed;
    top: 0;
    left: 0;
    width: 100%;
    height: 100%;
    pointer-events: none;
    z-index: 1000;
    background: repeating-linear-gradient(
      0deg,
      rgba(0, 0, 0, 0.1) 0px,
      rgba(0, 0, 0, 0.1) 1px,
      transparent 1px,
      transparent 2px
    );
  }

  /* Header */
  .terminal-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 0.75rem 1.5rem;
    background: var(--bg-secondary);
    border-bottom: 1px solid var(--border-dim);
    font-size: 0.9rem;
  }

  .header-left {
    display: flex;
    align-items: center;
    gap: 0;
  }

  .prompt {
    color: var(--accent-cyan);
    font-weight: 600;
  }

  .separator {
    color: var(--text-dim);
  }

  .path {
    color: var(--accent-amber);
  }

  .cursor {
    color: var(--text-primary);
    animation: blink 1s step-end infinite;
  }

  @keyframes blink {
    0%, 50% { opacity: 1; }
    51%, 100% { opacity: 0; }
  }

  .header-right {
    display: flex;
    align-items: center;
    gap: 0.75rem;
  }

  .status-indicator {
    width: 8px;
    height: 8px;
    background: var(--text-primary);
    border-radius: 50%;
    box-shadow: var(--glow-green);
    animation: pulse 2s ease-in-out infinite;
  }

  @keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.5; }
  }

  .status-text {
    color: var(--text-primary);
    font-size: 0.8rem;
    font-weight: 600;
    letter-spacing: 1px;
  }

  .clock {
    color: var(--text-dim);
    font-size: 0.85rem;
  }

  /* Navigation */
  .terminal-nav {
    background: var(--bg-secondary);
    border-bottom: 1px solid var(--border-dim);
  }

  .nav-border-top,
  .nav-border-bottom {
    height: 1px;
    background: linear-gradient(90deg,
      transparent 0%,
      var(--border-dim) 10%,
      var(--text-primary) 50%,
      var(--border-dim) 90%,
      transparent 100%
    );
  }

  .nav-items {
    display: flex;
    justify-content: center;
    gap: 0;
    padding: 0.5rem 1rem;
    flex-wrap: wrap;
  }

  .nav-item {
    display: flex;
    align-items: center;
    gap: 0.25rem;
    padding: 0.5rem 1rem;
    color: var(--text-secondary);
    text-decoration: none;
    font-size: 0.85rem;
    border: 1px solid transparent;
    transition: all 0.15s ease;
    position: relative;
  }

  .nav-item:hover {
    color: var(--text-bright);
    background: var(--bg-tertiary);
    border-color: var(--border-dim);
  }

  .nav-item.active {
    color: var(--bg-primary);
    background: var(--text-primary);
    border-color: var(--text-primary);
    text-shadow: none;
  }

  .nav-item.active .nav-key {
    color: var(--bg-primary);
    background: var(--bg-secondary);
  }

  .nav-key {
    font-size: 0.7rem;
    padding: 0.1rem 0.3rem;
    background: var(--bg-tertiary);
    border: 1px solid var(--border-dim);
    color: var(--text-dim);
    font-weight: 600;
  }

  .nav-label {
    font-weight: 500;
    letter-spacing: 0.5px;
  }

  /* Main Content */
  .terminal-main {
    flex: 1;
    padding: 1.5rem;
    max-width: 1400px;
    margin: 0 auto;
    width: 100%;
  }

  /* Footer */
  .terminal-footer {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 0.5rem 1.5rem;
    background: var(--bg-secondary);
    border-top: 1px solid var(--border-dim);
    font-size: 0.75rem;
    color: var(--text-dim);
  }

  .footer-center {
    color: var(--text-secondary);
  }

  /* Global Input Styles */
  :global(input[type="text"]),
  :global(input[type="password"]),
  :global(input[type="number"]),
  :global(select),
  :global(textarea) {
    font-family: 'JetBrains Mono', monospace;
    background: var(--bg-primary);
    border: 1px solid var(--border-dim);
    color: var(--text-primary);
    padding: 0.5rem 0.75rem;
    font-size: 0.9rem;
    transition: all 0.15s ease;
  }

  :global(input:focus),
  :global(select:focus),
  :global(textarea:focus) {
    outline: none;
    border-color: var(--text-primary);
    box-shadow: var(--glow-green);
  }

  :global(button) {
    font-family: 'JetBrains Mono', monospace;
    background: var(--bg-tertiary);
    border: 1px solid var(--text-primary);
    color: var(--text-primary);
    padding: 0.5rem 1rem;
    font-size: 0.85rem;
    cursor: pointer;
    transition: all 0.15s ease;
    text-transform: uppercase;
    letter-spacing: 0.5px;
  }

  :global(button:hover) {
    background: var(--text-primary);
    color: var(--bg-primary);
    box-shadow: var(--glow-green);
  }

  :global(button:active) {
    transform: scale(0.98);
  }

  :global(button.danger) {
    border-color: var(--accent-red);
    color: var(--accent-red);
  }

  :global(button.danger:hover) {
    background: var(--accent-red);
    color: var(--bg-primary);
  }

  /* Scrollbar */
  :global(::-webkit-scrollbar) {
    width: 8px;
    height: 8px;
  }

  :global(::-webkit-scrollbar-track) {
    background: var(--bg-primary);
  }

  :global(::-webkit-scrollbar-thumb) {
    background: var(--border-dim);
    border: 1px solid var(--bg-primary);
  }

  :global(::-webkit-scrollbar-thumb:hover) {
    background: var(--text-secondary);
  }

  /* Responsive */
  @media (max-width: 768px) {
    .terminal-header {
      flex-direction: column;
      gap: 0.5rem;
      text-align: center;
    }

    .nav-items {
      gap: 0.25rem;
    }

    .nav-item {
      padding: 0.4rem 0.6rem;
      font-size: 0.75rem;
    }

    .nav-key {
      display: none;
    }

    .terminal-footer {
      flex-direction: column;
      gap: 0.25rem;
      text-align: center;
    }

    .footer-left,
    .footer-right {
      display: none;
    }
  }
</style>
