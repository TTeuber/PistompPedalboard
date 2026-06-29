<script lang="ts">
  // The input section's meters (see the user's sketch):
  //   - an input LEVEL bar in dBFS, with two reference lines for the noise-gate
  //     and compressor thresholds (read live from those pedals' params, so they
  //     track as you turn the knobs);
  //   - a shared GAIN REDUCTION bar showing the combined (gate + comp) attenuation
  //     the input section is applying right now.
  // Levels come from the fast /api/meters poll (store.meters); thresholds come
  // from the board state. Pure display -- no controls here.
  import { board, meters } from './store.svelte.js';
  import LevelBar from './controls/LevelBar.svelte';
  import { usePeakHold } from './controls/peakHold.svelte.js';

  type Marker = { value: number; color: string };

  const FLOOR = -60; // dBFS scale bottom (matches the server's meter floor)

  // Pull a param value out of an input-section pedal by effect type + param id.
  function paramOf(effectType: string, paramId: string): number | null {
    const fx = board.effects.find((e) => e.type === effectType);
    const p = fx?.params.find((q) => q.id === paramId);
    return p ? p.value : null;
  }

  const gateThresh = $derived(paramOf('gate', 'threshold'));
  const compThresh = $derived(paramOf('comp', 'threshold'));

  // Marker lines, only for pedals that exist. Gate = red, comp = blue -- matching
  // those pedals' knob tints. No captions: the lines read directly off the knobs.
  const markers = $derived.by<Marker[]>(() => {
    const out: Marker[] = [];
    if (gateThresh !== null) out.push({ value: gateThresh, color: 'var(--danger)' });
    if (compThresh !== null) out.push({ value: compThresh, color: 'var(--sapphire)' });
    return out;
  });

  // Slow-falling peak hold for the input level meter.
  const inPeak = usePeakHold(() => meters.inputDb, FLOOR);

  // Level colour follows the same green/amber/red policy as the device input VU
  // (input_vu.h digitalThresholds: warn -12 dBFS, clip -1 dBFS).
  const levelColor = $derived(
    meters.inputDb >= -1 ? 'var(--danger)' : meters.inputDb >= -12 ? 'var(--warn)' : 'var(--ok)',
  );

  const fmtDb = (v: number) => (v <= FLOOR + 0.5 ? '−∞' : v.toFixed(1));
</script>

<div class="meters">
  <div class="meter">
    <div class="row">
      <span class="label">Input</span>
      <span class="value">{fmtDb(meters.inputDb)} dB</span>
    </div>
    <LevelBar value={meters.inputDb} min={FLOOR} max={0} color={levelColor} {markers} peak={inPeak.value} />
  </div>

  <div class="meter">
    <LevelBar value={meters.grDb} min={0} max={20} color="var(--warn)" fillFrom="end" />
    <div class="row">
      <span class="label">Gain Reduction</span>
      <span class="value">{meters.grDb.toFixed(1)} dB</span>
    </div>
  </div>
</div>

<style>
  /* Sits in the empty space to the right of the input pedals; grows to fill it. */
  .meters {
    flex: 1 1 280px;
    min-width: 240px;
    align-self: stretch;
    display: flex;
    flex-direction: column;
    justify-content: center;
    gap: var(--sp-5);
    padding-block: var(--sp-5);
  }
  .meter { display: flex; flex-direction: column; gap: var(--sp-2); }
  .row { display: flex; align-items: baseline; justify-content: space-between; gap: var(--sp-3); }
  .label {
    font-size: var(--fs-xs);
    letter-spacing: var(--track-wide);
    text-transform: uppercase;
    color: var(--muted);
  }
  .value {
    font-family: var(--font-mono);
    font-size: var(--fs-sm);
    font-variant-numeric: tabular-nums;
    color: var(--text);
  }
</style>
