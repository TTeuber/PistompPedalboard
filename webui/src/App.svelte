<script lang="ts">
  import { onMount } from 'svelte';
  import { api } from './lib/api.js';
  import {
    board,
    status,
    refresh,
    connectLive,
    pollTelemetry,
    pollMeters,
  } from './lib/store.svelte.js';
  import type { Effect } from './lib/types.js';
  import Pedal from './lib/Pedal.svelte';
  import InputMeters from './lib/InputMeters.svelte';
  import OutputMeter from './lib/OutputMeter.svelte';
  import FxTile from './lib/FxTile.svelte';
  import FxParamsPanel from './lib/FxParamsPanel.svelte';
  import EmptySlot from './lib/EmptySlot.svelte';
  import FootswitchBar from './lib/FootswitchBar.svelte';
  import Sidebar from './lib/Sidebar.svelte';
  import RigControls from './lib/RigControls.svelte';
  import { loadLists } from './lib/rigs.svelte.js';
  import TunerButton from './lib/TunerButton.svelte';
  import TunerModal from './lib/TunerModal.svelte';
  import SetlistEditor from './lib/SetlistEditor.svelte';
  import Experiments from './lib/Experiments.svelte';
  import AddFxModal from './lib/AddFxModal.svelte';
  import Fader from './lib/controls/Fader.svelte';
  import Button from './lib/controls/Button.svelte';
  import TempoControl from './lib/TempoControl.svelte';

  let tunerOpen = $state(false);
  // The FX slot the add-effect modal will fill, or null when it's closed.
  let addSlot = $state<number | null>(null);

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

  // Which FX pedal the right-hand panel is editing. Tracked by the stable `type`
  // id so an SSE state swap (or a removed/reordered pedal) just falls back to the
  // placeholder rather than leaving a stale reference.
  let selectedFxType = $state<string | null>(null);
  const selectedFx = $derived(
    board.effects.find((e) => e.section === 'fx' && e.type === selectedFxType) ?? null,
  );

  const masterPct = $derived(Math.round(board.master * 100));
  const telem = $derived(`DSP ${(status.dspPermille / 10).toFixed(1)}%  ·  xruns ${status.xruns}`);

  onMount(() => {
    refresh().catch(console.error);
    loadLists().catch(console.error);
    const es = connectLive();
    const t = setInterval(pollTelemetry, 1000);
    const m = setInterval(pollMeters, 66); // ~15 Hz: live enough for level meters
    const onHash = () => (route = routeOf());
    window.addEventListener('hashchange', onHash);
    return () => {
      es.close();
      clearInterval(t);
      clearInterval(m);
      window.removeEventListener('hashchange', onHash);
    };
  });

  function setMaster(v: number) {
    board.master = v;
    api('/api/master', { value: v }).catch(console.error);
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
      <span class="logo" title="pi-Stomp">π</span>
    </div>

    <RigControls />

    <div class="globals">
      <span class="telemetry">{telem}</span>
      <TunerButton active={tunerOpen} onclick={() => (tunerOpen = true)} />
      <Button href="#experiments" title="Component sandbox">Lab</Button>
      <TempoControl />
      <div class="master">
        <span class="master-label">Master</span>
        <Fader
          value={board.master}
          min={0}
          max={2}
          length={130}
          thickness={20}
          oninput={setMaster}
        />
        <output>{masterPct}%</output>
      </div>
    </div>
  </header>

  <div class="shell">
    <Sidebar />

    <main class="board">
      <!-- Input + Output ride above FX so the whole board fits without scrolling;
           neither is in a card, just a titled rack of flat strips. -->
      <!-- Left column: Input + Output above FX. The right pedal-controls column
           rides alongside, full height, so the board fits without scrolling. -->
      <div class="board-main">
        <section class="lane">
          <div class="rack">
            {#each inputFx as fx (fx.type)}
              <Pedal {fx} />
            {/each}
            <InputMeters />
          </div>
        </section>

        <section class="lane lane-fx">
          <div class="fx-grid">
            {#each grid as fx, slot (fx ? fx.type : `empty-${slot}`)}
              {#if fx}
                <FxTile
                  {fx}
                  selected={fx.type === selectedFxType}
                  onselect={(t) => (selectedFxType = t)}
                />
              {:else}
                <EmptySlot {slot} onadd={(s) => (addSlot = s)} />
              {/if}
            {/each}
          </div>
          <FootswitchBar />
        </section>

        <section class="lane lane-out">
          <div class="rack">
            {#each outputFx as fx (fx.type)}
              <Pedal {fx} />
            {/each}
            <OutputMeter />
          </div>
        </section>
      </div>

      <FxParamsPanel fx={selectedFx} />
    </main>
  </div>
</div>

{#if tunerOpen}
  <TunerModal onclose={() => (tunerOpen = false)} />
{/if}
{#if addSlot !== null}
  <AddFxModal slot={addSlot} onclose={() => (addSlot = null)} />
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
  .logo {
    font-size: 26px;
    line-height: 1;
    font-weight: 700;
    color: var(--accent);
    font-family: var(--font-mono);
  }
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
  .master { display: flex; align-items: center; gap: var(--sp-3); color: var(--muted); font-size: var(--fs-sm); }
  .master-label { letter-spacing: var(--track); text-transform: uppercase; font-size: var(--fs-xs); }
  .master output { color: var(--text); min-width: 42px; text-align: right; font-variant-numeric: tabular-nums; }

  /* The body row: the sidebar floats over the board (position:absolute within
     this relative box), so the board always spans the full width and only the
     collapsed rail's worth of left padding is reserved for it. */
  .shell { position: relative; flex: 1 1 auto; min-height: 0; }

  /* Two columns: the stacked Input/Output/FX on the left, the pedal-controls
     panel as a full-height column on the right (its divider runs top to bottom).
     Collapses to one column when the board gets narrow. */
  .board {
    height: 100%;
    overflow-y: auto;
    display: grid;
    grid-template-columns: 1fr minmax(260px, 320px);
    /* No grid gap: the dividers do the separating, so the right-hand panel's
       border-left meets the lanes' border-tops with no gap at the corners.
       Breathing room comes from each bordered box's own padding instead. */
    gap: 0;
    /* No outer padding: the dividers run all the way to the viewport edges. Only
       the collapsed sidebar rail is reserved on the left; every gutter is now
       internal to the bordered boxes (lanes/panel) so content still breathes. */
    padding: 0;
    padding-left: 46px;
  }
  /* Fill the board's height so the FX lane can grow and pin Output to the bottom. */
  .board-main { min-width: 0; min-height: 100%; display: flex; flex-direction: column; }
  .board :global(.panel) { border-left: 1px solid var(--line); padding: var(--sp-5) var(--sp-6); }
  /* Sections are no longer cards -- just titled groups. The left/right padding is
     the gutter inside each section; the border-top still spans the full padding
     box, so the rules run edge to edge (left rail to the panel divider). */
  .lane { min-width: 0; padding-left: var(--sp-5); padding-right: var(--sp-6); }

  /* Each section's effects sit in one wrapping row, divided by thin rules. The
     items stretch to the full lane height so their dividers run edge to edge and
     meet the horizontal section rules above/below (no gaps). */
  .rack { display: flex; flex-wrap: wrap; gap: var(--sp-5); align-items: stretch; }
  /* Direct children only -- the inner .meter rows inside InputMeters must not pick
     up a divider. The meters get a divider too (input level after the comp,
     output level after the output gain). */
  .rack > :global(.pedal:not(:first-child)),
  .rack > :global(.meters),
  .rack > :global(.meter) { border-left: 1px solid var(--line); padding-left: var(--sp-5); }

  /* Stack order is Input, FX, Output -- each separated from the last by a rule.
     No padding-top here: the rule sits flush at the lane edge so the racks'
     vertical dividers touch it; the lanes' own content carries the gutter (the
     FX title needs an explicit one since it isn't a padded rack item). */
  .lane-fx,
  .lane-out { border-top: 1px solid var(--line); }
  /* FX grows to absorb the slack so Output is pinned to the bottom of the board;
     it carries top + bottom padding since its content isn't a padded rack. The
     column lets the grid take the slack while the footswitch bar stays pinned
     below it. */
  .lane-fx {
    flex: 1 1 auto;
    display: flex;
    flex-direction: column;
    padding-top: var(--sp-5);
    padding-bottom: var(--sp-5);
  }

  /* FX grid: 4 columns, mirrors the device's 4x2 slot layout. It fills the lane's
     spare height and splits it evenly across rows (grid-auto-rows: 1fr) so the
     tiles stretch to absorb the slack instead of leaving empty space below. The
     taller tiles then flip to a vertical layout -- see FxTile. */
  .fx-grid {
    flex: 1 1 auto;
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    grid-auto-rows: 1fr;
    gap: var(--sp-4);
  }

  @media (max-width: 1100px) {
    .board { grid-template-columns: 1fr; }
    .board :global(.panel) {
      border-left: none;
      padding-left: 0;
      border-top: 1px solid var(--line);
      padding-top: var(--sp-4);
    }
    .fx-grid { grid-template-columns: repeat(2, 1fr); }
  }
</style>
