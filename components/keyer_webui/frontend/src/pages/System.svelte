<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { api } from '../lib/api';
  import type { SystemStats, DeviceStatus } from '../lib/types';

  let status = $state<DeviceStatus | null>(null);
  let stats = $state<SystemStats | null>(null);
  let error = $state<string | null>(null);
  let pollInterval: number | null = null;

  async function refresh() {
    try {
      [status, stats] = await Promise.all([
        api.getStatus(),
        api.getSystemStats()
      ]);
      error = null;
    } catch (e) {
      error = e instanceof Error ? e.message : 'Unknown error';
    }
  }

  async function handleReboot() {
    if (confirm('Reboot the device?')) {
      await api.reboot();
    }
  }

  onMount(() => {
    refresh();
    pollInterval = setInterval(refresh, 2000) as unknown as number;
  });

  onDestroy(() => {
    if (pollInterval) clearInterval(pollInterval);
  });
</script>

<div class="system-page">
  <h1>System Status</h1>

  {#if error}
    <div class="error">{error}</div>
  {/if}

  {#if status}
    <section class="card">
      <h2>Network</h2>
      <dl>
        <dt>Mode</dt><dd>{status.mode}</dd>
        <dt>IP</dt><dd>{status.ip}</dd>
        <dt>Ready</dt><dd>{status.ready ? 'Yes' : 'No'}</dd>
      </dl>
    </section>
  {/if}

  {#if stats}
    <section class="card">
      <h2>Uptime</h2>
      <p>{stats.uptime.hours}h {stats.uptime.minutes}m {stats.uptime.seconds}s</p>
    </section>

    <section class="card">
      <h2>Memory</h2>
      <dl>
        <dt>Free</dt><dd>{(stats.heap.free_bytes / 1024).toFixed(1)} KB</dd>
        <dt>Min Free</dt><dd>{(stats.heap.minimum_free_bytes / 1024).toFixed(1)} KB</dd>
        <dt>Largest Block</dt><dd>{(stats.heap.largest_free_block / 1024).toFixed(1)} KB</dd>
      </dl>
    </section>

    <section class="card">
      <h2>Tasks ({stats.tasks.length})</h2>
      <table>
        <thead>
          <tr><th>Name</th><th>State</th><th>Priority</th><th>Stack HWM</th></tr>
        </thead>
        <tbody>
          {#each stats.tasks as task}
            <tr>
              <td>{task.name}</td>
              <td>{task.state}</td>
              <td>{task.priority}</td>
              <td>{task.stack_hwm}</td>
            </tr>
          {/each}
        </tbody>
      </table>
    </section>
  {/if}

  <section class="card">
    <h2>Actions</h2>
    <button onclick={handleReboot}>Reboot</button>
  </section>
</div>

<style>
  .system-page { max-width: 800px; }
  .card { background: #f5f5f5; padding: 1rem; margin: 1rem 0; border-radius: 8px; }
  .error { color: red; padding: 1rem; background: #fee; border-radius: 8px; }
  dl { display: grid; grid-template-columns: auto 1fr; gap: 0.5rem; }
  dt { font-weight: bold; }
  table { width: 100%; border-collapse: collapse; }
  th, td { padding: 0.5rem; text-align: left; border-bottom: 1px solid #ddd; }
  button { padding: 0.5rem 1rem; cursor: pointer; }
</style>
