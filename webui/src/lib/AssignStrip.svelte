<script>
  import { api } from './api.js';
  import { applyState } from './store.svelte.js';

  let { fx } = $props();

  // FS1..FS4 -- must match the device palette (kFsColors) and the FootswitchBar:
  // red / green / blue / yellow.
  const COLORS = ['#ff453a', '#30d158', '#4d96ff', '#ffcc00'];

  // Cycle this pedal's binding for one footswitch, exactly like the device's
  // assign page: unassigned (or bound elsewhere) -> normal -> inverted -> clear.
  async function cycle(i) {
    let fs, mode;
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
    applyState(await api('/api/assign', { effect: fx.type, fs, mode }));
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
