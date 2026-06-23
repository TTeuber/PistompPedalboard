<script lang="ts">
  import { api } from './api.js';
  import type { Param } from './types.js';

  let { effectType, p }: { effectType: string; p: Param } = $props();

  // A couple of params render as something other than a slider, matching the
  // device: a 0..1 unit-less value is an on/off toggle (e.g. Reverb "Freeze");
  // an enumerated value is a dropdown.
  const ENUMS: Record<string, string[]> = { type: ['Overdrive', 'Distortion', 'Fuzz'] };

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
    clearTimeout(timer);
    timer = setTimeout(
      () => api('/api/param', { effect: effectType, param: p.id, value: v }).catch(console.error),
      30,
    );
  }

  const enumOpts = $derived(ENUMS[p.id]);
  const isToggle = $derived(p.min === 0 && p.max === 1 && !p.unit && !enumOpts);
  const step = $derived(p.max - p.min <= 4 ? 0.01 : 1);
  const fmt = (v: number) =>
    p.unit === '%' ? `${Math.round(v)}%` : p.unit ? `${v.toFixed(2)} ${p.unit}` : v.toFixed(2);
</script>

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
    <input
      type="range"
      min={p.min}
      max={p.max}
      {step}
      {value}
      oninput={(e) => send(+e.currentTarget.value)}
      onpointerdown={() => (editing = true)}
      onpointerup={() => (editing = false)}
      onpointercancel={() => (editing = false)}
      onkeydown={() => (editing = true)}
      onkeyup={() => (editing = false)}
      onblur={() => (editing = false)}
    />
  {/if}
</div>
