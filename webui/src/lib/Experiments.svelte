<script lang="ts">
  // Isolated sandbox for in-progress components, reached at #experiments. Nothing
  // here touches the live board — it's a place to shape primitives before they
  // graduate into the real UI.
  import Knob from './controls/Knob.svelte';
  import Fader from './controls/Fader.svelte';

  // Token swatches to eyeball the theme at a glance.
  const swatches = [
    'gold', 'gold-bright', 'gold-dim',
    'ruby', 'emerald', 'sapphire', 'amethyst', 'topaz', 'teal',
    'bg', 'panel', 'panel-2', 'inset', 'line', 'text', 'muted',
  ];

  // Jewel-toned knob grid, each independently live.
  const jewels = [
    { label: 'Drive', color: 'var(--ruby)' },
    { label: 'Tone', color: 'var(--topaz)' },
    { label: 'Level', color: 'var(--emerald)' },
    { label: 'Mix', color: 'var(--sapphire)' },
    { label: 'Depth', color: 'var(--amethyst)' },
    { label: 'Decay', color: 'var(--teal)' },
  ];
  let knobVals = $state(jewels.map((_, i) => 0.2 + i * 0.13));

  let big = $state(0.5);
  let h1 = $state(0.7);
  let h2 = $state(0.35);
  let v1 = $state(0.6);
  let v2 = $state(0.85);

  const pct = (v: number) => `${Math.round(v * 100)}%`;
</script>

<div class="lab">
  <header>
    <h1>Component Lab</h1>
    <a class="back" href="#board">← Back to board</a>
  </header>

  <section>
    <h2>Palette</h2>
    <div class="swatches">
      {#each swatches as name}
        <div class="swatch">
          <div class="chip" style="background: var(--{name})"></div>
          <code>--{name}</code>
        </div>
      {/each}
    </div>
  </section>

  <section>
    <h2>Knob — square pie-wipe</h2>
    <p class="hint">Drag vertically · Shift = fine · scroll · arrows · double-click resets</p>
    <div class="row">
      {#each jewels as j, i}
        <div class="cell">
          <Knob bind:value={knobVals[i]} label={j.label} color={j.color} />
          <span class="val">{pct(knobVals[i])}</span>
        </div>
      {/each}
    </div>
    <div class="row">
      <div class="cell">
        <Knob bind:value={big} label="Master" size={140} />
        <span class="val">{pct(big)}</span>
      </div>
      <div class="cell">
        <Knob bind:value={big} label="270° sweep" size={140} start={-225} sweep={270} />
        <span class="val">{pct(big)}</span>
      </div>
    </div>
  </section>

  <section>
    <h2>Fader — rectangle fill</h2>
    <div class="row">
      <div class="cell">
        <Fader bind:value={h1} label="Input" />
        <span class="val">{pct(h1)}</span>
      </div>
      <div class="cell">
        <Fader bind:value={h2} label="Output" color="var(--emerald)" />
        <span class="val">{pct(h2)}</span>
      </div>
    </div>
    <div class="row">
      <div class="cell">
        <Fader bind:value={v1} label="L" vertical color="var(--sapphire)" />
        <span class="val">{pct(v1)}</span>
      </div>
      <div class="cell">
        <Fader bind:value={v2} label="R" vertical color="var(--ruby)" />
        <span class="val">{pct(v2)}</span>
      </div>
    </div>
  </section>
</div>

<style>
  .lab { max-width: 980px; margin: 0 auto; padding: var(--sp-6); }
  header { display: flex; align-items: baseline; justify-content: space-between; margin-bottom: var(--sp-7); }
  h1 { font-size: var(--fs-xl); margin: 0; font-weight: 600; }
  h1::after { content: ' ▟'; color: var(--accent); }
  .back { color: var(--muted); text-decoration: none; font-size: var(--fs-sm); }
  .back:hover { color: var(--accent); }

  section {
    background: var(--panel);
    border: 1px solid var(--line);
    border-radius: var(--r-xl);
    padding: var(--sp-5) var(--sp-6);
    margin-bottom: var(--sp-5);
  }
  h2 {
    font-size: var(--fs-sm);
    letter-spacing: var(--track-wide);
    text-transform: uppercase;
    color: var(--muted);
    margin: 0 0 var(--sp-4);
  }
  .hint { font-size: var(--fs-sm); color: var(--faint); margin: 0 0 var(--sp-5); }

  .swatches { display: grid; grid-template-columns: repeat(auto-fill, minmax(120px, 1fr)); gap: var(--sp-4); }
  .swatch { display: flex; flex-direction: column; gap: var(--sp-2); }
  .chip { height: 48px; border-radius: var(--r-md); border: 1px solid var(--line); }
  code { font-family: var(--font-mono); font-size: var(--fs-xs); color: var(--muted); }

  .row { display: flex; flex-wrap: wrap; gap: var(--sp-7); align-items: flex-end; margin-bottom: var(--sp-5); }
  .row:last-child { margin-bottom: 0; }
  .cell { display: flex; flex-direction: column; align-items: center; gap: var(--sp-2); }
  .val { font-variant-numeric: tabular-nums; font-size: var(--fs-sm); color: var(--text); }
</style>
