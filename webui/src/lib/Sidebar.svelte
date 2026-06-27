<script lang="ts">
  // Left rail (collapsible) that FLOATS over the board: pick a setlist, then pick
  // a rig from it to load. "All rigs" lists the whole catalogue so nothing is
  // ever out of reach. Browsing state is shared (rigs.svelte.ts) with the top-bar
  // rig controls; authoring lives in the dedicated editor view (#setlists).
  import { ALL, rigState, chooseSetlist, pickRig, step } from './rigs.svelte.js';

  const KEY = 'pedalboard.sidebarCollapsed';

  let collapsed = $state(localStorage.getItem(KEY) === '1');
  $effect(() => localStorage.setItem(KEY, collapsed ? '1' : '0'));
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
        <select value={rigState.selSetlist} onchange={(e) => chooseSetlist(e.currentTarget.value)}>
          <option value={ALL}>All rigs</option>
          {#each rigState.setlistNames as n}
            <option value={n}>{n}</option>
          {/each}
        </select>
      </label>

      <ul class="riglist">
        {#each rigState.viewRigs as r (r)}
          <li>
            <button class="rig" class:active={r === rigState.activeRig} onclick={() => pickRig(r)}
              >{r}</button
            >
          </li>
        {/each}
        {#if !rigState.viewRigs.length}
          <li class="empty">{rigState.selSetlist === ALL ? 'No rigs saved yet' : 'Setlist is empty'}</li>
        {/if}
      </ul>

      <div class="stepper">
        <button onclick={() => step(-1)} disabled={!rigState.viewRigs.length}>◀ Prev</button>
        <button onclick={() => step(1)} disabled={!rigState.viewRigs.length}>Next ▶</button>
      </div>

      <button class="edit" onclick={() => (location.hash = '#setlists')}>Edit setlists…</button>
    </div>
  {/if}
</aside>

<style>
  /* Floats over the board (absolute within .shell), so expanding it never
     reflows Input/FX/Output -- it slides in front of them like a drawer. */
  .sidebar {
    position: absolute;
    top: 0;
    left: 0;
    bottom: 0;
    width: 300px;
    z-index: 20;
    overflow-y: auto;
    border-right: 1px solid var(--line);
    background: var(--panel);
    box-shadow: var(--shadow-2);
    display: flex;
    flex-direction: column;
    transition: width var(--t-med);
  }
  .sidebar.collapsed { width: 46px; overflow: hidden; box-shadow: var(--shadow-1); }

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

  /* Menu/list of rigs -- flat rows, not cards. */
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
  .rig:hover { background: var(--panel-2); }
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
