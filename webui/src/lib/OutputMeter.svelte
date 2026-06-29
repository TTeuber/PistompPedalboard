<script lang="ts">
  // The output section's level meters: one dBFS bar per channel (L over R),
  // post-master. Same fast /api/meters poll and the same green/amber/red policy
  // as the input level (input_vu.h: warn -12 dBFS, clip -1 dBFS) so clipping at
  // the output reads red. The lower (R) meter wears its label underneath so the
  // pair's captions bracket the group.
  import { meters } from './store.svelte.js';
  import LevelBar from './controls/LevelBar.svelte';
  import { usePeakHold } from './controls/peakHold.svelte.js';

  const FLOOR = -60;
  const colorFor = (db: number) =>
    db >= -1 ? 'var(--danger)' : db >= -12 ? 'var(--warn)' : 'var(--ok)';
  const fmtDb = (v: number) => (v <= FLOOR + 0.5 ? '−∞' : v.toFixed(1));

  const lColor = $derived(colorFor(meters.outputDbL));
  const rColor = $derived(colorFor(meters.outputDbR));
  const lPeak = usePeakHold(() => meters.outputDbL, FLOOR);
  const rPeak = usePeakHold(() => meters.outputDbR, FLOOR);
</script>

<div class="meters">
  <div class="meter">
    <div class="row">
      <span class="label">Output L</span>
      <span class="value">{fmtDb(meters.outputDbL)} dB</span>
    </div>
    <LevelBar value={meters.outputDbL} min={FLOOR} max={0} color={lColor} peak={lPeak.value} />
  </div>

  <div class="meter">
    <LevelBar value={meters.outputDbR} min={FLOOR} max={0} color={rColor} peak={rPeak.value} />
    <div class="row">
      <span class="label">Output R</span>
      <span class="value">{fmtDb(meters.outputDbR)} dB</span>
    </div>
  </div>
</div>

<style>
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
