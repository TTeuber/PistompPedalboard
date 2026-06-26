<script lang="ts">
  import { onMount } from 'svelte';
  import { api } from './api.js';
  import { applyState } from './store.svelte.js';
  import type { BoardState, Setlist } from './types.js';

  // The setlists we can pick from, and the full rig catalogue for the "add" menu.
  let setlistNames = $state<string[]>([]);
  let allRigs = $state<string[]>([]);

  // The setlist currently open for editing/stepping.
  let sel = $state(''); // name of the loaded setlist ('' = unsaved/new)
  let rigs = $state<string[]>([]); // its ordered rig names (locally editable)
  let name = $state(''); // name field for save / new
  let addSel = $state(''); // rig chosen in the "add" dropdown
  let activeIdx = $state(-1); // which rig we last stepped to (-1 = none yet)

  const current = $derived(activeIdx >= 0 && activeIdx < rigs.length ? rigs[activeIdx] : '');

  async function loadSetlistNames() {
    try {
      setlistNames = await api<string[]>('/api/setlists');
    } catch {
      /* ignore */
    }
  }
  async function loadRigs() {
    try {
      allRigs = await api<string[]>('/api/rigs');
    } catch {
      /* ignore */
    }
  }

  onMount(() => {
    loadSetlistNames();
    loadRigs();
  });

  async function open(n: string) {
    if (!n) return;
    const s = await api<Setlist>(`/api/setlist?name=${encodeURIComponent(n)}`);
    sel = s.name;
    name = s.name;
    rigs = s.rigs;
    activeIdx = -1;
  }

  function newSetlist() {
    sel = '';
    name = '';
    rigs = [];
    addSel = '';
    activeIdx = -1;
  }

  function addRig() {
    if (!addSel) return;
    rigs = [...rigs, addSel];
    addSel = '';
  }
  function removeRig(i: number) {
    rigs = rigs.filter((_, k) => k !== i);
    if (activeIdx >= rigs.length) activeIdx = rigs.length - 1;
  }
  function moveRig(i: number, dir: number) {
    const j = i + dir;
    if (j < 0 || j >= rigs.length) return;
    const copy = [...rigs];
    [copy[i], copy[j]] = [copy[j], copy[i]];
    rigs = copy;
  }

  async function save() {
    const n = name.trim();
    if (!n) return;
    await api('/api/setlist/save', { name: n, rigs });
    sel = n;
    await loadSetlistNames();
  }
  async function del() {
    if (!sel) return;
    await api('/api/setlist/delete', { name: sel });
    await loadSetlistNames();
    newSetlist();
  }

  // Stepping: load the rig at index `i` onto the board (the gigging action).
  async function goTo(i: number) {
    if (i < 0 || i >= rigs.length) return;
    activeIdx = i;
    applyState(await api<BoardState>('/api/rig/load', { name: rigs[i] }));
  }
  function step(delta: number) {
    const next = activeIdx < 0 ? (delta > 0 ? 0 : rigs.length - 1) : activeIdx + delta;
    goTo(next);
  }
</script>

<section class="setlist">
  <div class="setlist-head">
    <span class="title">Setlist</span>
    <select value={sel} onchange={(e) => open(e.currentTarget.value)}>
      <option value="">—</option>
      {#each setlistNames as n}
        <option value={n}>{n}</option>
      {/each}
    </select>
    <button onclick={newSetlist}>New</button>
    <input type="text" placeholder="name…" bind:value={name} />
    <button onclick={save} disabled={!name.trim()}>Save</button>
    <button onclick={del} disabled={!sel}>Delete</button>
  </div>

  {#if rigs.length}
    <div class="setlist-rigs">
      {#each rigs as r, i (r + '-' + i)}
        <div class="setlist-row" class:current={i === activeIdx}>
          <span class="idx">{i + 1}.</span>
          <span class="name">{r}</span>
          <button class="mini" title="Load this rig" onclick={() => goTo(i)}>▶</button>
          <button class="mini" title="Move up" disabled={i === 0} onclick={() => moveRig(i, -1)}>▲</button>
          <button class="mini" title="Move down" disabled={i === rigs.length - 1} onclick={() => moveRig(i, 1)}>▼</button>
          <button class="mini danger" title="Remove from setlist" onclick={() => removeRig(i)}>✕</button>
        </div>
      {/each}
    </div>
  {:else}
    <div class="setlist-empty">No rigs yet — add some below, then Save.</div>
  {/if}

  <div class="setlist-head">
    <select bind:value={addSel}>
      <option value="">add rig…</option>
      {#each allRigs as r}
        <option value={r}>{r}</option>
      {/each}
    </select>
    <button onclick={addRig} disabled={!addSel}>Add</button>
  </div>

  <div class="setlist-active">
    <button onclick={() => step(-1)} disabled={!rigs.length}>◀ Prev</button>
    <span class="now">{current ? `Now: ${current}` : 'Not stepping'}</span>
    <button onclick={() => step(1)} disabled={!rigs.length}>Next ▶</button>
  </div>
</section>

<style>
  /* Setlists panel: ordered list of rigs + live Prev/Next stepping. */
  .setlist {
    background: var(--panel);
    border: 1px solid var(--line);
    border-radius: var(--r-xl);
    margin: 0 var(--sp-6) var(--sp-6);
    padding: var(--sp-4);
  }
  .setlist-head { display: flex; align-items: center; gap: var(--sp-3); flex-wrap: wrap; margin-bottom: var(--sp-4); }
  .setlist-head .title {
    font-size: var(--fs-sm);
    letter-spacing: var(--track-wide);
    text-transform: uppercase;
    color: var(--muted);
    margin-right: var(--sp-1);
  }
  .setlist select,
  .setlist input,
  .setlist button {
    background: var(--panel-2);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    padding: 7px var(--sp-4);
    font: inherit;
  }
  .setlist button { cursor: pointer; }
  .setlist button:hover:not(:disabled) { border-color: var(--accent); }
  .setlist button:disabled { opacity: .4; cursor: default; }
  .setlist-rigs { display: flex; flex-direction: column; gap: var(--sp-2); margin: var(--sp-2) 0 var(--sp-4); }
  .setlist-row {
    display: flex;
    align-items: center;
    gap: var(--sp-3);
    padding: var(--sp-3) var(--sp-4);
    background: var(--panel-2);
    border: 1px solid var(--line);
    border-radius: var(--r-md);
  }
  .setlist-row.current { border-color: var(--accent); box-shadow: 0 0 8px -3px var(--accent); }
  .setlist-row .idx { color: var(--muted); font-variant-numeric: tabular-nums; min-width: 22px; }
  .setlist-row .name { flex: 1; }
  .setlist-row :global(.mini) { flex: 0 0 auto; padding: 5px var(--sp-3); }
  .setlist-active { display: flex; align-items: center; gap: var(--sp-4); flex-wrap: wrap; }
  .setlist-active .now { font-weight: 600; min-width: 120px; }
  .setlist-empty { color: var(--muted); font-size: var(--fs-sm); padding: var(--sp-1) 0 var(--sp-3); }
</style>
