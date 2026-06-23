<script lang="ts">
  import { api } from './api.js';
  import { board, applyState } from './store.svelte.js';
  import type { BoardState } from './types.js';

  let { slot }: { slot: number } = $props();

  async function add(e: Event & { currentTarget: HTMLSelectElement }) {
    const v = e.currentTarget.value;
    if (v === '') return;
    applyState(await api<BoardState>('/api/fx/add', { slot, kind: +v }));
    e.currentTarget.value = ''; // reset the picker
  }
</script>

<div class="slot-empty">
  <select onchange={add}>
    <option value="">+ add effect</option>
    {#each board.fxKinds as k, i}
      <option value={i}>{k.name}</option>
    {/each}
  </select>
</div>
