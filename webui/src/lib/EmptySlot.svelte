<script lang="ts">
  import { api } from './api.js';
  import { board, applyState } from './store.svelte.js';
  import type { BoardState } from './types.js';

  let { slot }: { slot: number } = $props();

  let dragOver = $state(false);

  async function add(e: Event & { currentTarget: HTMLSelectElement }) {
    const v = e.currentTarget.value;
    if (v === '') return;
    applyState(await api<BoardState>('/api/fx/add', { slot, kind: +v }));
    e.currentTarget.value = ''; // reset the picker
  }

  // Dropping a dragged pedal onto an empty cell appends it to the end of the
  // chain (the server treats an empty target slot as "append" and repacks).
  async function onDrop(e: DragEvent) {
    e.preventDefault();
    dragOver = false;
    const from = Number(e.dataTransfer?.getData('text/plain'));
    if (!Number.isInteger(from)) return;
    applyState(await api<BoardState>('/api/fx/reorder', { slot: from, to: slot }));
  }
</script>

<div
  class="slot-empty"
  class:drag-over={dragOver}
  role="presentation"
  ondragover={(e) => {
    e.preventDefault();
    dragOver = true;
  }}
  ondragleave={() => (dragOver = false)}
  ondrop={onDrop}
>
  <select onchange={add}>
    <option value="">+ add effect</option>
    {#each board.fxKinds as k, i}
      <option value={i}>{k.name}</option>
    {/each}
  </select>
</div>

<style>
  .slot-empty {
    border: 1px dashed var(--line);
    border-radius: var(--r-lg);
    background: var(--inset);
    min-height: 78px;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: var(--sp-3);
    transition: border-color var(--t-fast), background var(--t-fast);
  }
  .slot-empty.drag-over { border-color: var(--accent); background: var(--panel-2); }
  .slot-empty select {
    width: 100%;
    background: var(--panel-2);
    color: var(--muted);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    padding: var(--sp-3);
    cursor: pointer;
  }
  .slot-empty select:hover { border-color: var(--accent); color: var(--text); }
</style>
