<script lang="ts">
  // Display-only horizontal level bar (the input/output meters + the gain-
  // reduction meter). Not interactive -- unlike Fader.svelte, there are no
  // pointer handlers; it just paints `value` on a min..max scale. CSS (not SVG)
  // so it stretches to fill its container by percentage with no stroke distortion
  // and the threshold markers land at clean left:% positions.
  //
  //   - fillFrom 'start' grows from the left (a level meter);
  //   - fillFrom 'end' grows from the right (the gain-reduction meter, matching
  //     the sketch where reduction eats in from the right edge).
  //   - markers draw thin vertical lines (the gate / comp thresholds), with an
  //     optional caption beneath.

  let {
    value = 0,
    min = 0,
    max = 1,
    color = 'var(--ok)',
    fillFrom = 'start',
    markers = [],
    peak = null,
    height = 22,
  }: {
    value?: number;
    min?: number;
    max?: number;
    color?: string;
    fillFrom?: 'start' | 'end';
    markers?: { value: number; color: string; label?: string }[];
    // Slow-falling peak-hold position (same units as value); null hides it.
    peak?: number | null;
    height?: number;
  } = $props();

  const span = $derived(max - min || 1);
  const pct = (v: number) => Math.min(100, Math.max(0, ((v - min) / span) * 100));
  const fillPct = $derived(pct(value));
  const hasCaptions = $derived(markers.some((m) => m.label));
  const showPeak = $derived(peak !== null && peak > min);
</script>

<div class="levelbar">
  <div class="track" style="height:{height}px">
    <div
      class="fill"
      class:from-end={fillFrom === 'end'}
      style="width:{fillPct}%; background:{color}"
    ></div>
    {#each markers as m (m.label ?? m.value)}
      <div class="marker" style="left:{pct(m.value)}%; background:{m.color}"></div>
    {/each}
    {#if showPeak}
      <div class="peak" style="left:{pct(peak as number)}%"></div>
    {/if}
  </div>
  {#if hasCaptions}
    <div class="captions" style="height:24px">
      {#each markers as m (m.label ?? m.value)}
        {#if m.label}
          <span class="caption" style="left:{pct(m.value)}%; color:{m.color}">{m.label}</span>
        {/if}
      {/each}
    </div>
  {/if}
</div>

<style>
  .levelbar { width: 100%; min-width: 120px; }
  .track {
    position: relative;
    width: 100%;
    background: var(--inset);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    overflow: hidden;
  }
  .fill {
    position: absolute;
    top: 0;
    left: 0;
    height: 100%;
    border-radius: var(--r-sm) 0 0 var(--r-sm);
    /* Smooth the 15 Hz poll into a continuous meter sweep. */
    transition: width var(--t-fast) linear;
  }
  .fill.from-end {
    left: auto;
    right: 0;
    border-radius: 0 var(--r-sm) var(--r-sm) 0;
  }
  /* Threshold markers ride above the fill, overhanging the track top and bottom
     so they read as reference lines rather than part of the level. */
  .marker {
    position: absolute;
    top: -3px;
    bottom: -3px;
    width: 2px;
    transform: translateX(-1px);
    border-radius: 1px;
  }
  /* Peak-hold indicator: a bright tick that snaps up to new highs and is eased
     back down slowly by the parent, marking where the level is cresting. */
  .peak {
    position: absolute;
    top: -2px;
    bottom: -2px;
    width: 2px;
    transform: translateX(-1px);
    background: var(--text);
    border-radius: 1px;
    box-shadow: var(--glow) var(--text);
    transition: left var(--t-fast) linear;
  }
  .captions { position: relative; margin-top: var(--sp-2); }
  .caption {
    position: absolute;
    transform: translateX(-50%);
    font-size: var(--fs-xs);
    letter-spacing: var(--track);
    white-space: nowrap;
  }
</style>
