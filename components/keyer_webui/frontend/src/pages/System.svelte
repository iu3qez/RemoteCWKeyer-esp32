<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { api } from '../lib/api';
  import type { SystemStats, DeviceStatus, VpnStatus } from '../lib/types';

  let status = $state<DeviceStatus | null>(null);
  let stats = $state<SystemStats | null>(null);
  let vpnStatus = $state<VpnStatus | null>(null);
  let error = $state<string | null>(null);
  let pollInterval: number | null = null;
  let autoRefresh = $state(true);
  let lastUpdate = $state('--:--:--');

  function formatBytes(bytes: number): string {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / (1024 * 1024)).toFixed(2) + ' MB';
  }

  function getStateClass(state: string): string {
    if (state === 'Running') return 'state-running';
    if (state === 'Blocked') return 'state-blocked';
    if (state === 'Suspended') return 'state-suspended';
    if (state === 'Ready') return 'state-ready';
    return '';
  }

  async function refresh() {
    try {
      [status, stats, vpnStatus] = await Promise.all([
        api.getStatus(),
        api.getSystemStats(),
        api.getVpnStatus().catch(() => null)
      ]);
      error = null;
      lastUpdate = new Date().toTimeString().split(' ')[0];
    } catch (e) {
      error = e instanceof Error ? e.message : 'Unknown error';
    }
  }

  async function handleReboot() {
    if (confirm('>>> CONFIRM SYSTEM REBOOT <<<\n\nDevice will restart. Continue?')) {
      await api.reboot();
    }
  }

  function toggleAutoRefresh() {
    autoRefresh = !autoRefresh;
    if (autoRefresh) {
      pollInterval = setInterval(refresh, 2000) as unknown as number;
    } else if (pollInterval) {
      clearInterval(pollInterval);
      pollInterval = null;
    }
  }

  onMount(() => {
    refresh();
    pollInterval = setInterval(refresh, 2000) as unknown as number;
  });

  onDestroy(() => {
    if (pollInterval) clearInterval(pollInterval);
  });

  let memoryUsedPercent = $derived(stats
    ? ((stats.heap.total_bytes - stats.heap.free_bytes) / stats.heap.total_bytes * 100)
    : 0);

  let uptimeFormatted = $derived(stats
    ? `${String(stats.uptime.hours).padStart(2, '0')}:${String(stats.uptime.minutes).padStart(2, '0')}:${String(stats.uptime.seconds).padStart(2, '0')}`
    : '--:--:--');
</script>

<div class="system-page">
  <div class="page-header">
    <h1>/// SYSTEM MONITOR</h1>
    <div class="header-controls">
      <button class="control-btn" class:active={autoRefresh} onclick={toggleAutoRefresh}>
        {autoRefresh ? '● AUTO' : '○ PAUSED'}
      </button>
      <button class="control-btn" onclick={refresh}>↻ REFRESH</button>
      <span class="last-update">Last: {lastUpdate}</span>
    </div>
  </div>

  {#if error}
    <div class="error-box">
      <span class="error-icon">[!]</span>
      <span class="error-text">ERROR: {error}</span>
    </div>
  {/if}

  <div class="grid-layout">
    <!-- Uptime Panel -->
    <div class="panel uptime-panel">
      <div class="panel-header">
        <span class="panel-icon">[T]</span>
        <span class="panel-title">UPTIME</span>
      </div>
      <div class="uptime-display">
        <span class="uptime-value">{uptimeFormatted}</span>
        <span class="uptime-label">HH:MM:SS</span>
      </div>
      {#if stats}
        <div class="uptime-total">
          Total: {stats.uptime.total_seconds.toLocaleString()} seconds
        </div>
      {/if}
    </div>

    <!-- Network Panel -->
    <div class="panel network-panel">
      <div class="panel-header">
        <span class="panel-icon">[N]</span>
        <span class="panel-title">NETWORK</span>
      </div>
      {#if status}
        <div class="stat-rows">
          <div class="stat-row">
            <span class="stat-label">MODE</span>
            <span class="stat-value">{status.mode}</span>
          </div>
          <div class="stat-row">
            <span class="stat-label">IP</span>
            <span class="stat-value ip">{status.ip}</span>
          </div>
          <div class="stat-row">
            <span class="stat-label">STATUS</span>
            <span class="stat-value" class:online={status.ready}>
              {status.ready ? '● ONLINE' : '○ OFFLINE'}
            </span>
          </div>
        </div>
      {:else}
        <div class="loading">Loading...</div>
      {/if}
    </div>

    <!-- VPN Panel -->
    {#if vpnStatus && vpnStatus.state !== 'DISABLED'}
      <div class="panel vpn-panel">
        <div class="panel-header">
          <span class="panel-icon">[V]</span>
          <span class="panel-title">VPN</span>
        </div>
        <div class="stat-rows">
          <div class="stat-row">
            <span class="stat-label">STATE</span>
            <span class="stat-value" class:online={vpnStatus.connected}>
              {vpnStatus.connected ? '● ' : '○ '}{vpnStatus.state}
            </span>
          </div>
          {#if vpnStatus.stats}
            <div class="stat-row">
              <span class="stat-label">HANDSHAKES</span>
              <span class="stat-value">{vpnStatus.stats.handshakes}</span>
            </div>
          {/if}
        </div>
      </div>
    {/if}

    <!-- Memory Panel -->
    <div class="panel memory-panel">
      <div class="panel-header">
        <span class="panel-icon">[M]</span>
        <span class="panel-title">HEAP MEMORY</span>
      </div>
      {#if stats}
        <div class="memory-bar-container">
          <div class="memory-bar">
            <div
              class="memory-bar-fill"
              style="width: {memoryUsedPercent}%"
              class:warning={memoryUsedPercent > 70}
              class:critical={memoryUsedPercent > 90}
            ></div>
          </div>
          <div class="memory-bar-labels">
            <span>0%</span>
            <span class="memory-percent">{memoryUsedPercent.toFixed(1)}% USED</span>
            <span>100%</span>
          </div>
        </div>
        <div class="stat-rows">
          <div class="stat-row">
            <span class="stat-label">FREE</span>
            <span class="stat-value">{formatBytes(stats.heap.free_bytes)}</span>
          </div>
          <div class="stat-row">
            <span class="stat-label">MIN_FREE</span>
            <span class="stat-value">{formatBytes(stats.heap.minimum_free_bytes)}</span>
          </div>
          <div class="stat-row">
            <span class="stat-label">TOTAL</span>
            <span class="stat-value">{formatBytes(stats.heap.total_bytes)}</span>
          </div>
          <div class="stat-row">
            <span class="stat-label">MAX_BLOCK</span>
            <span class="stat-value">{formatBytes(stats.heap.largest_free_block)}</span>
          </div>
        </div>
      {:else}
        <div class="loading">Loading...</div>
      {/if}
    </div>
  </div>

  <!-- Tasks Table -->
  <div class="panel tasks-panel">
    <div class="panel-header">
      <span class="panel-icon">[P]</span>
      <span class="panel-title">PROCESS LIST</span>
      {#if stats}
        <span class="task-count">{stats.tasks.length} tasks</span>
      {/if}
    </div>
    {#if stats && stats.tasks.length > 0}
      <div class="table-container">
        <table>
          <thead>
            <tr>
              <th>PID</th>
              <th>TASK_NAME</th>
              <th>STATE</th>
              <th>PRI</th>
              <th>STACK_HWM</th>
            </tr>
          </thead>
          <tbody>
            {#each stats.tasks as task, i}
              <tr>
                <td class="pid">{String(i + 1).padStart(3, '0')}</td>
                <td class="name">{task.name}</td>
                <td>
                  <span class="state-badge {getStateClass(task.state)}">
                    {task.state}
                  </span>
                </td>
                <td class="priority">{task.priority}</td>
                <td class="stack">{task.stack_hwm}</td>
              </tr>
            {/each}
          </tbody>
        </table>
      </div>
    {:else}
      <div class="loading">No tasks available</div>
    {/if}
  </div>

  <!-- Actions Panel -->
  <div class="panel actions-panel">
    <div class="panel-header">
      <span class="panel-icon">[!]</span>
      <span class="panel-title">SYSTEM ACTIONS</span>
    </div>
    <div class="actions-row">
      <button class="action-btn danger" onclick={handleReboot}>
        <span class="btn-icon">⚡</span>
        <span class="btn-label">REBOOT SYSTEM</span>
      </button>
    </div>
    <div class="actions-warning">
      Warning: Reboot will disconnect all active connections
    </div>
  </div>
</div>

<style>
  .system-page {
    max-width: 1200px;
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

  .header-controls {
    display: flex;
    align-items: center;
    gap: 0.75rem;
  }

  .control-btn {
    padding: 0.4rem 0.8rem;
    font-size: 0.75rem;
  }

  .control-btn.active {
    color: var(--text-primary);
    border-color: var(--text-primary);
  }

  .last-update {
    font-size: 0.75rem;
    color: var(--text-dim);
  }

  .error-box {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    padding: 1rem;
    background: rgba(255, 51, 51, 0.1);
    border: 1px solid var(--accent-red);
    margin-bottom: 1.5rem;
  }

  .error-icon {
    color: var(--accent-red);
    font-weight: 700;
  }

  .error-text {
    color: var(--accent-red);
    font-size: 0.9rem;
  }

  .grid-layout {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
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

  .task-count {
    margin-left: auto;
    font-size: 0.75rem;
    color: var(--text-dim);
  }

  /* Uptime */
  .uptime-display {
    text-align: center;
    padding: 1rem 0;
  }

  .uptime-value {
    display: block;
    font-size: 2.5rem;
    font-weight: 700;
    color: var(--accent-cyan);
    text-shadow: var(--glow-cyan);
    letter-spacing: 2px;
  }

  .uptime-label {
    font-size: 0.7rem;
    color: var(--text-dim);
    letter-spacing: 2px;
  }

  .uptime-total {
    text-align: center;
    font-size: 0.75rem;
    color: var(--text-dim);
    margin-top: 0.5rem;
  }

  /* Stats */
  .stat-rows {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }

  .stat-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 0.4rem 0;
    border-bottom: 1px dashed var(--border-dim);
  }

  .stat-row:last-child {
    border-bottom: none;
  }

  .stat-label {
    font-size: 0.75rem;
    color: var(--text-dim);
    letter-spacing: 0.5px;
  }

  .stat-value {
    font-size: 0.9rem;
    color: var(--text-secondary);
    font-weight: 500;
  }

  .stat-value.ip {
    color: var(--accent-amber);
  }

  .stat-value.online {
    color: var(--text-primary);
  }

  /* Memory Bar */
  .memory-bar-container {
    margin-bottom: 1rem;
  }

  .memory-bar {
    height: 20px;
    background: var(--bg-tertiary);
    border: 1px solid var(--border-dim);
    overflow: hidden;
  }

  .memory-bar-fill {
    height: 100%;
    background: var(--text-primary);
    transition: width 0.3s ease;
    box-shadow: var(--glow-green);
  }

  .memory-bar-fill.warning {
    background: var(--accent-amber);
    box-shadow: var(--glow-amber);
  }

  .memory-bar-fill.critical {
    background: var(--accent-red);
  }

  .memory-bar-labels {
    display: flex;
    justify-content: space-between;
    font-size: 0.7rem;
    color: var(--text-dim);
    margin-top: 0.25rem;
  }

  .memory-percent {
    color: var(--text-secondary);
    font-weight: 600;
  }

  /* Tasks Table */
  .tasks-panel {
    margin-bottom: 1rem;
  }

  .table-container {
    overflow-x: auto;
  }

  table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.8rem;
  }

  th {
    text-align: left;
    padding: 0.5rem;
    color: var(--text-dim);
    font-weight: 600;
    font-size: 0.7rem;
    letter-spacing: 0.5px;
    border-bottom: 1px solid var(--border-dim);
    background: var(--bg-tertiary);
  }

  td {
    padding: 0.5rem;
    border-bottom: 1px solid var(--border-dim);
    color: var(--text-secondary);
  }

  tr:hover {
    background: var(--bg-tertiary);
  }

  .pid {
    color: var(--text-dim);
    font-size: 0.75rem;
  }

  .name {
    color: var(--text-primary);
    font-weight: 500;
  }

  .priority {
    color: var(--accent-cyan);
    text-align: center;
  }

  .stack {
    color: var(--accent-amber);
    text-align: right;
  }

  .state-badge {
    display: inline-block;
    padding: 0.15rem 0.4rem;
    font-size: 0.7rem;
    font-weight: 600;
    letter-spacing: 0.5px;
    border: 1px solid;
  }

  .state-running {
    color: var(--text-primary);
    border-color: var(--text-primary);
    background: rgba(0, 255, 65, 0.1);
  }

  .state-blocked {
    color: var(--accent-amber);
    border-color: var(--accent-amber);
    background: rgba(255, 176, 0, 0.1);
  }

  .state-suspended {
    color: var(--accent-red);
    border-color: var(--accent-red);
    background: rgba(255, 51, 51, 0.1);
  }

  .state-ready {
    color: var(--accent-cyan);
    border-color: var(--accent-cyan);
    background: rgba(0, 212, 255, 0.1);
  }

  /* Actions */
  .actions-row {
    margin-bottom: 0.75rem;
  }

  .action-btn {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    width: 100%;
    justify-content: center;
    padding: 0.75rem;
  }

  .action-btn.danger {
    border-color: var(--accent-red);
    color: var(--accent-red);
  }

  .action-btn.danger:hover {
    background: var(--accent-red);
    color: var(--bg-primary);
  }

  .btn-icon {
    font-size: 1rem;
  }

  .actions-warning {
    font-size: 0.7rem;
    color: var(--text-dim);
    text-align: center;
  }

  .loading {
    color: var(--text-dim);
    font-style: italic;
    text-align: center;
    padding: 1rem;
  }

  @media (max-width: 768px) {
    .page-header {
      flex-direction: column;
      align-items: flex-start;
    }

    .grid-layout {
      grid-template-columns: 1fr;
    }

    .uptime-value {
      font-size: 2rem;
    }

    table {
      font-size: 0.7rem;
    }

    th, td {
      padding: 0.35rem;
    }
  }
</style>
