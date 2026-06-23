<script>
  import { api } from './api.js';
  import { board, applyState } from './store.svelte.js';

  // FS1..FS4 colors -- match the device NeoPixels (kFsColors) and AssignStrip.
  const COLORS = ['#ff453a', '#30d158', '#4d96ff', '#ffcc00'];

  // How many effects each footswitch drives, so an unbound switch reads as inert.
  const counts = $derived(
    COLORS.map((_, i) => board.effects.filter((e) => e.fsAssign === i).length),
  );

  async function toggle(i) {
    board.footswitches[i] = !board.footswitches[i]; // optimistic; SSE confirms
    applyState(await api('/api/footswitch', { fs: i }));
  }
</script>

<div class="fsbar">
  {#each COLORS as color, i}
    <button
      class="fsswitch"
      class:engaged={board.footswitches[i]}
      class:idle={counts[i] === 0}
      style="--fs:{color}"
      title={`Footswitch ${i + 1} — ${counts[i]} pedal${counts[i] === 1 ? '' : 's'}`}
      onclick={() => toggle(i)}
    >
      <span class="fsnum">FS{i + 1}</span>
      <span class="fsstate">{board.footswitches[i] ? 'ON' : 'OFF'}</span>
    </button>
  {/each}
</div>
