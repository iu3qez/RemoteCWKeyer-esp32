<script lang="ts">
  import { onMount } from 'svelte';
  import { api } from '../lib/api';
  import type { ConfigSchema, ConfigValues, ParameterMeta } from '../lib/types';

  let schema = $state<ConfigSchema | null>(null);
  let values = $state<ConfigValues>({});
  let error = $state<string | null>(null);
  let saving = $state(false);

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
    } catch (e) {
      error = e instanceof Error ? e.message : 'Failed to set parameter';
    }
  }

  async function handleSave() {
    saving = true;
    try {
      await api.saveConfig();
      error = null;
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
    } catch (e) {
      error = e instanceof Error ? e.message : 'Failed to load config';
    }
  });
</script>

<div class="config-page">
  <h1>Configuration</h1>

  {#if error}
    <div class="error">{error}</div>
  {/if}

  {#if schema}
    {#each [...groupByFamily(schema.parameters)] as [family, params]}
      <section class="card">
        <h2>{family}</h2>
        {#each params as param}
          <div class="param">
            <label for={param.name}>
              {param.name.split('.')[1]}
              {#if param.unit}<span class="unit">({param.unit})</span>{/if}
            </label>

            {#if param.type === 'bool'}
              <input
                type="checkbox"
                id={param.name}
                checked={Boolean(getValue(param))}
                onchange={(e) => handleChange(param, e.currentTarget.checked)}
              />
            {:else if param.values}
              <select
                id={param.name}
                value={getValue(param)}
                onchange={(e) => handleChange(param, Number(e.currentTarget.value))}
              >
                {#each param.values as opt, i}
                  <option value={i}>{opt.name}</option>
                {/each}
              </select>
            {:else}
              <input
                type="number"
                id={param.name}
                value={getValue(param)}
                min={param.min}
                max={param.max}
                onchange={(e) => handleChange(param, Number(e.currentTarget.value))}
              />
            {/if}

            <small>{param.description}</small>
          </div>
        {/each}
      </section>
    {/each}

    <div class="actions">
      <button onclick={handleSave} disabled={saving}>
        {saving ? 'Saving...' : 'Save to NVS'}
      </button>
    </div>
  {/if}
</div>

<style>
  .config-page { max-width: 600px; }
  .card { background: #f5f5f5; padding: 1rem; margin: 1rem 0; border-radius: 8px; }
  .error { color: red; padding: 1rem; background: #fee; border-radius: 8px; }
  .param { margin: 1rem 0; }
  .param label { display: block; font-weight: bold; margin-bottom: 0.25rem; }
  .param .unit { font-weight: normal; color: #666; }
  .param input[type="number"], .param select { width: 100%; padding: 0.5rem; }
  .param small { display: block; color: #666; margin-top: 0.25rem; }
  .actions { margin-top: 2rem; }
  button { padding: 0.75rem 1.5rem; cursor: pointer; }
  button:disabled { opacity: 0.5; }
</style>
