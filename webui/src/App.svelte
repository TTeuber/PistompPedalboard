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

  let presetNames = $state<string[]>([]);
  let presetSel = $state('');
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

  async function loadPresetNames() {
    try {
      presetNames = await api<string[]>('/api/presets');
    } catch {
      /* ignore */
    }
  }

  onMount(() => {
    refresh().catch(console.error);
    loadPresetNames();
    const es = connectLive();
    const t = setInterval(pollTelemetry, 1000);
    return () => {
      es.close();
      clearInterval(t);
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
  async function loadPreset() {
    if (!presetSel) return;
    applyState(await api<BoardState>('/api/preset/load', { name: presetSel }));
  }
  async function savePreset() {
    const name = saveName.trim();
    if (!name) return;
    await api('/api/preset/save', { name });
    saveName = '';
    loadPresetNames();
  }
</script>

<header>
  <div class="brand">
    <span class="dot" class:offline={!status.live} title={status.live ? 'Live' : 'Reconnecting…'}
    ></span>
    <h1>pi-Stomp <em>Worship Pedalboard</em></h1>
  </div>
  <div class="globals">
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

<section class="presets">
  <label>
    Preset
    <select bind:value={presetSel}>
      <option value="">—</option>
      {#each presetNames as n}
        <option value={n}>{n}</option>
      {/each}
    </select>
  </label>
  <button onclick={loadPreset}>Load</button>
  <input type="text" placeholder="save as…" bind:value={saveName} />
  <button onclick={savePreset}>Save</button>
  <span class="telemetry">{telem}</span>
</section>

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
