<script lang="ts">
  import { api } from './api.js';
  import { applyState } from './store.svelte.js';
  import type { BoardState, Effect } from './types.js';
  import ParamControl from './ParamControl.svelte';
  import AssignStrip from './AssignStrip.svelte';
  import PedalPresets from './PedalPresets.svelte';

  let { fx, inGrid = false }: { fx: Effect; inGrid?: boolean } = $props();

  function togglePower() {
    const on = !fx.enabled;
    fx.enabled = on; // optimistic; the SSE echo confirms
    api('/api/enabled', { effect: fx.type, enabled: on }).catch(console.error);
  }

  // Grid moves/removes return the full new state (the chain re-laid out), so we
  // fold the response straight into the store.
  async function move(dir: number) {
    applyState(await api<BoardState>('/api/fx/move', { slot: fx.slot, dir }));
  }
  async function remove() {
    applyState(await api<BoardState>('/api/fx/remove', { slot: fx.slot }));
  }
</script>

<div class="pedal" class:off={!fx.enabled}>
  <div class="pedal-head">
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

  {#if inGrid}
    <AssignStrip {fx} />
    <div class="pedal-foot">
      <button class="mini" title="Move toward input" onclick={() => move(-1)}>◀</button>
      <button class="mini danger" title="Remove" onclick={remove}>✕</button>
      <button class="mini" title="Move toward output" onclick={() => move(1)}>▶</button>
    </div>
  {/if}
</div>

<style>
  .pedal {
    background: var(--panel-2);
    border: 1px solid var(--line);
    border-radius: var(--r-lg);
    padding: var(--sp-4);
    transition: opacity var(--t-med), border-color var(--t-med);
  }
  .pedal.off { opacity: .5; }
  .pedal-head { display: flex; align-items: center; justify-content: space-between; margin-bottom: var(--sp-3); }
  .pedal-head h3 {
    font-size: var(--fs-sm);
    margin: 0;
    letter-spacing: var(--track);
    text-transform: uppercase;
    color: var(--accent);
  }

  /* On/off power dot. */
  .power {
    width: 26px;
    height: 26px;
    border-radius: 50%;
    border: 2px solid var(--line);
    background: var(--inset);
    cursor: pointer;
  }
  .power.on { border-color: var(--ok); box-shadow: var(--glow) var(--ok); background: var(--ok); }

  .pedal-foot { display: flex; gap: var(--sp-2); }
</style>
