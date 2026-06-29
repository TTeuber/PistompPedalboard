<script lang="ts">
  // Input/Output effect strip: a flat power + name header over its faders. No
  // card chrome and no per-pedal presets -- these effects are part of the rig
  // and saved with it. (The FX section uses FxTile, not this.)
  import { api } from './api.js';
  import type { Effect } from './types.js';
  import ParamControl from './ParamControl.svelte';
  import Switch from './controls/Switch.svelte';

  let { fx }: { fx: Effect } = $props();

  // Per-effect knob tint: the gate/comp match their reference lines on the input
  // meter, amp is emerald, eq is amethyst; the input/output gains stay gold.
  const KNOB_COLORS: Record<string, string> = {
    gate: 'var(--danger)',
    comp: 'var(--sapphire)',
    amp: 'var(--emerald)',
    eq: 'var(--amethyst)',
  };
  const knobColor = $derived(KNOB_COLORS[fx.type] ?? 'var(--accent)');

  // The input/output gain stages are always on -- no power switch on those.
  const togglable = $derived(fx.type !== 'input' && fx.type !== 'output');

  function togglePower() {
    const on = !fx.enabled;
    fx.enabled = on; // optimistic; the SSE echo confirms
    api('/api/enabled', { effect: fx.type, enabled: on }).catch(console.error);
  }
</script>

<div class="pedal" class:off={togglable && !fx.enabled}>
  <div class="pedal-head">
    <h3>{fx.name}</h3>
    {#if togglable}
      <Switch on={fx.enabled} label="Enable / disable {fx.name}" onclick={togglePower} />
    {/if}
  </div>

  <div class="knobs">
    {#each fx.params as p (p.id)}
      <ParamControl effectType={fx.type} {p} knob color={knobColor} />
    {/each}
  </div>
</div>

<style>
  /* Vertical padding is the gutter that clears the section rules above/below; the
     box itself stretches to the full lane height so its left divider runs edge to
     edge. */
  .pedal { min-width: 0; padding: var(--sp-5) 0; transition: opacity var(--t-med); }
  .pedal.off { opacity: .5; }
  .pedal-head { display: flex; align-items: center; justify-content: space-between; gap: var(--sp-3); margin-bottom: var(--sp-3); }

  /* Params sit in one horizontal cluster of knobs under the header. */
  .knobs { display: flex; flex-wrap: wrap; gap: var(--sp-4); align-items: flex-start; }
  .pedal-head h3 {
    font-size: var(--fs-sm);
    margin: 0;
    letter-spacing: var(--track);
    text-transform: uppercase;
    color: var(--accent);
  }
</style>
