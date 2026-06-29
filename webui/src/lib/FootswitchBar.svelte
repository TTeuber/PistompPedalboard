<script lang="ts">
  import { api } from './api.js';
  import { board, applyState } from './store.svelte.js';
  import type { BoardState } from './types.js';
  import { FS_COLORS as COLORS } from './fsColors.js';

  // How many effects each footswitch drives, so an unbound switch reads as inert.
  const counts = $derived(
    COLORS.map((_, i) => board.effects.filter((e) => e.fsAssign === i).length),
  );

  async function toggle(i: number) {
    board.footswitches[i] = !board.footswitches[i]; // optimistic; SSE confirms
    applyState(await api<BoardState>('/api/footswitch', { fs: i }));
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

<style>
  /* Live footswitch bar: tap to toggle the same latches as the hardware stomps.
     Each switch carries its hardware NeoPixel colour via the inline --fs var. */
  .fsbar { display: grid; grid-template-columns: repeat(4, 1fr); gap: var(--sp-3); margin-top: var(--sp-4); }
  .fsswitch {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 2px;
    padding: var(--sp-3) 0;
    border-radius: var(--r-md);
    cursor: pointer;
    font: inherit;
    background: var(--inset);
    color: var(--muted);
    border: 1px solid var(--line);
    border-bottom: 3px solid var(--line);
    transition: background var(--t-fast), color var(--t-fast), border-color var(--t-fast), box-shadow var(--t-fast);
  }
  .fsswitch .fsnum { font-weight: 700; font-size: var(--fs-xs); letter-spacing: .5px; }
  .fsswitch .fsstate { font-size: 10px; font-variant-numeric: tabular-nums; }
  .fsswitch.engaged {
    color: var(--ink);
    background: var(--fs);
    border-color: var(--fs);
    border-bottom-color: color-mix(in srgb, var(--fs) 55%, #000);
    box-shadow: var(--glow) var(--fs);
  }
  .fsswitch.idle { opacity: .5; } /* no pedals bound -> visually inert */
</style>
