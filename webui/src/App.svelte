<script lang="ts">
  import { onMount } from 'svelte';
  import { api } from './lib/api.js';
  import {
    board,
    status,
    refresh,
    connectLive,
    pollTelemetry,
  } from './lib/store.svelte.js';
  import type { Effect } from './lib/types.js';
  import Pedal from './lib/Pedal.svelte';
  import EmptySlot from './lib/EmptySlot.svelte';
  import FootswitchBar from './lib/FootswitchBar.svelte';
  import Sidebar from './lib/Sidebar.svelte';
  import TunerButton from './lib/TunerButton.svelte';
  import TunerModal from './lib/TunerModal.svelte';
  import SetlistEditor from './lib/SetlistEditor.svelte';
  import Experiments from './lib/Experiments.svelte';

  let tunerOpen = $state(false);

  // Tiny hash router: #experiments swaps in the component sandbox, no library.
  const routeOf = () => location.hash.replace(/^#\/?/, '');
  let route = $state(routeOf());

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

  onMount(() => {
    refresh().catch(console.error);
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
</script>

{#if route === 'experiments'}
  <Experiments />
{:else if route === 'setlists'}
  <SetlistEditor />
{:else}
<div class="app">
  <header>
    <div class="brand">
      <span class="dot" class:offline={!status.live} title={status.live ? 'Live' : 'Reconnecting…'}
      ></span>
      <h1>pi-Stomp <em>Worship Pedalboard</em></h1>
    </div>

    <div class="globals">
      <span class="telemetry">{telem}</span>
      <TunerButton active={tunerOpen} onclick={() => (tunerOpen = true)} />
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

  <div class="shell">
    <Sidebar />

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
  </div>
</div>

{#if tunerOpen}
  <TunerModal onclose={() => (tunerOpen = false)} />
{/if}
{/if}

<style>
  /* App shell: fixed-height top bar, then a row that fills the viewport — the
     sidebar and board scroll independently, the page itself never scrolls. */
  .app { display: flex; flex-direction: column; height: 100vh; }

  header {
    flex: 0 0 auto;
    display: flex;
    justify-content: space-between;
    align-items: center;
    flex-wrap: wrap;
    gap: var(--sp-4);
    padding: var(--sp-4) var(--sp-6);
    border-bottom: 1px solid var(--line);
    background: linear-gradient(180deg, var(--panel), var(--bg));
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
  .telemetry { font-variant-numeric: tabular-nums; font-size: var(--fs-sm); color: var(--muted); }
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

  /* The body row: sidebar (own width/scroll) + board (fills, scrolls). */
  .shell { flex: 1 1 auto; display: flex; min-height: 0; }

  .board { flex: 1 1 auto; min-width: 0; overflow-y: auto; display: flex; flex-direction: column; gap: var(--sp-5); padding: var(--sp-6); }
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
