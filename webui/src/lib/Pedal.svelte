<script lang="ts">
  // Input/Output effect strip: a flat power + name header over its faders. No
  // card chrome and no per-pedal presets -- these effects are part of the rig
  // and saved with it. (The FX section uses FxTile, not this.)
  import { api } from './api.js';
  import type { Effect } from './types.js';
  import ParamControl from './ParamControl.svelte';

  let { fx }: { fx: Effect } = $props();

  function togglePower() {
    const on = !fx.enabled;
    fx.enabled = on; // optimistic; the SSE echo confirms
    api('/api/enabled', { effect: fx.type, enabled: on }).catch(console.error);
  }
</script>

<div class="pedal" class:off={!fx.enabled}>
  <div class="pedal-head">
    <h3>{fx.name}</h3>
    <button
      class="power"
      class:on={fx.enabled}
      title="Enable / disable"
      aria-label="Enable / disable {fx.name}"
      onclick={togglePower}
    ></button>
  </div>

  <div class="knobs">
    {#each fx.params as p (p.id)}
      <ParamControl effectType={fx.type} {p} knob />
    {/each}
  </div>
</div>

<style>
  .pedal { min-width: 0; transition: opacity var(--t-med); }
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

  /* On/off power dot -- same language as the FX tiles. */
  .power {
    flex: 0 0 auto;
    width: 22px;
    height: 22px;
    border-radius: 50%;
    border: 2px solid var(--line);
    background: var(--inset);
    cursor: pointer;
  }
  .power.on { border-color: var(--ok); box-shadow: var(--glow) var(--ok); background: var(--ok); }
</style>
