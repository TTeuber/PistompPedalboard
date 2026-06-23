<script lang="ts">
  import { api } from './api.js';
  import { applyState } from './store.svelte.js';
  import type { BoardState, Effect } from './types.js';
  import ParamControl from './ParamControl.svelte';
  import AssignStrip from './AssignStrip.svelte';

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
