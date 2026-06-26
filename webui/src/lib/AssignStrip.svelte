<script lang="ts">
  import { api } from './api.js';
  import { applyState } from './store.svelte.js';
  import type { BoardState, Effect } from './types.js';

  let { fx }: { fx: Effect } = $props();

  // FS1..FS4 -- must match the device palette (kFsColors) and the FootswitchBar:
  // red / green / blue / yellow.
  const COLORS = ['#ff453a', '#30d158', '#4d96ff', '#ffcc00'];

  // Cycle this pedal's binding for one footswitch, exactly like the device's
  // assign page: unassigned (or bound elsewhere) -> normal -> inverted -> clear.
  async function cycle(i: number) {
    let fs: number, mode: number;
    if (fx.fsAssign !== i) {
      fs = i;
      mode = 0;
    } else if (fx.fsMode === 0) {
      fs = i;
      mode = 1;
    } else {
      fs = -1;
      mode = 0;
    }
    applyState(await api<BoardState>('/api/assign', { effect: fx.type, fs, mode }));
  }
</script>

<div class="assign">
  {#each COLORS as color, i}
    <button
      class="fschip"
      class:on={fx.fsAssign === i}
      class:inv={fx.fsAssign === i && fx.fsMode === 1}
      style="--fs:{color}"
      title="Footswitch {i + 1}"
      onclick={() => cycle(i)}
    >
      {fx.fsAssign === i && fx.fsMode === 1 ? `${i + 1}⌀` : i + 1}
    </button>
  {/each}
</div>

<style>
  /* Footswitch assignment strip: one chip per FS, carrying its --fs colour. */
  .assign { display: flex; gap: var(--sp-2); margin: var(--sp-4) 0 var(--sp-3); }
  .fschip {
    flex: 1;
    padding: var(--sp-2) 0;
    border-radius: var(--r-sm);
    cursor: pointer;
    font-weight: 700;
    font-size: var(--fs-xs);
    color: var(--muted);
    background: var(--inset);
    border: 1px solid var(--line);
  }
  .fschip.on { color: var(--ink); background: var(--fs); border-color: var(--fs); box-shadow: var(--glow) var(--fs); }
  .fschip.inv { color: var(--fs); background: var(--inset); border-color: var(--fs); }
</style>
