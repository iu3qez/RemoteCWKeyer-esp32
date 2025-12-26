<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { api } from '../lib/api';
  import type { DecoderStatus } from '../lib/types';

  let status = $state<DecoderStatus | null>(null);
  let decodedText = $state('');
  let currentWpm = $state(0);
  let error = $state<string | null>(null);
  let eventSource: EventSource | null = null;

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

  function connectStream() {
    eventSource = api.connectDecoderStream(
      (char, wpm) => {
        decodedText += char;
        currentWpm = wpm;
        // Keep last 500 chars
        if (decodedText.length > 500) {
          decodedText = decodedText.slice(-500);
        }
      },
      () => {
        decodedText += ' ';
      }
    );

    eventSource.onerror = () => {
      error = 'SSE connection lost';
    };
  }

  onMount(() => {
    refresh();
    connectStream();
  });

  onDestroy(() => {
    if (eventSource) {
      eventSource.close();
    }
  });
</script>

<div class="decoder-page">
  <h1>Morse Decoder</h1>

  {#if error}
    <div class="error">{error}</div>
  {/if}

  {#if status}
    <section class="card controls">
      <label>
        <input type="checkbox" checked={status.enabled} onchange={toggleEnabled} />
        Decoder Enabled
      </label>
      <span class="wpm">WPM: {currentWpm || status.wpm}</span>
    </section>
  {/if}

  <section class="card output">
    <h2>Decoded Text</h2>
    <pre>{decodedText || '(waiting for CW...)'}</pre>
    {#if status?.pattern}
      <div class="pattern">Current: {status.pattern}</div>
    {/if}
  </section>

  <button onclick={() => { decodedText = ''; }}>Clear</button>
</div>

<style>
  .decoder-page { max-width: 800px; }
  .card { background: #f5f5f5; padding: 1rem; margin: 1rem 0; border-radius: 8px; }
  .error { color: red; padding: 1rem; background: #fee; border-radius: 8px; }
  .controls { display: flex; justify-content: space-between; align-items: center; }
  .wpm { font-size: 1.2rem; font-weight: bold; }
  .output pre {
    background: #222;
    color: #0f0;
    padding: 1rem;
    font-family: monospace;
    font-size: 1.2rem;
    min-height: 200px;
    white-space: pre-wrap;
    word-wrap: break-word;
  }
  .pattern { color: #666; margin-top: 0.5rem; }
  button { padding: 0.5rem 1rem; cursor: pointer; }
</style>
