<script lang="ts">
  let inputText = $state('');
  let sending = $state(false);

  const quickMessages = [
    { key: 'F1', label: 'CQ', text: 'CQ CQ CQ DE IU3QEZ IU3QEZ K' },
    { key: 'F2', label: 'RST', text: '599 599' },
    { key: 'F3', label: 'TU', text: 'TU 73 DE IU3QEZ SK' },
    { key: 'F4', label: 'QTH', text: 'QTH ITALY ITALY' },
  ];

  function handleSend() {
    if (!inputText.trim()) return;
    sending = true;
    // TODO: Implement actual sending via API
    console.log('Sending:', inputText);
    setTimeout(() => {
      sending = false;
      inputText = '';
    }, 1000);
  }

  function sendQuickMessage(text: string) {
    inputText = text;
    handleSend();
  }
</script>

<div class="keyer-page">
  <div class="page-header">
    <h1>/// TEXT KEYER</h1>
    <div class="header-status">
      <span class="status-badge">COMING SOON</span>
    </div>
  </div>

  <div class="coming-soon-banner">
    <div class="banner-icon">⚠</div>
    <div class="banner-text">
      <strong>Feature Under Development</strong>
      <span>Text keyer functionality will be available in a future update</span>
    </div>
  </div>

  <div class="preview-panel">
    <div class="panel-header">
      <span class="panel-icon">[K]</span>
      <span class="panel-title">KEYBOARD INPUT</span>
      <span class="panel-badge">PREVIEW</span>
    </div>

    <div class="input-area">
      <textarea
        bind:value={inputText}
        placeholder="TYPE YOUR MESSAGE HERE..."
        rows="4"
        disabled
      ></textarea>
    </div>

    <div class="input-controls">
      <button class="send-btn" disabled>
        <span class="btn-icon">▶</span>
        <span>SEND</span>
      </button>
      <button class="stop-btn" disabled>
        <span class="btn-icon">■</span>
        <span>STOP</span>
      </button>
      <button class="clear-btn" disabled>
        <span class="btn-icon">⌫</span>
        <span>CLEAR</span>
      </button>
    </div>
  </div>

  <div class="preview-panel">
    <div class="panel-header">
      <span class="panel-icon">[M]</span>
      <span class="panel-title">QUICK MESSAGES</span>
    </div>

    <div class="quick-buttons">
      {#each quickMessages as msg}
        <button class="quick-btn" disabled>
          <span class="quick-key">{msg.key}</span>
          <span class="quick-label">{msg.label}</span>
          <span class="quick-preview">{msg.text.substring(0, 20)}...</span>
        </button>
      {/each}
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
        <span>Real-time text to CW conversion</span>
      </li>
      <li>
        <span class="feature-icon">○</span>
        <span>Configurable quick message slots (F1-F12)</span>
      </li>
      <li>
        <span class="feature-icon">○</span>
        <span>Keyboard shortcuts for instant sending</span>
      </li>
      <li>
        <span class="feature-icon">○</span>
        <span>Message queue with priority control</span>
      </li>
      <li>
        <span class="feature-icon">○</span>
        <span>Contest mode with auto-increment serial numbers</span>
      </li>
      <li>
        <span class="feature-icon">○</span>
        <span>Prosign support (AR, SK, BT, etc.)</span>
      </li>
    </ul>
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
  .features-panel {
    background: var(--bg-secondary);
    border: 1px solid var(--border-dim);
    padding: 1rem;
    margin-bottom: 1rem;
    opacity: 0.7;
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

  .input-controls {
    display: flex;
    gap: 0.5rem;
  }

  .send-btn,
  .stop-btn,
  .clear-btn {
    flex: 1;
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 0.4rem;
    padding: 0.6rem;
    font-size: 0.8rem;
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

  .quick-buttons {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
    gap: 0.5rem;
  }

  .quick-btn {
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    padding: 0.75rem;
    background: var(--bg-tertiary);
    text-align: left;
  }

  .quick-btn:disabled {
    opacity: 0.4;
    cursor: not-allowed;
  }

  .quick-key {
    font-size: 0.7rem;
    color: var(--accent-cyan);
    margin-bottom: 0.25rem;
  }

  .quick-label {
    font-weight: 600;
    color: var(--text-primary);
    margin-bottom: 0.25rem;
  }

  .quick-preview {
    font-size: 0.7rem;
    color: var(--text-dim);
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
    .input-controls {
      flex-direction: column;
    }

    .quick-buttons {
      grid-template-columns: 1fr;
    }
  }
</style>
