<script lang="ts">
  import { onMount } from 'svelte';
  import { api } from './lib/api.js';
  import {
    board,
    status,
    refresh,
    connectLive,
    pollTelemetry,
    applyState,
  } from './lib/store.svelte.js';
  import type { BoardState, Effect } from './lib/types.js';
  import Pedal from './lib/Pedal.svelte';
  import EmptySlot from './lib/EmptySlot.svelte';
  import FootswitchBar from './lib/FootswitchBar.svelte';
  import Setlists from './lib/Setlists.svelte';
  import Experiments from './lib/Experiments.svelte';

  // Tiny hash router: #experiments swaps in the component sandbox, no library.
  const routeOf = () => location.hash.replace(/^#\/?/, '');
  let route = $state(routeOf());

  let rigNames = $state<string[]>([]);
  let rigSel = $state('');
  let saveName = $state('');

  // The board mirrors the device's Input / FX / Output layout, all derived from
  // the one state document -- new effects appear with no UI changes.
  const inputFx = $derived(board.effects.filter((e) => e.section === 'input'));
  const outputFx = $derived(board.effects.filter((e) => e.section === 'output'));
  const grid = $derived.by<(Effect | null)[]>(() => {
    const bySlot: Record<number, Effect> = {};
    for (const e of board.effects) if (e.section === 'fx') bySlot[e.slot] = e;
    return Array.from({ length: board.fxSlotCount }, (_, s) => bySlot[s] ?? null);
  });

  const masterPct = $derived(Math.round(board.master * 100));
  const telem = $derived(`DSP ${(status.dspPermille / 10).toFixed(1)}%  ·  xruns ${status.xruns}`);

  async function loadRigNames() {
    try {
      rigNames = await api<string[]>('/api/rigs');
    } catch {
      /* ignore */
    }
  }

  onMount(() => {
    refresh().catch(console.error);
    loadRigNames();
    const es = connectLive();
    const t = setInterval(pollTelemetry, 1000);
    const onHash = () => (route = routeOf());
    window.addEventListener('hashchange', onHash);
    return () => {
      es.close();
      clearInterval(t);
      window.removeEventListener('hashchange', onHash);
    };
  });

  function setMaster(v: number) {
    board.master = v / 100;
    api('/api/master', { value: v / 100 }).catch(console.error);
  }
  function toggleBypass() {
    board.bypassed = !board.bypassed;
    api('/api/bypass', { bypassed: board.bypassed }).catch(console.error);
  }
  async function loadRig() {
    if (!rigSel) return;
    applyState(await api<BoardState>('/api/rig/load', { name: rigSel }));
  }
  async function saveRig() {
    const name = saveName.trim();
    if (!name) return;
    await api('/api/rig/save', { name });
    saveName = '';
    loadRigNames();
  }
</script>

{#if route === 'experiments'}
  <Experiments />
{:else}
<header>
  <div class="brand">
    <span class="dot" class:offline={!status.live} title={status.live ? 'Live' : 'Reconnecting…'}
    ></span>
    <h1>pi-Stomp <em>Worship Pedalboard</em></h1>
  </div>
  <div class="globals">
    <a class="lab" href="#experiments" title="Component sandbox">Lab</a>
    <button class="bypass" class:active={board.bypassed} onclick={toggleBypass}>Bypass</button>
    <label class="master">
      Master <output>{masterPct}%</output>
      <input
        type="range"
        min="0"
        max="200"
        step="1"
        value={masterPct}
        oninput={(e) => setMaster(+e.currentTarget.value)}
      />
    </label>
  </div>
</header>

<section class="toolbar">
  <label>
    <span class="title">Rig</span>
    <select bind:value={rigSel}>
      <option value="">—</option>
      {#each rigNames as n}
        <option value={n}>{n}</option>
      {/each}
    </select>
  </label>
  <button onclick={loadRig}>Load</button>
  <input type="text" placeholder="save as…" bind:value={saveName} />
  <button onclick={saveRig}>Save</button>
  <span class="telemetry">{telem}</span>
</section>

<Setlists />

<main class="board">
  <section class="lane">
    <h2 class="lane-title">Input</h2>
    <div class="lane-body">
      {#each inputFx as fx (fx.type)}
        <Pedal {fx} />
      {/each}
    </div>
  </section>

  <section class="lane lane-fx">
    <h2 class="lane-title">FX <span class="lane-sub">tap a footswitch · ◀ ▶ to move</span></h2>
    <FootswitchBar />
    <div class="fx-grid">
      {#each grid as fx, slot (fx ? fx.type : `empty-${slot}`)}
        {#if fx}
          <Pedal {fx} inGrid />
        {:else}
          <EmptySlot {slot} />
        {/if}
      {/each}
    </div>
  </section>

  <section class="lane">
    <h2 class="lane-title">Output</h2>
    <div class="lane-body">
      {#each outputFx as fx (fx.type)}
        <Pedal {fx} />
      {/each}
    </div>
  </section>
</main>
{/if}

<style>
  header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    flex-wrap: wrap;
    gap: var(--sp-4);
    padding: var(--sp-5) var(--sp-6);
    border-bottom: 1px solid var(--line);
    background: linear-gradient(180deg, var(--panel), var(--bg));
    position: sticky;
    top: 0;
    z-index: 5;
  }
  .brand { display: flex; align-items: center; gap: var(--sp-3); }
  .brand h1 { font-size: var(--fs-lg); margin: 0; font-weight: 600; letter-spacing: .2px; }
  .brand em { color: var(--accent); font-style: normal; font-weight: 500; }
  .dot {
    width: 10px;
    height: 10px;
    border-radius: 50%;
    background: var(--ok);
    box-shadow: var(--glow) var(--ok);
    transition: background var(--t-med), box-shadow var(--t-med);
  }
  .dot.offline { background: var(--muted); box-shadow: none; }

  .globals { display: flex; align-items: center; gap: var(--sp-5); }
  .lab {
    color: var(--muted);
    text-decoration: none;
    font-size: var(--fs-sm);
    font-weight: 600;
    letter-spacing: var(--track-wide);
    text-transform: uppercase;
    padding: var(--sp-2) var(--sp-3);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    transition: color var(--t-fast), border-color var(--t-fast);
  }
  .lab:hover { color: var(--accent); border-color: var(--accent); }
  .master { display: flex; align-items: center; gap: var(--sp-3); color: var(--muted); font-size: var(--fs-sm); }
  .master output { color: var(--text); min-width: 42px; text-align: right; }
  .master input { width: 130px; }
  .bypass {
    background: var(--panel-2);
    color: var(--text);
    border: 1px solid var(--line);
    padding: var(--sp-3) var(--sp-5);
    border-radius: var(--r-sm);
    cursor: pointer;
    font-weight: 600;
  }
  .bypass.active { background: var(--danger); border-color: var(--danger); color: #fff; }

  .toolbar {
    display: flex;
    align-items: center;
    gap: var(--sp-4);
    flex-wrap: wrap;
    padding: var(--sp-4) var(--sp-6);
    border-bottom: 1px solid var(--line);
    color: var(--muted);
  }
  .toolbar label { display: flex; align-items: center; gap: var(--sp-2); }
  .toolbar select,
  .toolbar input,
  .toolbar button {
    background: var(--panel-2);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    padding: 7px var(--sp-4);
    font: inherit;
  }
  .toolbar button { cursor: pointer; }
  .toolbar button:hover { border-color: var(--accent); }
  .toolbar .title { font-weight: 600; letter-spacing: var(--track); color: var(--text); }
  .telemetry { margin-left: auto; font-variant-numeric: tabular-nums; font-size: var(--fs-sm); }

  /* Lanes stack vertically (Input over FX over Output), each full width so the
     pedals + preset rows get room; within a lane pedals flow and wrap. */
  .board { display: flex; flex-direction: column; gap: var(--sp-5); padding: var(--sp-6); }
  .lane {
    background: var(--panel);
    border: 1px solid var(--line);
    border-radius: var(--r-xl);
    padding: var(--sp-4);
  }
  .lane-title {
    font-size: var(--fs-sm);
    margin: 0 0 var(--sp-4);
    letter-spacing: var(--track-wide);
    text-transform: uppercase;
    color: var(--muted);
    display: flex;
    align-items: baseline;
    gap: var(--sp-3);
  }
  .lane-sub { font-size: var(--fs-xs); letter-spacing: .2px; text-transform: none; color: var(--muted); opacity: .7; }
  .lane-body { display: grid; grid-template-columns: repeat(auto-fill, minmax(240px, 1fr)); gap: var(--sp-4); }

  /* FX grid: 4 columns, mirrors the device's 4x2 slot layout. */
  .fx-grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: var(--sp-4); }
  @media (max-width: 1200px) { .fx-grid { grid-template-columns: repeat(2, 1fr); } }
</style>
