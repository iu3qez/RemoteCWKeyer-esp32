<script lang="ts">
  import { api } from '../lib/api';
  import type { TextKeyerState, MemorySlot } from '../lib/types';
  import { onMount, onDestroy } from 'svelte';

  let inputText = $state('');
  let state: TextKeyerState = $state('IDLE');
  let sent = $state(0);
  let total = $state(0);
  let progress = $state(0);
  let memorySlots: MemorySlot[] = $state([]);
  let editingSlot: number | null = $state(null);
  let editText = $state('');
  let editLabel = $state('');
  let error = $state('');
  let pollInterval: ReturnType<typeof setInterval> | null = null;

  async function loadStatus() {
    try {
      const status = await api.getTextStatus();
      state = status.state;
      sent = status.sent;
      total = status.total;
      progress = status.progress;
    } catch (e) {
      console.error('Failed to load status:', e);
    }
  }

  async function loadMemorySlots() {
    try {
      const data = await api.getMemorySlots();
      memorySlots = data.slots;
    } catch (e) {
      console.error('Failed to load memory slots:', e);
    }
  }

  onMount(() => {
    loadStatus();
    loadMemorySlots();
    pollInterval = setInterval(loadStatus, 500);
  });

  onDestroy(() => {
    if (pollInterval) {
      clearInterval(pollInterval);
    }
  });

  async function handleSend() {
    if (!inputText.trim()) return;
    error = '';
    try {
      await api.sendText(inputText);
      inputText = '';
    } catch (e) {
      error = e instanceof Error ? e.message : 'Failed to send';
    }
  }

  async function handleAbort() {
    try {
      await api.abortText();
    } catch (e) {
      error = e instanceof Error ? e.message : 'Failed to abort';
    }
  }

  async function handlePause() {
    try {
      await api.pauseText();
    } catch (e) {
      error = e instanceof Error ? e.message : 'Failed to pause';
    }
  }

  async function handleResume() {
    try {
      await api.resumeText();
    } catch (e) {
      error = e instanceof Error ? e.message : 'Failed to resume';
    }
  }

  function handleClear() {
    inputText = '';
    error = '';
  }

  async function playSlot(slot: number) {
    error = '';
    try {
      await api.playMemorySlot(slot);
    } catch (e) {
      error = e instanceof Error ? e.message : 'Failed to play';
    }
  }

  function startEdit(slot: MemorySlot) {
    editingSlot = slot.slot;
    editText = slot.text;
    editLabel = slot.label;
  }

  function cancelEdit() {
    editingSlot = null;
    editText = '';
    editLabel = '';
  }

  async function saveEdit() {
    if (editingSlot === null) return;
    try {
      await api.setMemorySlot(editingSlot, editText, editLabel);
      await loadMemorySlots();
      cancelEdit();
    } catch (e) {
      error = e instanceof Error ? e.message : 'Failed to save';
    }
  }

  function handleKeydown(e: KeyboardEvent) {
    if (e.key === 'Escape' && editingSlot !== null) {
      cancelEdit();
    }
    if (e.key === 'Enter' && e.ctrlKey && inputText.trim()) {
      handleSend();
    }
  }
</script>

<svelte:window on:keydown={handleKeydown} />

<div class="keyer-page">
  <div class="page-header">
    <h1>/// TEXT KEYER</h1>
    <div class="header-status">
      <span class="status-badge" class:sending={state === 'SENDING'} class:paused={state === 'PAUSED'}>
        {state}
      </span>
    </div>
  </div>

  {#if error}
    <div class="error-banner">
      <span>{error}</span>
      <button class="error-dismiss" onclick={() => error = ''}>×</button>
    </div>
  {/if}

  {#if state !== 'IDLE'}
    <div class="progress-panel">
      <div class="progress-bar">
        <div class="progress-fill" style="width: {progress}%"></div>
      </div>
      <div class="progress-text">
        <span>{sent} / {total} characters</span>
        <span>{progress}%</span>
      </div>
    </div>
  {/if}

  <div class="keyer-panel">
    <div class="panel-header">
      <span class="panel-icon">[K]</span>
      <span class="panel-title">KEYBOARD INPUT</span>
    </div>

    <div class="input-area">
      <textarea
        bind:value={inputText}
        placeholder="Type your message here... (Ctrl+Enter to send)"
        rows="4"
        disabled={state === 'SENDING'}
      ></textarea>
    </div>

    <div class="input-controls">
      {#if state === 'IDLE'}
        <button class="send-btn" onclick={handleSend} disabled={!inputText.trim()}>
          <span class="btn-icon">▶</span>
          <span>SEND</span>
        </button>
      {:else if state === 'SENDING'}
        <button class="pause-btn" onclick={handlePause}>
          <span class="btn-icon">❚❚</span>
          <span>PAUSE</span>
        </button>
      {:else}
        <button class="resume-btn" onclick={handleResume}>
          <span class="btn-icon">▶</span>
          <span>RESUME</span>
        </button>
      {/if}
      <button class="stop-btn" onclick={handleAbort} disabled={state === 'IDLE'}>
        <span class="btn-icon">■</span>
        <span>ABORT</span>
      </button>
      <button class="clear-btn" onclick={handleClear} disabled={state === 'SENDING'}>
        <span class="btn-icon">⌫</span>
        <span>CLEAR</span>
      </button>
    </div>
  </div>

  <div class="keyer-panel">
    <div class="panel-header">
      <span class="panel-icon">[M]</span>
      <span class="panel-title">MEMORY SLOTS</span>
    </div>

    <div class="memory-slots">
      {#each memorySlots as slot}
        <div class="memory-slot" class:editing={editingSlot === slot.slot}>
          {#if editingSlot === slot.slot}
            <div class="edit-form">
              <input
                type="text"
                bind:value={editLabel}
                placeholder="Label"
                class="edit-label"
              />
              <textarea
                bind:value={editText}
                placeholder="Message text..."
                rows="2"
                class="edit-text"
              ></textarea>
              <div class="edit-buttons">
                <button class="save-btn" onclick={saveEdit}>Save</button>
                <button class="cancel-btn" onclick={cancelEdit}>Cancel</button>
              </div>
            </div>
          {:else}
            <div class="slot-header">
              <span class="slot-number">M{slot.slot + 1}</span>
              <span class="slot-label">{slot.label || '(empty)'}</span>
              <button class="edit-btn" onclick={() => startEdit(slot)}>✎</button>
            </div>
            <div class="slot-text">{slot.text || 'No message configured'}</div>
            <button
              class="play-btn"
              onclick={() => playSlot(slot.slot)}
              disabled={!slot.text || state !== 'IDLE'}
            >
              ▶ Play
            </button>
          {/if}
        </div>
      {/each}
    </div>
  </div>
</div>

<style>
  .keyer-page {
    max-width: 900px;
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
    background: var(--bg-tertiary);
    color: var(--text-dim);
    font-size: 0.7rem;
    font-weight: 700;
    letter-spacing: 1px;
  }

  .status-badge.sending {
    background: var(--accent-green);
    color: var(--bg-primary);
  }

  .status-badge.paused {
    background: var(--accent-amber);
    color: var(--bg-primary);
  }

  .error-banner {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0.75rem 1rem;
    background: rgba(255, 82, 82, 0.15);
    border: 1px solid var(--accent-red);
    color: var(--accent-red);
    margin-bottom: 1rem;
    font-size: 0.85rem;
  }

  .error-dismiss {
    background: none;
    border: none;
    color: var(--accent-red);
    font-size: 1.2rem;
    cursor: pointer;
    padding: 0;
    line-height: 1;
  }

  .progress-panel {
    background: var(--bg-secondary);
    border: 1px solid var(--border-dim);
    padding: 1rem;
    margin-bottom: 1rem;
  }

  .progress-bar {
    height: 8px;
    background: var(--bg-tertiary);
    border-radius: 4px;
    overflow: hidden;
    margin-bottom: 0.5rem;
  }

  .progress-fill {
    height: 100%;
    background: var(--accent-green);
    transition: width 0.2s ease;
  }

  .progress-text {
    display: flex;
    justify-content: space-between;
    font-size: 0.75rem;
    color: var(--text-dim);
  }

  .keyer-panel {
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
    color: var(--accent-cyan);
    font-weight: 700;
    font-size: 0.85rem;
  }

  .panel-title {
    color: var(--text-primary);
    font-size: 0.85rem;
    font-weight: 600;
    letter-spacing: 0.5px;
  }

  .input-area textarea {
    width: 100%;
    background: var(--bg-primary);
    border: 1px solid var(--border-dim);
    color: var(--text-primary);
    padding: 0.75rem;
    font-family: inherit;
    font-size: 1rem;
    resize: vertical;
    margin-bottom: 1rem;
  }

  .input-area textarea:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }

  .input-area textarea:focus {
    outline: none;
    border-color: var(--accent-cyan);
  }

  .input-controls {
    display: flex;
    gap: 0.5rem;
  }

  .send-btn,
  .stop-btn,
  .clear-btn,
  .pause-btn,
  .resume-btn {
    flex: 1;
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 0.4rem;
    padding: 0.6rem;
    font-size: 0.8rem;
    font-weight: 600;
    border: 1px solid var(--border-dim);
    background: var(--bg-tertiary);
    color: var(--text-primary);
    cursor: pointer;
    transition: all 0.2s ease;
  }

  .send-btn:hover:not(:disabled),
  .resume-btn:hover:not(:disabled) {
    background: var(--accent-green);
    color: var(--bg-primary);
    border-color: var(--accent-green);
  }

  .pause-btn:hover {
    background: var(--accent-amber);
    color: var(--bg-primary);
    border-color: var(--accent-amber);
  }

  .stop-btn:hover:not(:disabled) {
    background: var(--accent-red);
    color: var(--bg-primary);
    border-color: var(--accent-red);
  }

  .clear-btn:hover:not(:disabled) {
    background: var(--bg-primary);
    border-color: var(--text-dim);
  }

  .send-btn:disabled,
  .stop-btn:disabled,
  .clear-btn:disabled {
    opacity: 0.4;
    cursor: not-allowed;
  }

  .btn-icon {
    font-size: 0.9rem;
  }

  .memory-slots {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
    gap: 0.75rem;
  }

  .memory-slot {
    background: var(--bg-tertiary);
    border: 1px solid var(--border-dim);
    padding: 0.75rem;
  }

  .memory-slot.editing {
    border-color: var(--accent-cyan);
  }

  .slot-header {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    margin-bottom: 0.5rem;
  }

  .slot-number {
    font-size: 0.7rem;
    color: var(--accent-cyan);
    font-weight: 700;
    padding: 0.15rem 0.4rem;
    background: var(--bg-primary);
    border: 1px solid var(--border-dim);
  }

  .slot-label {
    font-weight: 600;
    color: var(--text-primary);
    flex: 1;
  }

  .edit-btn {
    background: none;
    border: none;
    color: var(--text-dim);
    cursor: pointer;
    padding: 0.25rem;
    font-size: 0.9rem;
  }

  .edit-btn:hover {
    color: var(--accent-cyan);
  }

  .slot-text {
    font-size: 0.8rem;
    color: var(--text-dim);
    margin-bottom: 0.75rem;
    min-height: 2rem;
    word-break: break-word;
  }

  .play-btn {
    width: 100%;
    padding: 0.5rem;
    background: var(--bg-primary);
    border: 1px solid var(--border-dim);
    color: var(--text-primary);
    font-size: 0.75rem;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.2s ease;
  }

  .play-btn:hover:not(:disabled) {
    background: var(--accent-green);
    color: var(--bg-primary);
    border-color: var(--accent-green);
  }

  .play-btn:disabled {
    opacity: 0.4;
    cursor: not-allowed;
  }

  .edit-form {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }

  .edit-label {
    padding: 0.5rem;
    background: var(--bg-primary);
    border: 1px solid var(--border-dim);
    color: var(--text-primary);
    font-size: 0.85rem;
  }

  .edit-text {
    padding: 0.5rem;
    background: var(--bg-primary);
    border: 1px solid var(--border-dim);
    color: var(--text-primary);
    font-size: 0.85rem;
    resize: vertical;
    font-family: inherit;
  }

  .edit-label:focus,
  .edit-text:focus {
    outline: none;
    border-color: var(--accent-cyan);
  }

  .edit-buttons {
    display: flex;
    gap: 0.5rem;
  }

  .save-btn,
  .cancel-btn {
    flex: 1;
    padding: 0.4rem;
    font-size: 0.75rem;
    font-weight: 600;
    border: 1px solid var(--border-dim);
    cursor: pointer;
  }

  .save-btn {
    background: var(--accent-green);
    color: var(--bg-primary);
    border-color: var(--accent-green);
  }

  .cancel-btn {
    background: var(--bg-primary);
    color: var(--text-primary);
  }

  @media (max-width: 768px) {
    .input-controls {
      flex-direction: column;
    }

    .memory-slots {
      grid-template-columns: 1fr;
    }
  }
</style>
