<script lang="ts">
  import { api } from './api.js';
  import { applyState } from './store.svelte.js';
  import type { BoardState } from './types.js';

  let { slot, onadd }: { slot: number; onadd: (slot: number) => void } = $props();

  let dragOver = $state(false);

  // Dropping a dragged pedal onto an empty cell moves it into THIS exact cell
  // (free-form positioning -- the server preserves gaps, no repacking).
  async function onDrop(e: DragEvent) {
    e.preventDefault();
    dragOver = false;
    const from = Number(e.dataTransfer?.getData('text/plain'));
    if (!Number.isInteger(from)) return;
    applyState(await api<BoardState>('/api/fx/moveto', { slot: from, to: slot }));
  }
</script>

<button
  class="slot-empty"
  class:drag-over={dragOver}
  title="Add an effect here"
  aria-label="Add an effect to slot {slot + 1}"
  onclick={() => onadd(slot)}
  ondragover={(e) => {
    e.preventDefault();
    dragOver = true;
  }}
  ondragleave={() => (dragOver = false)}
  ondrop={onDrop}
>
  <span class="plus">+</span>
</button>

<style>
  .slot-empty {
    width: 100%;
    border: 1px dashed var(--line);
    border-radius: var(--r-lg);
    background: var(--inset);
    min-height: 78px;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: var(--sp-3);
    cursor: pointer;
    color: var(--muted);
    transition: border-color var(--t-fast), background var(--t-fast), color var(--t-fast);
  }
  .slot-empty:hover { border-color: var(--accent); color: var(--accent); }
  .slot-empty.drag-over { border-color: var(--accent); background: var(--panel-2); }
  .plus { font-size: 24px; line-height: 1; font-weight: 300; }
</style>
