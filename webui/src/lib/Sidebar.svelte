<script lang="ts">
  // Left rail (collapsible): pick a setlist, then pick a rig from it to load onto
  // the board. "All rigs" lists the whole catalogue so nothing is ever out of
  // reach. Authoring rigs/setlists lives in the dedicated editor view (#setlists).
  import { onMount } from 'svelte';
  import { api } from './api.js';
  import { applyState } from './store.svelte.js';
  import type { BoardState, Setlist } from './types.js';

  const ALL = '__all__';
  const KEY = 'pedalboard.sidebarCollapsed';

  let collapsed = $state(localStorage.getItem(KEY) === '1');
  $effect(() => localStorage.setItem(KEY, collapsed ? '1' : '0'));

  let setlistNames = $state<string[]>([]);
  let allRigs = $state<string[]>([]);
  let selSetlist = $state(ALL);
  let viewRigs = $state<string[]>([]); // rigs shown for the current selection
  let activeRig = $state(''); // last rig we loaded (for highlight + stepping)

  async function loadLists() {
    try {
      [setlistNames, allRigs] = await Promise.all([
        api<string[]>('/api/setlists'),
        api<string[]>('/api/rigs'),
      ]);
    } catch {
      /* ignore */
    }
    chooseSetlist(selSetlist);
  }

  onMount(loadLists);

  async function chooseSetlist(name: string) {
    selSetlist = name;
    if (name === ALL) {
      viewRigs = allRigs;
      return;
    }
    try {
      const s = await api<Setlist>(`/api/setlist?name=${encodeURIComponent(name)}`);
      viewRigs = s.rigs;
    } catch {
      viewRigs = [];
    }
  }

  async function pickRig(name: string) {
    activeRig = name;
    applyState(await api<BoardState>('/api/rig/load', { name }));
  }
  function step(dir: number) {
    if (!viewRigs.length) return;
    const cur = viewRigs.indexOf(activeRig);
    const next =
      cur < 0 ? (dir > 0 ? 0 : viewRigs.length - 1) : Math.min(viewRigs.length - 1, Math.max(0, cur + dir));
    pickRig(viewRigs[next]);
  }
</script>

<aside class="sidebar" class:collapsed>
  <div class="head">
    {#if !collapsed}<span class="head-title">Library</span>{/if}
    <button
      class="collapse"
      title={collapsed ? 'Expand sidebar' : 'Collapse sidebar'}
      onclick={() => (collapsed = !collapsed)}
    >
      {collapsed ? '»' : '«'}
    </button>
  </div>

  {#if !collapsed}
    <div class="content">
      <label class="field">
        <span class="field-label">Setlist</span>
        <select value={selSetlist} onchange={(e) => chooseSetlist(e.currentTarget.value)}>
          <option value={ALL}>All rigs</option>
          {#each setlistNames as n}
            <option value={n}>{n}</option>
          {/each}
        </select>
      </label>

      <ul class="riglist">
        {#each viewRigs as r (r)}
          <li>
            <button class="rig" class:active={r === activeRig} onclick={() => pickRig(r)}>{r}</button>
          </li>
        {/each}
        {#if !viewRigs.length}
          <li class="empty">{selSetlist === ALL ? 'No rigs saved yet' : 'Setlist is empty'}</li>
        {/if}
      </ul>

      <div class="stepper">
        <button onclick={() => step(-1)} disabled={!viewRigs.length}>◀ Prev</button>
        <button onclick={() => step(1)} disabled={!viewRigs.length}>Next ▶</button>
      </div>

      <button class="edit" onclick={() => (location.hash = '#setlists')}>Edit setlists…</button>
    </div>
  {/if}
</aside>

<style>
  .sidebar {
    flex: 0 0 300px;
    width: 300px;
    overflow-y: auto;
    border-right: 1px solid var(--line);
    background: var(--bg);
    display: flex;
    flex-direction: column;
    transition: flex-basis var(--t-med), width var(--t-med);
  }
  .sidebar.collapsed { flex-basis: 46px; width: 46px; overflow: hidden; }

  .head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: var(--sp-3);
    padding: var(--sp-4);
    border-bottom: 1px solid var(--line);
  }
  .head-title {
    font-size: var(--fs-sm);
    letter-spacing: var(--track-wide);
    text-transform: uppercase;
    color: var(--muted);
  }
  .collapse {
    flex: 0 0 auto;
    width: 26px;
    height: 26px;
    border-radius: var(--r-sm);
    border: 1px solid var(--line);
    background: var(--panel-2);
    color: var(--muted);
    cursor: pointer;
    font-size: var(--fs-sm);
  }
  .collapse:hover { color: var(--accent); border-color: var(--accent); }

  .content { display: flex; flex-direction: column; gap: var(--sp-4); padding: var(--sp-4); }

  .field { display: flex; flex-direction: column; gap: var(--sp-2); }
  .field-label {
    font-size: var(--fs-xs);
    letter-spacing: var(--track-wide);
    text-transform: uppercase;
    color: var(--muted);
  }
  .field select {
    width: 100%;
    background: var(--panel-2);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    padding: var(--sp-3) var(--sp-4);
    font: inherit;
  }

  /* Menu/list of rigs — flat rows, not cards. */
  .riglist { list-style: none; margin: 0; padding: 0; display: flex; flex-direction: column; }
  .rig {
    width: 100%;
    text-align: left;
    background: none;
    border: none;
    border-left: 2px solid transparent;
    color: var(--text);
    font: inherit;
    padding: var(--sp-3) var(--sp-4);
    border-radius: var(--r-sm);
    cursor: pointer;
    transition: background var(--t-fast), color var(--t-fast);
  }
  .rig:hover { background: var(--panel); }
  .rig.active {
    background: color-mix(in srgb, var(--accent) 14%, transparent);
    border-left-color: var(--accent);
    color: var(--accent);
    font-weight: 600;
  }
  .empty { color: var(--faint); font-size: var(--fs-sm); padding: var(--sp-3) var(--sp-4); }

  .stepper { display: flex; gap: var(--sp-2); }
  .stepper button {
    flex: 1;
    background: var(--panel-2);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    padding: var(--sp-3) 0;
    font: inherit;
    cursor: pointer;
  }
  .stepper button:hover:not(:disabled) { border-color: var(--accent); }
  .stepper button:disabled { opacity: .4; cursor: default; }

  .edit {
    background: none;
    border: 1px dashed var(--line);
    color: var(--muted);
    border-radius: var(--r-sm);
    padding: var(--sp-3);
    font: inherit;
    font-weight: 600;
    cursor: pointer;
    transition: color var(--t-fast), border-color var(--t-fast);
  }
  .edit:hover { color: var(--accent); border-color: var(--accent); }
</style>
