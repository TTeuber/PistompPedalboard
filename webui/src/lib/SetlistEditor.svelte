<script lang="ts">
  // Dedicated full-page view (route #setlists) for composing setlists, opened
  // from the sidebar. Two panes that fill the viewport: the LEFT is the whole
  // rig catalogue (filterable) you drag from; the RIGHT is the setlist being
  // edited, whose rigs you reorder by drag-and-drop. Browsing/stepping happens
  // in the sidebar -- this view is purely for authoring.
  //
  // Reads/writes the shared rigState so the sidebar stays in lockstep after a
  // save or delete (loadLists re-fetches the catalogue + setlist names).
  import { onMount } from 'svelte';
  import { api } from './api.js';
  import { rigState, loadLists, saveRig as saveBoardRig } from './rigs.svelte.js';
  import type { Setlist } from './types.js';

  // The setlist currently open for editing ('' = new/unsaved).
  let sel = $state('');
  let rigs = $state<string[]>([]); // its ordered rig names (locally editable)
  let name = $state(''); // name field for save / new
  let filter = $state(''); // catalogue search box
  let rigName = $state(''); // save-current-board-as-a-rig field

  // Catalogue filtered by the search box.
  const shown = $derived(
    rigState.allRigs.filter((r) => r.toLowerCase().includes(filter.trim().toLowerCase())),
  );
  // How many times each rig appears in the working setlist (for the count badge).
  const counts = $derived.by(() => {
    const m: Record<string, number> = {};
    for (const r of rigs) m[r] = (m[r] ?? 0) + 1;
    return m;
  });
  const dirty = $derived(sel !== '' || rigs.length > 0 || name.trim() !== '');

  onMount(() => {
    loadLists();
  });

  async function open(n: string) {
    if (!n) return newSetlist();
    const s = await api<Setlist>(`/api/setlist?name=${encodeURIComponent(n)}`);
    sel = s.name;
    name = s.name;
    rigs = s.rigs;
  }
  function newSetlist() {
    sel = '';
    name = '';
    rigs = [];
  }

  function addRig(r: string, at = rigs.length) {
    const copy = [...rigs];
    copy.splice(at, 0, r);
    rigs = copy;
  }
  function removeRig(i: number) {
    rigs = rigs.filter((_, k) => k !== i);
  }

  async function save() {
    const n = name.trim();
    if (!n) return;
    await api('/api/setlist/save', { name: n, rigs });
    sel = n;
    await loadLists();
  }
  async function del() {
    if (!sel) return;
    await api('/api/setlist/delete', { name: sel });
    newSetlist();
    await loadLists();
  }
  async function saveBoard() {
    const n = rigName.trim();
    if (!n) return;
    await saveBoardRig(n); // snapshots the live board, refreshes the catalogue
    rigName = '';
  }

  // ---- drag & drop -------------------------------------------------------
  // One in-flight drag, from either pane. `from:'cat'` adds a rig; `from:'list'`
  // reorders within the setlist. `over` is the insertion index in the setlist
  // (where a drop would land); `over === rigs.length` means "append".
  let drag = $state<{ from: 'cat' | 'list'; index: number; rig: string } | null>(null);
  let over = $state(-1);

  function startCat(e: DragEvent, rig: string) {
    drag = { from: 'cat', index: -1, rig };
    e.dataTransfer!.effectAllowed = 'copy';
  }
  function startList(e: DragEvent, index: number) {
    drag = { from: 'list', index, rig: rigs[index] };
    e.dataTransfer!.effectAllowed = 'move';
  }
  // One handler on the whole list: the insertion point is the first row whose
  // midpoint the pointer is above, else the end. Handling it here (rather than
  // per-row) means the gaps between rows resolve correctly too, instead of
  // falling through to a separate "append" handler.
  function overList(e: DragEvent) {
    if (!drag) return;
    e.preventDefault();
    const rows = [...(e.currentTarget as HTMLElement).querySelectorAll('.rig-row')];
    let idx = rows.length;
    for (let k = 0; k < rows.length; k++) {
      const b = rows[k].getBoundingClientRect();
      if (e.clientY < b.top + b.height / 2) {
        idx = k;
        break;
      }
    }
    over = idx;
  }
  function drop() {
    if (!drag || over < 0) return reset();
    if (drag.from === 'cat') {
      addRig(drag.rig, over);
    } else {
      // Reorder: pull the item, then insert at the target (adjust for the gap
      // the removal opens up when moving an item further down the list).
      const copy = [...rigs];
      const [m] = copy.splice(drag.index, 1);
      copy.splice(over > drag.index ? over - 1 : over, 0, m);
      rigs = copy;
    }
    reset();
  }
  function reset() {
    drag = null;
    over = -1;
  }
</script>

<div class="view">
  <header>
    <h1>Setlists</h1>
    <a class="back" href="#board">← Back to board</a>
  </header>

  <div class="panes">
    <!-- LEFT: the rig catalogue you drag from --------------------------- -->
    <section class="pane catalogue">
      <div class="pane-head">
        <h2>Rigs <span class="count">{shown.length}</span></h2>
        <input class="search" type="search" placeholder="Filter rigs…" bind:value={filter} />
      </div>

      <ul class="list">
        {#each shown as r (r)}
          <li
            class="cat-row"
            class:used={counts[r]}
            draggable="true"
            ondragstart={(e) => startCat(e, r)}
            ondragend={reset}
            ondblclick={() => addRig(r)}
            title="Drag into the setlist, or double-click to append"
          >
            <span class="grip">⋮⋮</span>
            <span class="name">{r}</span>
            {#if counts[r]}<span class="badge">×{counts[r]}</span>{/if}
            <button class="add" title="Add to setlist" onclick={() => addRig(r)}>＋</button>
          </li>
        {/each}
        {#if !shown.length}
          <li class="empty">{rigState.allRigs.length ? 'No rigs match.' : 'No rigs saved yet.'}</li>
        {/if}
      </ul>

      <div class="pane-foot">
        <input
          class="search"
          type="text"
          placeholder="Save current board as rig…"
          bind:value={rigName}
          onkeydown={(e) => e.key === 'Enter' && saveBoard()}
        />
        <button class="btn" onclick={saveBoard} disabled={!rigName.trim()}>Save</button>
      </div>
    </section>

    <!-- RIGHT: the setlist being edited --------------------------------- -->
    <section class="pane setlist">
      <div class="pane-head row">
        <select class="picker" value={sel} onchange={(e) => open(e.currentTarget.value)}>
          <option value="">＋ New setlist</option>
          {#each rigState.setlistNames as n}
            <option value={n}>{n}</option>
          {/each}
        </select>
        <input class="search" type="text" placeholder="Setlist name…" bind:value={name} />
        <button class="btn" onclick={save} disabled={!name.trim()}>Save</button>
        <button class="btn danger" onclick={del} disabled={!sel}>Delete</button>
      </div>

      <!-- The drop target: rows reorder live; the line shows where a drop lands. -->
      <ol
        class="list droplist"
        class:dragging={drag}
        ondragover={overList}
        ondrop={drop}
        ondragleave={(e) => e.currentTarget === e.target && (over = -1)}
      >
        {#each rigs as r, i (r + '-' + i)}
          {#if over === i}<li class="dropline"></li>{/if}
          <li
            class="rig-row"
            class:ghost={drag?.from === 'list' && drag.index === i}
            draggable="true"
            ondragstart={(e) => startList(e, i)}
            ondragend={reset}
          >
            <span class="grip">⋮⋮</span>
            <span class="idx">{i + 1}</span>
            <span class="name">{r}</span>
            <button class="remove" title="Remove" onclick={() => removeRig(i)}>✕</button>
          </li>
        {/each}
        {#if over === rigs.length}<li class="dropline"></li>{/if}

        {#if !rigs.length}
          <li class="empty drop-hint">Drag rigs here from the left to build your setlist.</li>
        {/if}
      </ol>

      <div class="pane-foot meta">
        {rigs.length} rig{rigs.length === 1 ? '' : 's'}{#if dirty && !sel} · unsaved{/if}
      </div>
    </section>
  </div>
</div>

<style>
  /* Fills the viewport like the board: fixed header, then a two-pane row that
     takes the rest; each pane scrolls its own list, the page never scrolls. */
  .view { display: flex; flex-direction: column; height: 100vh; }
  header {
    flex: 0 0 auto;
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    gap: var(--sp-4);
    padding: var(--sp-4) var(--sp-6);
    border-bottom: 1px solid var(--line);
    background: linear-gradient(180deg, var(--panel), var(--bg));
  }
  h1 { font-size: var(--fs-xl); margin: 0; font-weight: 600; }
  .back { color: var(--muted); text-decoration: none; font-size: var(--fs-sm); }
  .back:hover { color: var(--accent); }

  .panes {
    flex: 1 1 auto;
    min-height: 0;
    display: grid;
    grid-template-columns: minmax(280px, 1fr) minmax(320px, 1.3fr);
  }
  .pane {
    min-height: 0;
    display: flex;
    flex-direction: column;
    overflow: hidden;
  }
  .catalogue { border-right: 1px solid var(--line); }

  .pane-head {
    flex: 0 0 auto;
    display: flex;
    flex-direction: column;
    gap: var(--sp-3);
    padding: var(--sp-5) var(--sp-6);
    border-bottom: 1px solid var(--line);
  }
  .pane-head.row { flex-direction: row; flex-wrap: wrap; align-items: center; }
  h2 {
    font-size: var(--fs-sm);
    letter-spacing: var(--track-wide);
    text-transform: uppercase;
    color: var(--muted);
    margin: 0;
    display: flex;
    align-items: center;
    gap: var(--sp-3);
  }
  .count {
    font-size: var(--fs-xs);
    color: var(--faint);
    background: var(--inset);
    border-radius: var(--r-pill);
    padding: 1px var(--sp-3);
    font-variant-numeric: tabular-nums;
  }

  /* Inputs / selects / buttons share the inset-well chrome. */
  .search,
  .picker {
    background: var(--inset);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    padding: var(--sp-3) var(--sp-4);
    font: inherit;
  }
  .search { width: 100%; }
  .picker { flex: 0 0 auto; min-width: 150px; }
  .pane-head.row .search { flex: 1 1 140px; width: auto; }
  .search:focus,
  .picker:focus { outline: none; border-color: var(--accent); }

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

  /* The scrolling list area each pane owns. */
  .list {
    flex: 1 1 auto;
    overflow-y: auto;
    list-style: none;
    margin: 0;
    padding: var(--sp-4) var(--sp-5);
    display: flex;
    flex-direction: column;
    gap: var(--sp-2);
  }
  .droplist { gap: var(--sp-1); }

  .cat-row,
  .rig-row {
    display: flex;
    align-items: center;
    gap: var(--sp-3);
    padding: var(--sp-3) var(--sp-4);
    background: var(--panel-2);
    border: 1px solid var(--line);
    border-radius: var(--r-md);
    cursor: grab;
  }
  .cat-row:active,
  .rig-row:active { cursor: grabbing; }
  .cat-row:hover,
  .rig-row:hover { border-color: var(--line-2); }
  .cat-row.used { border-style: dashed; }
  .rig-row.ghost { opacity: .35; }

  .grip { color: var(--faint); cursor: grab; user-select: none; letter-spacing: -2px; }
  .idx {
    color: var(--muted);
    font-variant-numeric: tabular-nums;
    min-width: 18px;
    text-align: right;
  }
  .name { flex: 1; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .badge {
    font-size: var(--fs-xs);
    color: var(--accent);
    background: color-mix(in srgb, var(--accent) 14%, transparent);
    border-radius: var(--r-pill);
    padding: 1px var(--sp-3);
    font-variant-numeric: tabular-nums;
  }

  .add,
  .remove {
    flex: 0 0 auto;
    width: 26px;
    height: 26px;
    display: grid;
    place-items: center;
    background: var(--inset);
    color: var(--muted);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    cursor: pointer;
    transition: color var(--t-fast), border-color var(--t-fast);
  }
  .add:hover { color: var(--accent); border-color: var(--accent); }
  .remove:hover { color: var(--danger); border-color: var(--danger); }

  /* Insertion indicator while dragging into the setlist. */
  .dropline {
    height: 0;
    border-top: 2px solid var(--accent);
    box-shadow: var(--glow) var(--accent);
    border-radius: var(--r-pill);
    margin: 0 var(--sp-2);
    list-style: none;
  }
  .droplist.dragging { outline: 1px dashed var(--line-2); outline-offset: -4px; border-radius: var(--r-md); }

  .empty { color: var(--muted); font-size: var(--fs-sm); padding: var(--sp-4); }
  .drop-hint {
    margin: auto var(--sp-2);
    text-align: center;
    border: 1px dashed var(--line);
    border-radius: var(--r-lg);
    color: var(--faint);
  }

  .pane-foot {
    flex: 0 0 auto;
    display: flex;
    gap: var(--sp-3);
    align-items: center;
    padding: var(--sp-4) var(--sp-6);
    border-top: 1px solid var(--line);
  }
  .pane-foot.meta { color: var(--muted); font-size: var(--fs-sm); }

  @media (max-width: 820px) {
    .panes { grid-template-columns: 1fr; grid-template-rows: 1fr 1fr; }
    .catalogue { border-right: none; border-bottom: 1px solid var(--line); }
  }
</style>
