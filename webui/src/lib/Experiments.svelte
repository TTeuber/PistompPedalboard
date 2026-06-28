<script lang="ts">
  // Isolated sandbox for in-progress components, reached at #experiments. Nothing
  // here touches the live board — it's a place to shape primitives before they
  // graduate into the real UI.
  //
  // Right now it's being used to trial CONTRAST fixes: every section below shows
  // the current look (left) against a proposed look (right). The "proposed" side
  // is driven by local --p-* tokens defined in the style block — nothing global changes
  // until we like it and promote those values into app.css.

  // Token swatches to eyeball the theme at a glance.
  const swatches = [
    'gold', 'gold-bright', 'gold-dim',
    'ruby', 'emerald', 'sapphire', 'amethyst', 'topaz', 'teal',
    'bg', 'panel', 'panel-2', 'inset', 'line', 'text', 'muted',
  ];

  // Four pedals to mirror the real FX grid in the tile demos.
  const pedals = [
    { name: 'Drive',  color: 'var(--ruby)' },
    { name: 'Chorus', color: 'var(--sapphire)' },
    { name: 'Delay',  color: 'var(--teal)' },
    { name: 'Reverb', color: 'var(--amethyst)' },
  ];
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

  <!-- ============================================================ -->
  <!-- CONTRAST TRIALS                                              -->
  <!-- ============================================================ -->

  <section>
    <h2>Surface elevation</h2>
    <p class="hint">
      The neutral stack currently lives in a ~1.1:1 band — page, well, card and
      control are nearly the same black. Proposed: widen the steps so each layer
      reads as raised (or recessed) on its own, without leaning on a border.
    </p>
    <div class="compare">
      <div class="col">
        <span class="tag tag-before">Current</span>
        <div class="stack">
          <div class="lvl" style="background: var(--bg)">bg <code>#0a0a0c</code></div>
          <div class="lvl" style="background: var(--inset)">inset <code>#070709</code></div>
          <div class="lvl" style="background: var(--panel)">panel <code>#131316</code></div>
          <div class="lvl" style="background: var(--panel-2)">panel-2 <code>#1a1a1f</code></div>
        </div>
      </div>
      <div class="col proposed">
        <span class="tag tag-after">Proposed</span>
        <div class="stack">
          <div class="lvl" style="background: var(--p-bg)">bg <code>#0a0a0c</code></div>
          <div class="lvl" style="background: var(--p-inset)">inset <code>#15151b</code></div>
          <div class="lvl" style="background: var(--p-panel)">panel <code>#1c1c23</code></div>
          <div class="lvl" style="background: var(--p-panel-2)">panel-2 <code>#26262e</code></div>
        </div>
      </div>
    </div>
  </section>

  <section>
    <h2>FX pedal tile — resting fill</h2>
    <p class="hint">
      Today an unselected tile is <code>background: transparent</code> — it's just
      a faint hairline on the page, so only the selected pedal reads as solid.
      Proposed: give every tile a resting fill so it reads as an object; the accent
      rail + brighter fill stay reserved for the selected one.
    </p>
    <div class="compare">
      <div class="col">
        <span class="tag tag-before">Current</span>
        <div class="stage">
          {#each pedals as p, i}
            <div class="tile" class:selected={i === 1}>
              <span class="dot" style="background: {p.color}"></span>
              <span class="tname">{p.name}</span>
              <span class="sw" class:on={i === 1}><span class="knob"></span></span>
            </div>
          {/each}
        </div>
      </div>
      <div class="col proposed">
        <span class="tag tag-after">Proposed</span>
        <div class="stage proposed-tiles">
          {#each pedals as p, i}
            <div class="tile" class:selected={i === 1}>
              <span class="dot" style="background: {p.color}"></span>
              <span class="tname">{p.name}</span>
              <span class="sw" class:on={i === 1}><span class="knob"></span></span>
            </div>
          {/each}
        </div>
      </div>
    </div>
  </section>

  <section>
    <h2>Empty slot</h2>
    <p class="hint">
      The <code>+</code> slot fills with <code>--inset (#070709)</code> — darker
      than the page, so it reads as a hole, not a drop target. Proposed: a faintly
      raised fill and a clearly brighter dashed border so it reads as an open slot.
    </p>
    <div class="compare">
      <div class="col">
        <span class="tag tag-before">Current</span>
        <div class="stage">
          <div class="slot">+</div>
          <div class="slot">+</div>
        </div>
      </div>
      <div class="col proposed">
        <span class="tag tag-after">Proposed</span>
        <div class="stage proposed-tiles">
          <div class="slot p-slot">+</div>
          <div class="slot p-slot">+</div>
        </div>
      </div>
    </div>
  </section>

  <section>
    <h2>Header buttons & borders</h2>
    <p class="hint">
      Outline buttons (Tuner / LAB / Bypass) and inputs sit on <code>--panel-2</code>
      with a <code>~1.4:1</code> border — barely pressable. Proposed: brighter idle
      border (raise <code>--line</code>) and a touch more fill so chrome reads as
      interactive.
    </p>
    <div class="compare">
      <div class="col">
        <span class="tag tag-before">Current</span>
        <div class="stage stage-pad">
          <div class="btn-row">
            <button class="hbtn">Tuner</button>
            <button class="hbtn">LAB</button>
            <button class="hbtn">Bypass</button>
          </div>
          <select class="inp"><option>preset…</option></select>
          <input class="inp" placeholder="save as…" />
        </div>
      </div>
      <div class="col proposed">
        <span class="tag tag-after">Proposed</span>
        <div class="stage stage-pad proposed-tiles">
          <div class="btn-row">
            <button class="hbtn p-hbtn">Tuner</button>
            <button class="hbtn p-hbtn">LAB</button>
            <button class="hbtn p-hbtn">Bypass</button>
          </div>
          <select class="inp p-inp"><option>preset…</option></select>
          <input class="inp p-inp" placeholder="save as…" />
        </div>
      </div>
    </div>
  </section>
</div>

<style>
  /* ---- proposed token set (local trial; promote to app.css if liked) ---- */
  .proposed {
    --p-bg:       #0a0a0c;  /* page — unchanged */
    --p-inset:    #15151b;  /* recessed wells now read above the page */
    --p-panel:    #1c1c23;  /* cards lift clear of the page */
    --p-panel-2:  #26262e;  /* controls lift clear of the card */
    --p-line:     #45454f;  /* hairline ~2:1 vs page (was ~1.4:1) */
    --p-line-2:   #5c5c68;  /* emphasis / dashed targets */
  }

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
  .hint { font-size: var(--fs-sm); color: var(--faint); margin: 0 0 var(--sp-5); line-height: 1.5; }
  .hint code, .lvl code { font-family: var(--font-mono); font-size: .9em; color: var(--muted); }

  /* ---- palette ---- */
  .swatches { display: grid; grid-template-columns: repeat(auto-fill, minmax(120px, 1fr)); gap: var(--sp-4); }
  .swatch { display: flex; flex-direction: column; gap: var(--sp-2); }
  .chip { height: 48px; border-radius: var(--r-md); border: 1px solid var(--line); }
  code { font-family: var(--font-mono); font-size: var(--fs-xs); color: var(--muted); }

  /* ---- before/after scaffolding ---- */
  .compare { display: grid; grid-template-columns: 1fr 1fr; gap: var(--sp-5); }
  .col { display: flex; flex-direction: column; gap: var(--sp-3); min-width: 0; }
  .tag {
    align-self: flex-start;
    font-size: var(--fs-xs);
    letter-spacing: var(--track-wide);
    text-transform: uppercase;
    font-weight: 700;
    padding: 2px var(--sp-2);
    border-radius: var(--r-sm);
  }
  .tag-before { color: var(--muted); border: 1px solid var(--line); }
  .tag-after { color: var(--ink); background: var(--accent); }

  /* The stage simulates the board background the real tiles sit on, so contrast
     is judged against the page black — not against the lighter lab panel. */
  .stage {
    background: var(--bg);
    border-radius: var(--r-lg);
    padding: var(--sp-4);
    display: flex;
    flex-direction: column;
    gap: var(--sp-3);
  }
  .proposed-tiles { background: var(--p-bg); }
  .stage-pad { gap: var(--sp-4); }

  /* ---- surface stack ---- */
  .stack { display: flex; flex-direction: column; gap: 0; border-radius: var(--r-lg); overflow: hidden; }
  .lvl {
    padding: var(--sp-4) var(--sp-5);
    font-size: var(--fs-sm);
    color: var(--text);
    display: flex;
    justify-content: space-between;
    align-items: center;
  }

  /* ---- pedal tiles ---- */
  .tile {
    display: flex;
    align-items: center;
    gap: var(--sp-3);
    padding: var(--sp-3);
    min-height: 64px;
    border: 1px solid var(--line);
    border-radius: var(--r-md);
    background: transparent;          /* current: no resting fill */
  }
  .tile.selected { border-color: var(--accent); background: var(--panel-2); }

  /* proposed: every tile gets a resting fill + brighter border */
  .proposed-tiles .tile { background: var(--p-panel); border-color: var(--p-line); }
  .proposed-tiles .tile.selected { background: var(--p-panel-2); border-color: var(--accent); box-shadow: inset 3px 0 0 var(--accent); }

  .dot { flex: 0 0 auto; width: 14px; height: 14px; border-radius: 4px; }
  .tname { flex: 1; font-size: var(--fs-sm); font-weight: 600; color: var(--text); }

  /* ---- switches (off-state contrast) ---- */
  .sw {
    flex: 0 0 auto;
    width: 40px; height: 22px;
    border-radius: var(--r-pill);
    background: var(--inset);
    border: 1px solid var(--line);
    position: relative;
  }
  .sw .knob {
    position: absolute; top: 2px; left: 2px;
    width: 16px; height: 16px; border-radius: 50%;
    background: var(--muted);
    transition: left var(--t-fast);
  }
  .sw.on { background: var(--accent); border-color: var(--accent); }
  .sw.on .knob { left: 20px; background: var(--ink); }
  /* proposed: lighter track + brighter thumb so OFF is legible */
  .proposed-tiles .sw { background: var(--p-inset); border-color: var(--p-line); }
  .proposed-tiles .sw .knob { background: #9b958a; }

  /* ---- empty slots ---- */
  .slot {
    min-height: 64px;
    border: 1px dashed var(--line);
    border-radius: var(--r-lg);
    background: var(--inset);          /* current: darker than page */
    display: flex; align-items: center; justify-content: center;
    color: var(--muted);
    font-size: 24px; font-weight: 300;
  }
  .p-slot { background: var(--p-inset); border-color: var(--p-line-2); color: var(--p-line-2); }

  /* ---- header buttons + inputs ---- */
  .btn-row { display: flex; gap: var(--sp-3); }
  .hbtn {
    background: var(--panel-2);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    padding: var(--sp-3) var(--sp-4);
    font: inherit; font-weight: 600; cursor: pointer;
  }
  .p-hbtn { background: var(--p-panel-2); border-color: var(--p-line); }
  .inp {
    width: 100%;
    background: var(--inset);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    padding: var(--sp-2) var(--sp-3);
    font: inherit; font-size: var(--fs-sm);
  }
  .p-inp { background: var(--p-inset); border-color: var(--p-line); }
</style>
