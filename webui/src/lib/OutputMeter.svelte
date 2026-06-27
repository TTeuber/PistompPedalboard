<script lang="ts">
  // The output section's level meter: a single dBFS bar, post-master. Same fast
  // /api/meters poll and the same green/amber/red policy as the input level
  // (input_vu.h: warn -12 dBFS, clip -1 dBFS) so clipping at the output reads red.
  import { meters } from './store.svelte.js';
  import LevelBar from './controls/LevelBar.svelte';

  const FLOOR = -60;
  const levelColor = $derived(
    meters.outputDb >= -1 ? 'var(--danger)' : meters.outputDb >= -12 ? 'var(--warn)' : 'var(--ok)',
  );
  const fmtDb = (v: number) => (v <= FLOOR + 0.5 ? '−∞' : v.toFixed(1));
</script>

<div class="meter">
  <div class="row">
    <span class="label">Output</span>
    <span class="value">{fmtDb(meters.outputDb)} dB</span>
  </div>
  <LevelBar value={meters.outputDb} min={FLOOR} max={0} color={levelColor} />
</div>

<style>
  .meter {
    flex: 1 1 280px;
    min-width: 240px;
    align-self: center;
    margin-left: auto;
    display: flex;
    flex-direction: column;
    gap: var(--sp-2);
  }
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
