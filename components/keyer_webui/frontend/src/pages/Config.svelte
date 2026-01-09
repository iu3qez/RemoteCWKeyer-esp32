<script lang="ts">
  import { onMount } from 'svelte';
  import { api } from '../lib/api';
  import type { ConfigSchema, ConfigValues, ParameterMeta } from '../lib/types';

  let schema = $state<ConfigSchema | null>(null);
  let values = $state<ConfigValues>({});
  let error = $state<string | null>(null);
  let saving = $state(false);
  let activeFamily = $state<string>('');
  let dirtyParams = $state<Set<string>>(new Set());
  let successMessage = $state<string | null>(null);

  // Group parameters by family (first part of name)
  function groupByFamily(params: ParameterMeta[]): Map<string, ParameterMeta[]> {
    const groups = new Map<string, ParameterMeta[]>();
    for (const p of params) {
      const family = p.name.split('.')[0];
      if (!groups.has(family)) groups.set(family, []);
      groups.get(family)!.push(p);
    }
    return groups;
  }

  function getValue(param: ParameterMeta): number | boolean | string {
    const [family, name] = param.name.split('.');
    return values[family]?.[name] ?? 0;
  }

  async function handleChange(param: ParameterMeta, value: number | boolean | string) {
    try {
      await api.setParameter(param.name, value);
      // Update local state
      const [family, name] = param.name.split('.');
      if (!values[family]) values[family] = {};
      values[family][name] = value;
      dirtyParams.add(param.name);
      dirtyParams = new Set(dirtyParams);
      error = null;
    } catch (e) {
      error = e instanceof Error ? e.message : 'Failed to set parameter';
    }
  }

  async function handleSave() {
    saving = true;
    try {
      await api.saveConfig();
      error = null;
      dirtyParams.clear();
      dirtyParams = new Set(dirtyParams);
      successMessage = 'Configuration saved to NVS';
      setTimeout(() => successMessage = null, 3000);
    } catch (e) {
      error = e instanceof Error ? e.message : 'Failed to save';
    }
    saving = false;
  }

  onMount(async () => {
    try {
      [schema, values] = await Promise.all([
        api.getSchema(),
        api.getConfig()
      ]);
      if (schema && schema.parameters.length > 0) {
        const firstFamily = schema.parameters[0].name.split('.')[0];
        activeFamily = firstFamily;
      }
    } catch (e) {
      error = e instanceof Error ? e.message : 'Failed to load config';
    }
  });

  let families = $derived(schema ? [...groupByFamily(schema.parameters)] : []);
  let currentParams = $derived(families.find(([f]) => f === activeFamily)?.[1] ?? []);
</script>

<div class="config-page">
  <div class="page-header">
    <h1>/// CONFIGURATION</h1>
    <div class="header-info">
      {#if dirtyParams.size > 0}
        <span class="dirty-indicator">● {dirtyParams.size} unsaved</span>
      {/if}
    </div>
  </div>

  {#if error}
    <div class="message-box error">
      <span class="msg-icon">[!]</span>
      <span class="msg-text">ERROR: {error}</span>
    </div>
  {/if}

  {#if successMessage}
    <div class="message-box success">
      <span class="msg-icon">[✓]</span>
      <span class="msg-text">{successMessage}</span>
    </div>
  {/if}

  {#if schema}
    <div class="tabs-container">
      <div class="tabs">
        {#each families as [family]}
          <button
            class="tab"
            class:active={activeFamily === family}
            onclick={() => activeFamily = family}
          >
            {family.toUpperCase()}
          </button>
        {/each}
      </div>
      <div class="tabs-line"></div>
    </div>

    <div class="params-panel">
      <div class="panel-header">
        <span class="panel-icon">[{activeFamily.charAt(0).toUpperCase()}]</span>
        <span class="panel-title">{activeFamily.toUpperCase()} PARAMETERS</span>
        <span class="param-count">{currentParams.length} items</span>
      </div>

      <div class="params-list">
        {#each currentParams as param}
          <div class="param-row" class:dirty={dirtyParams.has(param.name)}>
            <div class="param-info">
              <div class="param-name">
                <span class="param-key">{param.name.split('.')[1]}</span>
                {#if param.unit}
                  <span class="param-unit">[{param.unit}]</span>
                {/if}
              </div>
              <div class="param-desc">{param.description}</div>
            </div>

            <div class="param-input">
              {#if param.type === 'bool'}
                <label class="toggle">
                  <input
                    type="checkbox"
                    checked={Boolean(getValue(param))}
                    onchange={(e) => handleChange(param, e.currentTarget.checked)}
                  />
                  <span class="toggle-track">
                    <span class="toggle-thumb"></span>
                  </span>
                  <span class="toggle-label">
                    {Boolean(getValue(param)) ? 'ON' : 'OFF'}
                  </span>
                </label>
              {:else if param.type === 'string'}
                <input
                  type={param.widget === 'password' ? 'password' : 'text'}
                  class="text-input"
                  value={String(getValue(param) ?? '')}
                  placeholder={param.description}
                  onchange={(e) => handleChange(param, e.currentTarget.value)}
                />
              {:else if param.values}
                <select
                  value={getValue(param)}
                  onchange={(e) => handleChange(param, Number(e.currentTarget.value))}
                >
                  {#each param.values as opt, i}
                    <option value={i}>{opt.name}</option>
                  {/each}
                </select>
              {:else}
                <div class="number-input-group">
                  <input
                    type="range"
                    value={getValue(param)}
                    min={param.min}
                    max={param.max}
                    oninput={(e) => handleChange(param, Number(e.currentTarget.value))}
                  />
                  <input
                    type="number"
                    value={getValue(param)}
                    min={param.min}
                    max={param.max}
                    onchange={(e) => handleChange(param, Number(e.currentTarget.value))}
                  />
                </div>
                <div class="range-labels">
                  <span>{param.min}</span>
                  <span>{param.max}</span>
                </div>
              {/if}
            </div>
          </div>
        {/each}
      </div>
    </div>

    <div class="actions-bar">
      <div class="actions-left">
        {#if dirtyParams.size > 0}
          <span class="action-hint">Changes are applied immediately. Save to persist after reboot.</span>
        {/if}
      </div>
      <div class="actions-right">
        <button class="save-btn" onclick={handleSave} disabled={saving}>
          <span class="btn-icon">{saving ? '◐' : '⬇'}</span>
          <span class="btn-text">{saving ? 'SAVING...' : 'SAVE TO NVS'}</span>
        </button>
      </div>
    </div>
  {:else}
    <div class="loading-panel">
      <div class="loading-text">Loading configuration...</div>
      <div class="loading-bar">
        <div class="loading-progress"></div>
      </div>
    </div>
  {/if}
</div>

<style>
  .config-page {
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

  .dirty-indicator {
    color: var(--accent-amber);
    font-size: 0.8rem;
    animation: pulse 1.5s ease-in-out infinite;
  }

  @keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.5; }
  }

  .message-box {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    padding: 0.75rem 1rem;
    margin-bottom: 1rem;
    border: 1px solid;
  }

  .message-box.error {
    background: rgba(255, 51, 51, 0.1);
    border-color: var(--accent-red);
    color: var(--accent-red);
  }

  .message-box.success {
    background: rgba(0, 255, 65, 0.1);
    border-color: var(--text-primary);
    color: var(--text-primary);
  }

  .msg-icon {
    font-weight: 700;
  }

  .msg-text {
    font-size: 0.85rem;
  }

  /* Tabs */
  .tabs-container {
    margin-bottom: 1rem;
  }

  .tabs {
    display: flex;
    gap: 0.25rem;
    flex-wrap: wrap;
  }

  .tab {
    padding: 0.5rem 1rem;
    font-size: 0.75rem;
    background: var(--bg-tertiary);
    border: 1px solid var(--border-dim);
    color: var(--text-dim);
    text-transform: uppercase;
    letter-spacing: 0.5px;
  }

  .tab:hover {
    color: var(--text-secondary);
    border-color: var(--text-secondary);
  }

  .tab.active {
    background: var(--text-primary);
    color: var(--bg-primary);
    border-color: var(--text-primary);
  }

  .tabs-line {
    height: 1px;
    background: linear-gradient(90deg, var(--text-primary), var(--border-dim), transparent);
    margin-top: 0.5rem;
  }

  /* Params Panel */
  .params-panel {
    background: var(--bg-secondary);
    border: 1px solid var(--border-dim);
    margin-bottom: 1rem;
  }

  .panel-header {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    padding: 0.75rem 1rem;
    border-bottom: 1px solid var(--border-dim);
    background: var(--bg-tertiary);
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

  .param-count {
    margin-left: auto;
    font-size: 0.7rem;
    color: var(--text-dim);
  }

  .params-list {
    padding: 0.5rem;
  }

  .param-row {
    display: grid;
    grid-template-columns: 1fr auto;
    gap: 1rem;
    padding: 0.75rem;
    border-bottom: 1px solid var(--border-dim);
    align-items: center;
  }

  .param-row:last-child {
    border-bottom: none;
  }

  .param-row:hover {
    background: var(--bg-tertiary);
  }

  .param-row.dirty {
    background: rgba(255, 176, 0, 0.05);
    border-left: 2px solid var(--accent-amber);
  }

  .param-info {
    min-width: 0;
  }

  .param-name {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    margin-bottom: 0.25rem;
  }

  .param-key {
    color: var(--text-primary);
    font-weight: 600;
    font-size: 0.9rem;
  }

  .param-unit {
    color: var(--accent-cyan);
    font-size: 0.75rem;
  }

  .param-desc {
    color: var(--text-dim);
    font-size: 0.75rem;
    line-height: 1.3;
  }

  .param-input {
    min-width: 200px;
    max-width: 280px;
  }

  /* Toggle */
  .toggle {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    cursor: pointer;
  }

  .toggle input {
    display: none;
  }

  .toggle-track {
    width: 44px;
    height: 22px;
    background: var(--bg-tertiary);
    border: 1px solid var(--border-dim);
    position: relative;
    transition: all 0.2s;
  }

  .toggle input:checked + .toggle-track {
    background: var(--text-primary);
    border-color: var(--text-primary);
  }

  .toggle-thumb {
    position: absolute;
    width: 16px;
    height: 16px;
    background: var(--text-dim);
    top: 2px;
    left: 2px;
    transition: all 0.2s;
  }

  .toggle input:checked + .toggle-track .toggle-thumb {
    left: 24px;
    background: var(--bg-primary);
  }

  .toggle-label {
    font-size: 0.8rem;
    font-weight: 600;
    color: var(--text-secondary);
    min-width: 30px;
  }

  /* Text Input */
  .text-input {
    width: 100%;
    padding: 0.5rem;
    background: var(--bg-primary);
    border: 1px solid var(--border-dim);
    color: var(--text-primary);
    font-size: 0.85rem;
    font-family: inherit;
  }

  .text-input:hover,
  .text-input:focus {
    border-color: var(--text-secondary);
    outline: none;
  }

  .text-input::placeholder {
    color: var(--text-dim);
    opacity: 0.5;
  }

  /* Select */
  select {
    width: 100%;
    padding: 0.5rem;
    background: var(--bg-primary);
    border: 1px solid var(--border-dim);
    color: var(--text-primary);
    font-size: 0.85rem;
    cursor: pointer;
  }

  select:hover {
    border-color: var(--text-secondary);
  }

  select option {
    background: var(--bg-secondary);
    color: var(--text-primary);
  }

  /* Number Input Group */
  .number-input-group {
    display: flex;
    gap: 0.5rem;
    align-items: center;
  }

  .number-input-group input[type="range"] {
    flex: 1;
    height: 4px;
    background: var(--bg-tertiary);
    border: none;
    outline: none;
    -webkit-appearance: none;
    cursor: pointer;
  }

  .number-input-group input[type="range"]::-webkit-slider-thumb {
    -webkit-appearance: none;
    width: 14px;
    height: 14px;
    background: var(--text-primary);
    cursor: pointer;
    box-shadow: var(--glow-green);
  }

  .number-input-group input[type="number"] {
    width: 70px;
    padding: 0.4rem;
    text-align: center;
    font-size: 0.85rem;
    border: 1px solid var(--border-dim);
    background: var(--bg-primary);
    color: var(--text-primary);
  }

  .range-labels {
    display: flex;
    justify-content: space-between;
    font-size: 0.65rem;
    color: var(--text-dim);
    margin-top: 0.25rem;
  }

  /* Actions Bar */
  .actions-bar {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 1rem;
    background: var(--bg-secondary);
    border: 1px solid var(--border-dim);
  }

  .action-hint {
    font-size: 0.75rem;
    color: var(--text-dim);
  }

  .save-btn {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    padding: 0.6rem 1.25rem;
  }

  .save-btn:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }

  .btn-icon {
    font-size: 0.9rem;
  }

  .btn-text {
    font-size: 0.8rem;
  }

  /* Loading */
  .loading-panel {
    background: var(--bg-secondary);
    border: 1px solid var(--border-dim);
    padding: 2rem;
    text-align: center;
  }

  .loading-text {
    color: var(--text-dim);
    margin-bottom: 1rem;
  }

  .loading-bar {
    height: 4px;
    background: var(--bg-tertiary);
    overflow: hidden;
  }

  .loading-progress {
    height: 100%;
    width: 30%;
    background: var(--text-primary);
    animation: loading 1s ease-in-out infinite;
  }

  @keyframes loading {
    0% { transform: translateX(-100%); }
    100% { transform: translateX(400%); }
  }

  @media (max-width: 768px) {
    .param-row {
      grid-template-columns: 1fr;
      gap: 0.5rem;
    }

    .param-input {
      max-width: none;
    }

    .tabs {
      overflow-x: auto;
      flex-wrap: nowrap;
      padding-bottom: 0.5rem;
    }

    .tab {
      flex-shrink: 0;
    }

    .actions-bar {
      flex-direction: column;
      gap: 0.75rem;
      text-align: center;
    }

    .save-btn {
      width: 100%;
      justify-content: center;
    }
  }
</style>
