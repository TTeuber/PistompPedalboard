<script lang="ts">
  import { api } from './api.js';
  import { applyState } from './store.svelte.js';
  import type { BoardState, Effect } from './types.js';
  import ParamControl from './ParamControl.svelte';
  import AssignStrip from './AssignStrip.svelte';
  import PedalPresets from './PedalPresets.svelte';

  // The currently selected FX pedal, or null when nothing is selected.
  let { fx }: { fx: Effect | null } = $props();

  function togglePower() {
    if (!fx) return;
    const on = !fx.enabled;
    fx.enabled = on; // optimistic; the SSE echo confirms
    api('/api/enabled', { effect: fx.type, enabled: on }).catch(console.error);
  }

  async function remove() {
    if (!fx) return;
    applyState(await api<BoardState>('/api/fx/remove', { slot: fx.slot }));
  }
</script>

<aside class="panel">
  {#if fx}
    <div class="panel-head">
      <h3>{fx.name}</h3>
      <button
        class="power"
        class:on={fx.enabled}
        title="Enable / disable"
        aria-label="Enable / disable {fx.name}"
        onclick={togglePower}
      ></button>
    </div>

    <PedalPresets {fx} />

    {#each fx.params as p (p.id)}
      <ParamControl effectType={fx.type} {p} />
    {/each}

    <AssignStrip {fx} />

    <button class="remove mini danger" title="Remove from chain" onclick={remove}>Remove</button>
  {:else}
    <p class="empty">Select a pedal to edit its parameters.</p>
  {/if}
</aside>

<style>
  .panel { min-width: 0; }
  .panel-head { display: flex; align-items: center; justify-content: space-between; margin-bottom: var(--sp-3); }
  .panel-head h3 {
    font-size: var(--fs-sm);
    margin: 0;
    letter-spacing: var(--track);
    text-transform: uppercase;
    color: var(--accent);
  }
  .power {
    width: 26px;
    height: 26px;
    border-radius: 50%;
    border: 2px solid var(--line);
    background: var(--inset);
    cursor: pointer;
  }
  .power.on { border-color: var(--ok); box-shadow: var(--glow) var(--ok); background: var(--ok); }

  .remove { width: 100%; margin-top: var(--sp-4); }

  .empty {
    color: var(--muted);
    font-size: var(--fs-sm);
    text-align: center;
    margin: var(--sp-6) 0;
  }
</style>
