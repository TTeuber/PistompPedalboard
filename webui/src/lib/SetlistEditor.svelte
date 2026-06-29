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

  // Header chrome: the disclosure menu (pick a setlist) and inline rename.
  let menuOpen = $state(false);
  let renaming = $state(false);
  let nameBeforeRename = ''; // snapshot so Escape can revert

  // Last-saved snapshot the working state is diffed against to detect edits.
  let baseName = $state('');
  let baseRigs = $state<string[]>([]);
  // An action parked behind the unsaved-changes prompt (null = no prompt open).
  let pending = $state<null | (() => void | Promise<void>)>(null);
  // Whether the delete-confirmation prompt is open.
  let confirmingDelete = $state(false);

  const sameList = (a: string[], b: string[]) =>
    a.length === b.length && a.every((x, i) => x === b[i]);
  // True when the working name or rig order differs from the last save.
  const dirty = $derived(name.trim() !== baseName.trim() || !sameList(rigs, baseRigs));

  // Snapshot the current working state as the clean baseline.
  function markClean() {
    baseName = name;
    baseRigs = [...rigs];
  }
  // Run `action` now, or park it behind the save/discard prompt if there are
  // unsaved edits. Used by every "leave this setlist" path.
  function guard(action: () => void | Promise<void>) {
    menuOpen = false;
    if (dirty) pending = action;
    else action();
  }
  async function promptSave() {
    const next = pending;
    pending = null;
    await save();
    await next?.();
  }
  function promptDiscard() {
    const next = pending;
    pending = null;
    next?.();
  }

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

  onMount(() => {
    loadLists();
  });

  async function open(n: string) {
    menuOpen = false;
    renaming = false;
    if (!n) return newSetlist();
    const s = await api<Setlist>(`/api/setlist?name=${encodeURIComponent(n)}`);
    sel = s.name;
    name = s.name;
    rigs = s.rigs;
    markClean();
  }
  // New, empty setlist -- drop straight into the rename field so it gets a name.
  function newSetlist() {
    sel = '';
    name = '';
    rigs = [];
    menuOpen = false;
    markClean();
    startRename();
  }

  // ---- title rename (inline) --------------------------------------------
  // The pencil swaps the title for a text field; the name isn't written until
  // Save (which renames the file on disk when it differs from the open one).
  function startRename() {
    nameBeforeRename = name;
    menuOpen = false;
    renaming = true;
  }
  function stopRename() {
    renaming = false;
  }
  function cancelRename() {
    name = nameBeforeRename;
    renaming = false;
  }
  // Focus + select the field as soon as it mounts.
  function autofocus(el: HTMLInputElement) {
    el.focus();
    el.select();
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
    const prev = sel; // the setlist this view was opened from (if any)
    await api('/api/setlist/save', { name: n, rigs });
    // Renaming an existing setlist: the save wrote a new file, so drop the old.
    if (prev && prev !== n) await api('/api/setlist/delete', { name: prev });
    sel = n;
    name = n;
    renaming = false;
    markClean();
    await loadLists();
  }
  function requestDelete() {
    if (!sel) return;
    menuOpen = false;
    confirmingDelete = true;
  }
  async function del() {
    confirmingDelete = false;
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
    <a
      class="back"
      href="#board"
      onclick={(e) => {
        e.preventDefault();
        guard(() => {
          location.hash = '#board';
        });
      }}>← Back to board</a
    >
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
      <div class="pane-head setlist-head">
        {#if renaming}
          <input
            class="title-input"
            type="text"
            placeholder="Setlist name…"
            bind:value={name}
            use:autofocus
            onkeydown={(e) => {
              if (e.key === 'Enter') stopRename();
              else if (e.key === 'Escape') cancelRename();
            }}
            onblur={stopRename}
          />
        {:else}
          <div class="title-group">
            <button
              class="disclosure"
              class:open={menuOpen}
              class:placeholder={!name}
              onclick={() => (menuOpen = !menuOpen)}
              title="Choose a setlist"
            >
              <svg class="tri" viewBox="0 0 16 16" width="13" height="13" aria-hidden="true">
                <path d="M3 5.5h10L8 12z" fill="currentColor" />
              </svg>
              <span class="title">{name || 'New setlist'}</span>{#if dirty}<span
                  class="star"
                  title="Unsaved changes">*</span
                >{/if}
            </button>
            <button class="icon-btn flat" onclick={startRename} title="Rename" aria-label="Rename">
              <svg viewBox="0 0 24 24" width="17" height="17" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <path d="M12 20h9" /><path d="M16.5 3.5a2.12 2.12 0 0 1 3 3L7 19l-4 1 1-4z" />
              </svg>
            </button>
          </div>
        {/if}

        <div class="actions">
          <button class="icon-btn" onclick={() => guard(newSetlist)} title="New setlist" aria-label="New setlist">
            <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round">
              <path d="M12 5v14M5 12h14" />
            </svg>
          </button>
          <button class="icon-btn" onclick={save} disabled={!name.trim()} title="Save" aria-label="Save">
            <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z" />
              <path d="M17 21v-8H7v8M7 3v5h8" />
            </svg>
          </button>
          <button class="icon-btn danger" onclick={requestDelete} disabled={!sel} title="Delete" aria-label="Delete">
            <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
              <path d="M3 6h18M8 6V4a1 1 0 0 1 1-1h6a1 1 0 0 1 1 1v2m2 0v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6" />
              <path d="M10 11v6M14 11v6" />
            </svg>
          </button>
        </div>

        {#if menuOpen}
          <button class="backdrop" aria-label="Close menu" onclick={() => (menuOpen = false)}></button>
          <ul class="menu">
            {#each rigState.setlistNames as n}
              <li>
                <button class:active={n === sel} onclick={() => guard(() => open(n))}>{n}</button>
              </li>
            {/each}
            {#if !rigState.setlistNames.length}
              <li class="empty">No setlists saved yet.</li>
            {/if}
          </ul>
        {/if}
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
        {rigs.length} rig{rigs.length === 1 ? '' : 's'}{#if dirty} · unsaved changes{/if}
      </div>
    </section>
  </div>

  {#if pending}
    <div class="modal-overlay" role="dialog" aria-modal="true" aria-label="Unsaved changes">
      <div class="modal">
        <h3>Unsaved changes</h3>
        <p>
          You have unsaved changes to “{name.trim() || 'this setlist'}”. Save them before leaving?
        </p>
        <div class="modal-actions">
          <button class="btn" onclick={() => (pending = null)}>Cancel</button>
          <button class="btn danger" onclick={promptDiscard}>Discard</button>
          <button class="btn primary" onclick={promptSave} disabled={!name.trim()}>Save</button>
        </div>
        {#if !name.trim()}<p class="hint">Name the setlist first to save it.</p>{/if}
      </div>
    </div>
  {/if}

  {#if confirmingDelete}
    <div class="modal-overlay" role="dialog" aria-modal="true" aria-label="Delete setlist">
      <div class="modal">
        <h3>Delete setlist</h3>
        <p>Delete “{sel}”? This can't be undone. (The rigs in it aren't affected.)</p>
        <div class="modal-actions">
          <button class="btn" onclick={() => (confirmingDelete = false)}>Cancel</button>
          <button class="btn danger" onclick={del}>Delete</button>
        </div>
      </div>
    </div>
  {/if}
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
  /* Setlist pane header: title + disclosure menu on the left, actions right. */
  .setlist-head {
    position: relative;
    flex-direction: row;
    align-items: center;
    justify-content: space-between;
    gap: var(--sp-3);
  }
  .title-group { display: flex; align-items: center; gap: var(--sp-1); min-width: 0; }
  .disclosure {
    display: flex;
    align-items: center;
    gap: var(--sp-2);
    min-width: 0;
    background: none;
    border: none;
    padding: var(--sp-2) 0;
    color: var(--text);
    font: inherit;
    font-size: var(--fs-lg);
    font-weight: 600;
    cursor: pointer;
  }
  .disclosure .title { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .disclosure.placeholder .title { color: var(--muted); font-weight: 500; }
  .star { flex: 0 0 auto; color: var(--accent); margin-left: 1px; }
  .disclosure:hover .title { color: var(--accent); }
  .tri { flex: 0 0 auto; color: var(--muted); transition: transform var(--t-fast); }
  .disclosure.open .tri { transform: rotate(180deg); }
  .disclosure:hover .tri { color: var(--accent); }

  .title-input {
    flex: 1 1 auto;
    min-width: 0;
    background: var(--inset);
    color: var(--text);
    border: 1px solid var(--accent);
    border-radius: var(--r-sm);
    padding: var(--sp-2) var(--sp-3);
    font: inherit;
    font-size: var(--fs-lg);
    font-weight: 600;
  }
  .title-input:focus { outline: none; }

  .actions { display: flex; align-items: center; gap: var(--sp-2); flex: 0 0 auto; }
  .icon-btn {
    flex: 0 0 auto;
    width: 34px;
    height: 34px;
    display: grid;
    place-items: center;
    background: var(--inset);
    color: var(--muted);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    cursor: pointer;
    transition: color var(--t-fast), border-color var(--t-fast);
  }
  .icon-btn:hover:not(:disabled) { color: var(--accent); border-color: var(--accent); }
  .icon-btn.danger:hover:not(:disabled) { color: var(--danger); border-color: var(--danger); }
  .icon-btn:disabled { opacity: .4; cursor: default; }
  .icon-btn.flat { width: 30px; height: 30px; background: none; border: none; }
  .icon-btn.flat:hover { color: var(--accent); }

  /* Disclosure dropdown: list of setlists to switch to. */
  .backdrop { position: fixed; inset: 0; z-index: 15; background: none; border: none; cursor: default; }
  .menu {
    position: absolute;
    top: calc(100% - var(--sp-3));
    left: var(--sp-6);
    z-index: 20;
    min-width: 220px;
    max-height: 320px;
    overflow-y: auto;
    list-style: none;
    margin: 0;
    padding: var(--sp-2);
    background: var(--panel-2);
    border: 1px solid var(--line-2);
    border-radius: var(--r-md);
    box-shadow: 0 8px 24px rgba(0, 0, 0, .4);
  }
  .menu li { list-style: none; }
  .menu li button {
    display: block;
    width: 100%;
    text-align: left;
    background: none;
    border: none;
    color: var(--text);
    font: inherit;
    padding: var(--sp-3) var(--sp-4);
    border-radius: var(--r-sm);
    cursor: pointer;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .menu li button:hover { background: var(--inset); }
  .menu li button.active { color: var(--accent); }
  .menu .empty { color: var(--muted); padding: var(--sp-3) var(--sp-4); font-size: var(--fs-sm); }

  /* Unsaved-changes prompt shown when leaving a dirty setlist. */
  .modal-overlay {
    position: fixed;
    inset: 0;
    z-index: 50;
    display: grid;
    place-items: center;
    background: rgba(0, 0, 0, .55);
    padding: var(--sp-5);
  }
  .modal {
    width: min(420px, 100%);
    background: var(--panel);
    border: 1px solid var(--line-2);
    border-radius: var(--r-lg);
    padding: var(--sp-6);
    box-shadow: 0 16px 48px rgba(0, 0, 0, .5);
  }
  .modal h3 { margin: 0 0 var(--sp-3); font-size: var(--fs-lg); font-weight: 600; }
  .modal p { margin: 0; color: var(--muted); font-size: var(--fs-sm); line-height: 1.5; }
  .modal-actions {
    display: flex;
    justify-content: flex-end;
    gap: var(--sp-3);
    margin-top: var(--sp-6);
  }
  .modal .hint { margin-top: var(--sp-3); color: var(--faint); font-size: var(--fs-xs); text-align: right; }

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

  /* Inputs / buttons share the inset-well chrome. */
  .search {
    width: 100%;
    background: var(--inset);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    padding: var(--sp-3) var(--sp-4);
    font: inherit;
  }
  .search:focus { outline: none; border-color: var(--accent); }

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
  .btn:disabled { opacity: .4; cursor: default; }
  .btn.primary { background: var(--accent); border-color: var(--accent); color: var(--bg); }
  .btn.primary:hover:not(:disabled) { filter: brightness(1.08); }
  .btn.danger:hover:not(:disabled) { border-color: var(--danger); color: var(--danger); }

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
