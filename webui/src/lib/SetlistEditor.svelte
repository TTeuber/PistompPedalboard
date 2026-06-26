<script lang="ts">
  // Dedicated full-page view (route #setlists) for composing rigs + setlists,
  // opened from the sidebar. Browsing/stepping happens in the sidebar; this view
  // is purely for authoring: snapshot the current board as a rig, then assemble
  // and order rigs into named setlists.
  import { onMount } from 'svelte';
  import { api } from './api.js';
  import type { Setlist } from './types.js';

  let setlistNames = $state<string[]>([]);
  let allRigs = $state<string[]>([]);

  // The setlist currently open for editing ('' = new/unsaved).
  let sel = $state('');
  let rigs = $state<string[]>([]); // its ordered rig names (locally editable)
  let name = $state(''); // name field for save / new
  let addSel = $state(''); // rig chosen in the "add" dropdown

  // Save-current-board-as-a-rig field.
  let rigName = $state('');

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

  async function saveRig() {
    const n = rigName.trim();
    if (!n) return;
    await api('/api/rig/save', { name: n }); // snapshots the live board
    rigName = '';
    await loadRigs();
  }

  async function open(n: string) {
    if (!n) {
      newSetlist();
      return;
    }
    const s = await api<Setlist>(`/api/setlist?name=${encodeURIComponent(n)}`);
    sel = s.name;
    name = s.name;
    rigs = s.rigs;
  }
  function newSetlist() {
    sel = '';
    name = '';
    rigs = [];
    addSel = '';
  }

  function addRig() {
    if (!addSel) return;
    rigs = [...rigs, addSel];
    addSel = '';
  }
  function removeRig(i: number) {
    rigs = rigs.filter((_, k) => k !== i);
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
</script>

<div class="view">
  <header>
    <h1>Setlists</h1>
    <a class="back" href="#board">← Back to board</a>
  </header>

  <section class="panel">
    <h2>Save current board as a rig</h2>
    <p class="hint">Captures the live chain + knob settings under a name you can drop into setlists.</p>
    <div class="row">
      <input type="text" placeholder="rig name…" bind:value={rigName} />
      <button class="btn" onclick={saveRig} disabled={!rigName.trim()}>Save rig</button>
    </div>
  </section>

  <section class="panel">
    <h2>Edit setlist</h2>
    <div class="row">
      <select value={sel} onchange={(e) => open(e.currentTarget.value)}>
        <option value="">＋ New setlist</option>
        {#each setlistNames as n}
          <option value={n}>{n}</option>
        {/each}
      </select>
      <input type="text" placeholder="setlist name…" bind:value={name} />
      <button class="btn" onclick={save} disabled={!name.trim()}>Save</button>
      <button class="btn danger" onclick={del} disabled={!sel}>Delete</button>
    </div>

    {#if rigs.length}
      <ol class="rig-list">
        {#each rigs as r, i (r + '-' + i)}
          <li class="rig-row">
            <span class="idx">{i + 1}</span>
            <span class="name">{r}</span>
            <button class="step" title="Move up" disabled={i === 0} onclick={() => moveRig(i, -1)}>▲</button>
            <button class="step" title="Move down" disabled={i === rigs.length - 1} onclick={() => moveRig(i, 1)}>▼</button>
            <button class="step danger" title="Remove" onclick={() => removeRig(i)}>✕</button>
          </li>
        {/each}
      </ol>
    {:else}
      <div class="empty">No rigs in this setlist yet — add some below.</div>
    {/if}

    <div class="row">
      <select bind:value={addSel}>
        <option value="">add a rig…</option>
        {#each allRigs as r}
          <option value={r}>{r}</option>
        {/each}
      </select>
      <button class="btn" onclick={addRig} disabled={!addSel}>Add</button>
    </div>
  </section>
</div>

<style>
  .view { max-width: 720px; margin: 0 auto; padding: var(--sp-6); }
  header { display: flex; align-items: baseline; justify-content: space-between; margin-bottom: var(--sp-6); }
  h1 { font-size: var(--fs-xl); margin: 0; font-weight: 600; }
  .back { color: var(--muted); text-decoration: none; font-size: var(--fs-sm); }
  .back:hover { color: var(--accent); }

  .panel {
    background: var(--panel);
    border: 1px solid var(--line);
    border-radius: var(--r-xl);
    padding: var(--sp-5) var(--sp-6);
    margin-bottom: var(--sp-5);
  }
  h2 {
    font-size: var(--fs-sm);
    letter-spacing: var(--track-wide);
    text-transform: uppercase;
    color: var(--muted);
    margin: 0 0 var(--sp-3);
  }
  .hint { font-size: var(--fs-sm); color: var(--faint); margin: 0 0 var(--sp-4); }

  .row { display: flex; gap: var(--sp-3); flex-wrap: wrap; align-items: center; margin-bottom: var(--sp-4); }
  .row:last-child { margin-bottom: 0; }
  .row select,
  .row input {
    flex: 1;
    min-width: 140px;
    background: var(--panel-2);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    padding: var(--sp-3) var(--sp-4);
    font: inherit;
  }
  .btn {
    flex: 0 0 auto;
    background: var(--panel-2);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    padding: var(--sp-3) var(--sp-5);
    font: inherit;
    font-weight: 600;
    cursor: pointer;
    transition: border-color var(--t-fast), color var(--t-fast);
  }
  .btn:hover:not(:disabled) { border-color: var(--accent); }
  .btn.danger:hover:not(:disabled) { border-color: var(--danger); color: var(--danger); }
  .btn:disabled { opacity: .4; cursor: default; }

  .rig-list { list-style: none; margin: 0 0 var(--sp-4); padding: 0; display: flex; flex-direction: column; gap: var(--sp-2); }
  .rig-row {
    display: flex;
    align-items: center;
    gap: var(--sp-3);
    padding: var(--sp-3) var(--sp-4);
    background: var(--panel-2);
    border: 1px solid var(--line);
    border-radius: var(--r-md);
  }
  .rig-row .idx { color: var(--muted); font-variant-numeric: tabular-nums; min-width: 20px; }
  .rig-row .name { flex: 1; }
  .step {
    flex: 0 0 auto;
    width: 30px;
    padding: var(--sp-2) 0;
    background: var(--inset);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    cursor: pointer;
  }
  .step:hover:not(:disabled) { border-color: var(--accent); }
  .step.danger:hover:not(:disabled) { border-color: var(--danger); color: var(--danger); }
  .step:disabled { opacity: .35; cursor: default; }
  .empty { color: var(--muted); font-size: var(--fs-sm); padding: var(--sp-2) 0 var(--sp-4); }
</style>
