<script lang="ts">
  import { api } from './api.js';
  import type { Param } from './types.js';
  import Knob from './controls/Knob.svelte';
  import Fader from './controls/Fader.svelte';

  let {
    effectType,
    p,
    knob = false,
    size = 54,
    color = 'var(--accent)',
  }: {
    effectType: string;
    p: Param;
    knob?: boolean;
    size?: number;
    color?: string;
  } = $props();

  // A couple of params render as something other than a slider, matching the
  // device: a 0..1 unit-less value is an on/off toggle (e.g. Reverb "Freeze"); an
  // enumerated value is a dropdown -- keyed by param id (e.g. Tremolo's "shape").
  const ENUMS: Record<string, string[]> = { shape: ['Sine', 'Square'] };

  // Local mirror of the param value. We send this to the device on input, and
  // adopt the device's value when it changes underneath us -- UNLESS the user is
  // actively dragging, so a live SSE echo never fights the drag. This guard is
  // the whole reason live two-way sync feels smooth instead of jittery.
  //
  // $effect.pre seeds + re-syncs the mirror from the (reactive) prop before each
  // paint, so there's no initial flash and no stale-capture warning.
  let value = $state(0);
  let editing = $state(false);

  $effect.pre(() => {
    const incoming = p.value;
    if (!editing) value = incoming;
  });

  let timer: ReturnType<typeof setTimeout>;
  function send(v: number) {
    value = v;
    p.value = v; // optimistic: anything deriving from board state (e.g. the input
    //               meter's gate/comp lines) tracks the knob now, not on the echo
    clearTimeout(timer);
    timer = setTimeout(
      () => api('/api/param', { effect: effectType, param: p.id, value: v }).catch(console.error),
      30,
    );
  }

  // The Knob and Fader drive their own pointer/scroll/key interaction and only
  // emit values -- no drag start/end. So we hold the `editing` guard open while
  // inputs keep arriving and release it shortly after they stop, keeping a live
  // SSE echo from fighting the turn.
  let editTimer: ReturnType<typeof setTimeout>;
  function liveInput(v: number) {
    editing = true;
    clearTimeout(editTimer);
    editTimer = setTimeout(() => (editing = false), 250);
    send(v);
  }

  const enumOpts = $derived(ENUMS[p.id]);
  const isToggle = $derived(p.min === 0 && p.max === 1 && !p.unit && !enumOpts);
  const isKnob = $derived(knob && !isToggle && !enumOpts);
  // A param that swings either side of zero (e.g. EQ ±dB) rests at centre.
  const bipolar = $derived(p.min < 0 && p.max > 0);
  const fmt = (v: number) =>
    p.unit === '%' ? `${Math.round(v)}%` : p.unit ? `${v.toFixed(2)} ${p.unit}` : v.toFixed(2);
</script>

{#if isKnob}
  <div class="knob-cell" class:editing>
    <Knob
      value={value}
      min={p.min}
      max={p.max}
      label={p.name}
      {size}
      {color}
      {bipolar}
      start={-225}
      sweep={270}
      oninput={liveInput}
    />
    <!-- Name by default, value while hovering/turning (CSS swap). The name keeps
         the caption's box; the value overlays it. The hidden Knob label still
         names the control for assistive tech. -->
    <div class="kcap">
      <span class="kname">{p.name}</span>
      <span class="kval">{fmt(value)}</span>
    </div>
  </div>
{:else}
<div class="param">
  <label>
    {p.name}
    <output
      >{#if enumOpts}{enumOpts[Math.round(value)] ?? ''}{:else if isToggle}{value > 0.5
          ? 'on'
          : 'off'}{:else}{fmt(value)}{/if}</output
    >
  </label>

  {#if isToggle}
    <button class="toggle" class:on={value > 0.5} onclick={() => send(value > 0.5 ? 0 : 1)}>
      {value > 0.5 ? 'on' : 'off'}
    </button>
  {:else if enumOpts}
    <select value={Math.round(value)} onchange={(e) => send(+e.currentTarget.value)}>
      {#each enumOpts as name, i}
        <option value={i}>{name}</option>
      {/each}
    </select>
  {:else}
    <Fader value={value} min={p.min} max={p.max} {bipolar} {color} full oninput={liveInput} />
  {/if}
</div>
{/if}

<style>
  /* Knob variant: a caption under the knob that swaps name <-> value on
     hover/turn (CSS only). The name stays in flow to size the caption; the value
     overlays it. The Knob's own label is hidden (kept for its aria name). */
  .knob-cell { display: flex; flex-direction: column; align-items: center; gap: var(--sp-1); }
  .knob-cell :global(.knob .label) { display: none; }
  .kcap {
    position: relative;
    font-size: var(--fs-xs);
    letter-spacing: var(--track);
    white-space: nowrap;
  }
  .kname { text-transform: uppercase; color: var(--muted); transition: opacity var(--t-fast); }
  .kval {
    position: absolute;
    left: 50%;
    top: 0;
    transform: translateX(-50%);
    color: var(--text);
    font-variant-numeric: tabular-nums;
    opacity: 0;
    pointer-events: none;
    transition: opacity var(--t-fast);
  }
  /* On hover or while turning, reveal the value and fade out the name. */
  .knob-cell:hover .kname,
  .knob-cell.editing .kname { opacity: 0; }
  .knob-cell:hover .kval,
  .knob-cell.editing .kval { opacity: 1; }

  .param { margin: var(--sp-4) 0; }
  .param label {
    display: flex;
    justify-content: space-between;
    font-size: var(--fs-sm);
    color: var(--muted);
    margin-bottom: var(--sp-2);
  }
  .param output { color: var(--text); font-variant-numeric: tabular-nums; }
  .param select {
    width: 100%;
    background: var(--panel-2);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    padding: 7px;
  }
  .toggle {
    width: 100%;
    padding: var(--sp-3);
    border-radius: var(--r-sm);
    cursor: pointer;
    background: var(--panel-2);
    color: var(--muted);
    border: 1px solid var(--line);
    font-weight: 600;
  }
  .toggle.on { background: var(--accent); color: var(--ink); border-color: var(--accent); }
</style>
