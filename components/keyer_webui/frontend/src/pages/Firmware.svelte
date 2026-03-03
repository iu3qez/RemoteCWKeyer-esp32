<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import { api } from '../lib/api';
  import type { FirmwareStatus } from '../lib/types';

  let fw = $state<FirmwareStatus | null>(null);
  let error = $state<string | null>(null);
  let pollInterval: number | null = null;

  // Upload state
  let selectedFile = $state<File | null>(null);
  let uploadProgress = $state(0);
  let uploading = $state(false);
  let dragOver = $state(false);

  // URL download state
  let firmwareUrl = $state('');

  function formatBytes(bytes: number): string {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / (1024 * 1024)).toFixed(2) + ' MB';
  }

  function stateLabel(state: string): string {
    switch (state) {
      case 'IDLE': return 'Idle';
      case 'UPLOADING': return 'Uploading...';
      case 'DOWNLOADING': return 'Downloading...';
      case 'DONE': return 'Complete';
      case 'ERROR': return 'Error';
      default: return state;
    }
  }

  function stateClass(state: string): string {
    switch (state) {
      case 'DONE': return 'state-done';
      case 'ERROR': return 'state-error';
      case 'UPLOADING':
      case 'DOWNLOADING': return 'state-active';
      default: return '';
    }
  }

  async function refresh() {
    try {
      fw = await api.getFirmwareStatus();
      error = null;
    } catch (e) {
      error = e instanceof Error ? e.message : 'Unknown error';
    }
  }

  function handleDragOver(e: DragEvent) {
    e.preventDefault();
    dragOver = true;
  }

  function handleDragLeave() {
    dragOver = false;
  }

  function handleDrop(e: DragEvent) {
    e.preventDefault();
    dragOver = false;
    const files = e.dataTransfer?.files;
    if (files && files.length > 0) {
      selectFile(files[0]);
    }
  }

  function handleFileInput(e: Event) {
    const target = e.target as HTMLInputElement;
    if (target.files && target.files.length > 0) {
      selectFile(target.files[0]);
    }
  }

  function selectFile(file: File) {
    if (!file.name.endsWith('.bin')) {
      error = 'Only .bin firmware files are supported';
      return;
    }
    selectedFile = file;
    error = null;
  }

  async function handleUpload() {
    if (!selectedFile) return;
    if (!confirm('>>> CONFIRM FIRMWARE UPLOAD <<<\n\nFlash new firmware from file:\n' + selectedFile.name + '\n\nContinue?')) return;

    uploading = true;
    uploadProgress = 0;
    error = null;
    try {
      await api.uploadFirmware(selectedFile, (pct) => {
        uploadProgress = pct;
      });
      selectedFile = null;
      uploadProgress = 0;
    } catch (e) {
      error = e instanceof Error ? e.message : 'Upload failed';
    } finally {
      uploading = false;
    }
  }

  async function handleUrlDownload() {
    if (!firmwareUrl.trim()) return;
    if (!confirm('>>> CONFIRM FIRMWARE DOWNLOAD <<<\n\nDownload and flash firmware from:\n' + firmwareUrl + '\n\nContinue?')) return;

    error = null;
    try {
      await api.downloadFirmwareFromUrl(firmwareUrl.trim());
      firmwareUrl = '';
    } catch (e) {
      error = e instanceof Error ? e.message : 'Download failed';
    }
  }

  async function handleConfirm() {
    try {
      await api.firmwareConfirm();
    } catch (e) {
      error = e instanceof Error ? e.message : 'Confirm failed';
    }
  }

  async function handleRollback() {
    if (!confirm('>>> CONFIRM FIRMWARE ROLLBACK <<<\n\nRoll back to previous firmware version.\nDevice will reboot. Continue?')) return;

    try {
      await api.firmwareRollback();
    } catch (e) {
      error = e instanceof Error ? e.message : 'Rollback failed';
    }
  }

  async function handleUf2Reboot() {
    if (!confirm('>>> CONFIRM USB BOOT MODE <<<\n\nReboot into UF2/USB firmware update mode.\nDevice will stop normal operation. Continue?')) return;

    try {
      await api.firmwareUf2Reboot();
    } catch (e) {
      error = e instanceof Error ? e.message : 'UF2 reboot failed';
    }
  }

  async function handleReboot() {
    if (!confirm('>>> CONFIRM REBOOT <<<\n\nReboot device with new firmware. Continue?')) return;

    try {
      await api.reboot();
    } catch (e) {
      error = e instanceof Error ? e.message : 'Reboot failed';
    }
  }

  onMount(() => {
    refresh();
    pollInterval = setInterval(refresh, 1000) as unknown as number;
  });

  onDestroy(() => {
    if (pollInterval) clearInterval(pollInterval);
  });

  let otaActive = $derived(fw !== null && fw.ota_state !== 'IDLE');
  let otaProgressPercent = $derived(fw ? fw.ota_progress : 0);
</script>

<div class="firmware-page">
  <div class="page-header">
    <h1>/// FIRMWARE UPDATE</h1>
  </div>

  {#if error}
    <div class="error-box">
      <span class="error-icon">[!]</span>
      <span class="error-text">ERROR: {error}</span>
    </div>
  {/if}

  <!-- Current Firmware Info -->
  <div class="panel info-panel">
    <div class="panel-header">
      <span class="panel-icon">[F]</span>
      <span class="panel-title">CURRENT FIRMWARE</span>
    </div>
    {#if fw}
      <div class="stat-rows">
        <div class="stat-row">
          <span class="stat-label">VERSION</span>
          <span class="stat-value highlight">{fw.version}</span>
        </div>
        <div class="stat-row">
          <span class="stat-label">BUILD_DATE</span>
          <span class="stat-value">{fw.date} {fw.time}</span>
        </div>
        <div class="stat-row">
          <span class="stat-label">IDF_VERSION</span>
          <span class="stat-value">{fw.idf_ver}</span>
        </div>
        <div class="stat-row">
          <span class="stat-label">ACTIVE_PART</span>
          <span class="stat-value">{fw.running_partition}</span>
        </div>
        <div class="stat-row">
          <span class="stat-label">NEXT_PART</span>
          <span class="stat-value">{fw.next_partition}</span>
        </div>
        <div class="stat-row">
          <span class="stat-label">ROLLBACK</span>
          <span class="stat-value" class:available={fw.rollback_available} class:unavailable={!fw.rollback_available}>
            {fw.rollback_available ? 'AVAILABLE' : 'N/A'}
          </span>
        </div>
        <div class="stat-row">
          <span class="stat-label">VERIFY</span>
          <span class="stat-value" class:pending={fw.pending_verify}>
            {fw.pending_verify ? 'PENDING CONFIRMATION' : 'CONFIRMED'}
          </span>
        </div>
      </div>
    {:else}
      <div class="loading">Loading...</div>
    {/if}
  </div>

  <!-- OTA Progress (shown only when active) -->
  {#if otaActive && fw}
    <div class="panel ota-panel">
      <div class="panel-header">
        <span class="panel-icon">[O]</span>
        <span class="panel-title">OTA PROGRESS</span>
      </div>
      <div class="ota-state">
        <span class="state-badge {stateClass(fw.ota_state)}">
          {stateLabel(fw.ota_state)}
        </span>
      </div>
      <div class="progress-container">
        <div class="progress-bar">
          <div
            class="progress-fill"
            class:error-fill={fw.ota_state === 'ERROR'}
            class:done-fill={fw.ota_state === 'DONE'}
            style="width: {otaProgressPercent}%"
          ></div>
        </div>
        <div class="progress-labels">
          <span>{otaProgressPercent}%</span>
          <span>
            {formatBytes(fw.ota_bytes_written)}
            {#if fw.ota_total_size > 0}
              / {formatBytes(fw.ota_total_size)}
            {/if}
          </span>
        </div>
      </div>
      {#if fw.ota_state === 'ERROR' && fw.ota_error}
        <div class="ota-error">
          <span class="error-icon">[!]</span> {fw.ota_error}
        </div>
      {/if}
      {#if fw.ota_state === 'DONE'}
        <div class="ota-actions">
          <button class="action-btn reboot-btn" onclick={handleReboot}>
            REBOOT NOW
          </button>
        </div>
      {/if}
    </div>
  {/if}

  <div class="grid-layout">
    <!-- Upload Firmware -->
    <div class="panel upload-panel">
      <div class="panel-header">
        <span class="panel-icon">[U]</span>
        <span class="panel-title">UPLOAD FIRMWARE</span>
      </div>
      <div
        class="drop-zone"
        class:drag-over={dragOver}
        class:has-file={selectedFile !== null}
        role="button"
        tabindex="0"
        ondragover={handleDragOver}
        ondragleave={handleDragLeave}
        ondrop={handleDrop}
        onclick={() => document.getElementById('file-input')?.click()}
        onkeydown={(e) => { if (e.key === 'Enter') document.getElementById('file-input')?.click(); }}
      >
        {#if selectedFile}
          <div class="file-info">
            <span class="file-name">{selectedFile.name}</span>
            <span class="file-size">{formatBytes(selectedFile.size)}</span>
          </div>
        {:else}
          <div class="drop-hint">
            <span class="drop-icon">[+]</span>
            <span>Drop .bin file here or click to browse</span>
          </div>
        {/if}
      </div>
      <input
        id="file-input"
        type="file"
        accept=".bin"
        style="display: none"
        onchange={handleFileInput}
      />
      {#if uploading}
        <div class="upload-progress">
          <div class="progress-bar">
            <div class="progress-fill" style="width: {uploadProgress}%"></div>
          </div>
          <span class="upload-pct">{uploadProgress}%</span>
        </div>
      {/if}
      <button
        class="action-btn flash-btn"
        onclick={handleUpload}
        disabled={!selectedFile || uploading}
      >
        FLASH FIRMWARE
      </button>
    </div>

    <!-- Download from URL -->
    <div class="panel url-panel">
      <div class="panel-header">
        <span class="panel-icon">[D]</span>
        <span class="panel-title">DOWNLOAD FROM URL</span>
      </div>
      <div class="url-input-group">
        <input
          type="text"
          placeholder="https://example.com/firmware.bin"
          bind:value={firmwareUrl}
          class="url-input"
        />
        <button
          class="action-btn"
          onclick={handleUrlDownload}
          disabled={!firmwareUrl.trim()}
        >
          DOWNLOAD & FLASH
        </button>
      </div>
    </div>
  </div>

  <div class="grid-layout">
    <!-- USB Boot Mode -->
    <div class="panel usb-panel">
      <div class="panel-header">
        <span class="panel-icon">[B]</span>
        <span class="panel-title">USB BOOT MODE</span>
      </div>
      <p class="panel-desc">Reboot into UF2/USB mode for direct USB firmware flashing.</p>
      <button class="action-btn danger" onclick={handleUf2Reboot}>
        REBOOT TO USB MODE
      </button>
      <div class="actions-warning">
        Warning: Device will stop normal operation
      </div>
    </div>

    <!-- Actions -->
    <div class="panel actions-panel">
      <div class="panel-header">
        <span class="panel-icon">[A]</span>
        <span class="panel-title">ACTIONS</span>
      </div>
      {#if fw?.pending_verify}
        <button class="action-btn confirm-btn" onclick={handleConfirm}>
          CONFIRM FIRMWARE
        </button>
        <div class="actions-warning">
          Firmware is pending verification. Confirm to keep this version.
        </div>
      {/if}
      {#if fw?.rollback_available}
        <button class="action-btn danger" onclick={handleRollback}>
          ROLLBACK FIRMWARE
        </button>
        <div class="actions-warning">
          Revert to previous firmware version. Device will reboot.
        </div>
      {/if}
      {#if fw && !fw.pending_verify && !fw.rollback_available}
        <div class="loading">No actions available</div>
      {/if}
    </div>
  </div>
</div>

<style>
  .firmware-page {
    max-width: 1200px;
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
    margin-bottom: 1rem;
  }

  .grid-layout .panel {
    margin-bottom: 0;
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

  .panel-desc {
    font-size: 0.8rem;
    color: var(--text-dim);
    margin-bottom: 1rem;
    line-height: 1.4;
  }

  /* Stat rows */
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

  .stat-value.highlight {
    color: var(--accent-cyan);
    font-weight: 700;
  }

  .stat-value.available {
    color: var(--text-primary);
  }

  .stat-value.unavailable {
    color: var(--text-dim);
  }

  .stat-value.pending {
    color: var(--accent-amber);
    font-weight: 700;
  }

  /* OTA Panel */
  .ota-panel {
    border-color: var(--accent-amber);
  }

  .ota-state {
    margin-bottom: 1rem;
    text-align: center;
  }

  .state-badge {
    display: inline-block;
    padding: 0.25rem 0.75rem;
    font-size: 0.8rem;
    font-weight: 600;
    letter-spacing: 0.5px;
    border: 1px solid;
    color: var(--text-dim);
    border-color: var(--border-dim);
  }

  .state-active {
    color: var(--accent-amber);
    border-color: var(--accent-amber);
    background: rgba(255, 176, 0, 0.1);
  }

  .state-done {
    color: var(--text-primary);
    border-color: var(--text-primary);
    background: rgba(0, 255, 65, 0.1);
  }

  .state-error {
    color: var(--accent-red);
    border-color: var(--accent-red);
    background: rgba(255, 51, 51, 0.1);
  }

  /* Progress bar */
  .progress-container {
    margin-bottom: 1rem;
  }

  .progress-bar {
    height: 20px;
    background: var(--bg-tertiary);
    border: 1px solid var(--border-dim);
    overflow: hidden;
    margin-bottom: 0.25rem;
  }

  .progress-fill {
    height: 100%;
    background: var(--text-primary);
    transition: width 0.3s ease;
    box-shadow: var(--glow-green);
  }

  .progress-fill.error-fill {
    background: var(--accent-red);
    box-shadow: none;
  }

  .progress-fill.done-fill {
    background: var(--text-primary);
    box-shadow: var(--glow-green);
  }

  .progress-labels {
    display: flex;
    justify-content: space-between;
    font-size: 0.7rem;
    color: var(--text-dim);
  }

  .ota-error {
    padding: 0.75rem;
    background: rgba(255, 51, 51, 0.1);
    border: 1px solid var(--accent-red);
    color: var(--accent-red);
    font-size: 0.8rem;
    margin-bottom: 1rem;
  }

  .ota-actions {
    text-align: center;
  }

  /* Drop zone */
  .drop-zone {
    border: 2px dashed var(--border-dim);
    padding: 2rem 1rem;
    text-align: center;
    cursor: pointer;
    transition: all 0.15s ease;
    margin-bottom: 1rem;
  }

  .drop-zone:hover,
  .drop-zone.drag-over {
    border-color: var(--text-primary);
    background: rgba(0, 255, 65, 0.05);
  }

  .drop-zone.has-file {
    border-color: var(--accent-cyan);
    border-style: solid;
    background: rgba(0, 212, 255, 0.05);
  }

  .drop-hint {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 0.5rem;
    color: var(--text-dim);
    font-size: 0.8rem;
  }

  .drop-icon {
    font-size: 1.5rem;
    color: var(--text-secondary);
  }

  .file-info {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
  }

  .file-name {
    color: var(--accent-cyan);
    font-weight: 600;
    font-size: 0.9rem;
  }

  .file-size {
    color: var(--text-dim);
    font-size: 0.75rem;
  }

  /* Upload progress */
  .upload-progress {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    margin-bottom: 1rem;
  }

  .upload-progress .progress-bar {
    flex: 1;
  }

  .upload-pct {
    font-size: 0.8rem;
    color: var(--text-primary);
    font-weight: 600;
    min-width: 3rem;
    text-align: right;
  }

  /* URL input */
  .url-input-group {
    display: flex;
    flex-direction: column;
    gap: 0.75rem;
  }

  .url-input {
    width: 100%;
  }

  /* Action buttons */
  .action-btn {
    display: flex;
    align-items: center;
    justify-content: center;
    width: 100%;
    padding: 0.75rem;
  }

  .action-btn:disabled {
    opacity: 0.4;
    cursor: not-allowed;
  }

  .action-btn:disabled:hover {
    background: var(--bg-tertiary);
    color: var(--text-primary);
    box-shadow: none;
  }

  .action-btn.danger {
    border-color: var(--accent-red);
    color: var(--accent-red);
  }

  .action-btn.danger:hover {
    background: var(--accent-red);
    color: var(--bg-primary);
  }

  .action-btn.danger:disabled:hover {
    background: var(--bg-tertiary);
    color: var(--accent-red);
  }

  .confirm-btn {
    border-color: var(--accent-amber);
    color: var(--accent-amber);
    margin-bottom: 0.5rem;
  }

  .confirm-btn:hover {
    background: var(--accent-amber);
    color: var(--bg-primary);
    box-shadow: var(--glow-amber);
  }

  .reboot-btn {
    border-color: var(--accent-cyan);
    color: var(--accent-cyan);
  }

  .reboot-btn:hover {
    background: var(--accent-cyan);
    color: var(--bg-primary);
    box-shadow: var(--glow-cyan);
  }

  .flash-btn {
    border-color: var(--accent-cyan);
    color: var(--accent-cyan);
  }

  .flash-btn:hover {
    background: var(--accent-cyan);
    color: var(--bg-primary);
    box-shadow: var(--glow-cyan);
  }

  .flash-btn:disabled:hover {
    background: var(--bg-tertiary);
    color: var(--accent-cyan);
    box-shadow: none;
  }

  .actions-warning {
    font-size: 0.7rem;
    color: var(--text-dim);
    text-align: center;
    margin-top: 0.5rem;
  }

  .loading {
    color: var(--text-dim);
    font-style: italic;
    text-align: center;
    padding: 1rem;
  }

  @media (max-width: 768px) {
    .grid-layout {
      grid-template-columns: 1fr;
    }
  }
</style>
