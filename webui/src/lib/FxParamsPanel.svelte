<script lang="ts">
  import { api } from './api.js';
  import { applyState } from './store.svelte.js';
  import type { BoardState, Effect } from './types.js';
  import ParamControl from './ParamControl.svelte';
  import AssignStrip from './AssignStrip.svelte';
  import PedalPresets from './PedalPresets.svelte';
  import Switch from './controls/Switch.svelte';

  // The currently selected FX pedal, or null when nothing is selected.
  let { fx }: { fx: Effect | null } = $props();

  // Beat-sync coupling: a synced effect (has a `sync` param) drives its time from
  // `div` at the board tempo, so the manual Time/Rate knob is inert; when sync is
  // off, `div` is inert. Dim whichever control isn't doing anything so it's clear.
  const sync = $derived(fx?.params.find((p) => p.id === 'sync'));
  const synced = $derived(!!sync && sync.value > 0.5);
  function inert(id: string): boolean {
    if (!sync) return false;
    if (id === 'time' || id === 'rate') return synced;
    if (id === 'div') return !synced;
    return false;
  }

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
      <Switch on={fx.enabled} label="Enable / disable {fx.name}" onclick={togglePower} />
    </div>

    <PedalPresets {fx} />

    {#each fx.params as p (p.id)}
      <div class:inert={inert(p.id)}>
        <ParamControl effectType={fx.type} {p} />
      </div>
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
  .remove { width: 100%; margin-top: var(--sp-4); }

  /* An inert control (manual Time while synced, or Div while free-running) is
     dimmed and non-interactive so the active one reads clearly. */
  .inert { opacity: 0.4; pointer-events: none; transition: opacity var(--t-fast); }

  .empty {
    color: var(--muted);
    font-size: var(--fs-sm);
    text-align: center;
    margin: var(--sp-6) 0;
  }
</style>
