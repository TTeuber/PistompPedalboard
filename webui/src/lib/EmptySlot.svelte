<script>
  import { api } from './api.js';
  import { board, applyState } from './store.svelte.js';

  let { slot } = $props();

  async function add(e) {
    const v = e.currentTarget.value;
    if (v === '') return;
    applyState(await api('/api/fx/add', { slot, kind: +v }));
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
