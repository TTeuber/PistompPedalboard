<script lang="ts">
  // Knob that "wipes" a fill around its centre as the value rises, with a tick
  // marking the leading edge (the sketch shape). Pure SVG geometry — no assets —
  // so it recolours from a single CSS var and scales crisply.
  //
  // Shapes: 'square' (ray meets the square edge) or 'circle' (ray meets a circle).
  // Bipolar: the fill grows out from the centre of the sweep toward the value
  // instead of from the start — for params that swing either side of a neutral.
  //
  // Interaction: drag vertically (Shift = fine), scroll wheel, arrow keys, and
  // double-click to reset. Value is $bindable for easy two-way use.
  let {
    value = $bindable(0),
    min = 0,
    max = 1,
    label = '',
    size = 76,
    color = 'var(--accent)',
    start = 90, // where the fill begins; 90 = bottom (6 o'clock), wiping clockwise
    sweep = 360, // total degrees the fill can travel, clockwise
    shape = 'square', // 'square' | 'circle'
    bipolar = false, // fill from the sweep centre outward instead of from start
    resetValue = bipolar ? min + (max - min) / 2 : min,
    oninput = undefined,
  }: {
    value?: number;
    min?: number;
    max?: number;
    label?: string;
    size?: number;
    color?: string;
    start?: number;
    sweep?: number;
    shape?: 'square' | 'circle';
    bipolar?: boolean;
    resetValue?: number;
    oninput?: (v: number) => void;
  } = $props();

  const PAD = 5; // inset so the fill sits inside the outline
  const span = $derived(max - min || 1);
  const t = $derived(Math.min(1, Math.max(0, (value - min) / span)));

  // Ray from the centre to wherever it meets the edge at `deg` — the square's
  // perimeter, or a circle of radius r.
  function edge(deg: number, c: number, r: number): [number, number] {
    const a = (deg * Math.PI) / 180;
    const dx = Math.cos(a);
    const dy = Math.sin(a);
    if (shape === 'circle') return [c + dx * r, c + dy * r];
    const m = Math.max(Math.abs(dx), Math.abs(dy));
    return [c + (dx / m) * r, c + (dy / m) * r];
  }

  // The swept wedge as a polygon: centre + perimeter samples between the two
  // bounding angles. Straight square edges stay exact between samples; circle
  // edges are smooth enough at a 2° step.
  const points = $derived.by(() => {
    const half = size / 2;
    const r = half - PAD;
    const aCur = start + sweep * t;
    let a0: number, a1: number;
    if (bipolar) {
      const aMid = start + sweep * 0.5;
      a0 = Math.min(aMid, aCur);
      a1 = Math.max(aMid, aCur);
      if (a1 - a0 < 0.5) return ''; // sitting at centre — nothing to fill
    } else {
      if (t <= 0) return '';
      a0 = start;
      a1 = aCur;
    }
    const pts = [`${half},${half}`];
    for (let a = a0; a < a1; a += 2) {
      const [x, y] = edge(a, half, r);
      pts.push(`${x.toFixed(2)},${y.toFixed(2)}`);
    }
    const [ex, ey] = edge(a1, half, r);
    pts.push(`${ex.toFixed(2)},${ey.toFixed(2)}`);
    return pts.join(' ');
  });

  // Leading-edge indicator line (also the rest position when value is at min).
  const tick = $derived.by(() => {
    const half = size / 2;
    const [x2, y2] = edge(start + sweep * t, half, half - PAD);
    return { c: half, x2, y2 };
  });

  // Neutral mark at the sweep centre, so a bipolar knob shows where zero is.
  const centerMark = $derived.by(() => {
    if (!bipolar) return null;
    const half = size / 2;
    const aMid = start + sweep * 0.5;
    const [ox, oy] = edge(aMid, half, half - PAD);
    const [ix, iy] = edge(aMid, half, half - PAD - 7);
    return { ox, oy, ix, iy };
  });

  function set(v: number) {
    value = Math.min(max, Math.max(min, v));
    oninput?.(value);
  }

  let dragging = $state(false);
  let lastY = 0;
  function down(e: PointerEvent) {
    dragging = true;
    lastY = e.clientY;
    (e.currentTarget as Element).setPointerCapture(e.pointerId);
  }
  function move(e: PointerEvent) {
    if (!dragging) return;
    const dy = e.clientY - lastY;
    lastY = e.clientY;
    set(value - (dy / 180) * span * (e.shiftKey ? 0.25 : 1)); // 180px ≈ full sweep
  }
  function up() {
    dragging = false;
  }
  function wheel(e: WheelEvent) {
    e.preventDefault();
    set(value - Math.sign(e.deltaY) * (span / 100) * (e.shiftKey ? 1 : 5));
  }
  function keydown(e: KeyboardEvent) {
    const k = e.key;
    if (k === 'ArrowUp' || k === 'ArrowRight') set(value + (span / 100) * (e.shiftKey ? 1 : 5));
    else if (k === 'ArrowDown' || k === 'ArrowLeft') set(value - (span / 100) * (e.shiftKey ? 1 : 5));
    else return;
    e.preventDefault();
  }
</script>

<button
  class="knob"
  style="--knob:{color}; width:{size}px"
  aria-label={label || 'knob'}
  onpointerdown={down}
  onpointermove={move}
  onpointerup={up}
  onpointercancel={up}
  ondblclick={() => set(resetValue)}
  onwheel={wheel}
  onkeydown={keydown}
>
  <svg viewBox="0 0 {size} {size}" width={size} height={size}>
    {#if shape === 'circle'}
      <circle class="frame" cx={size / 2} cy={size / 2} r={size / 2 - PAD} />
    {:else}
      <rect class="frame" x={PAD} y={PAD} width={size - PAD * 2} height={size - PAD * 2} rx="3" />
    {/if}
    {#if points}<polygon class="fill" {points} />{/if}
    {#if centerMark}
      <line class="center" x1={centerMark.ox} y1={centerMark.oy} x2={centerMark.ix} y2={centerMark.iy} />
    {/if}
    <line class="tick" x1={tick.c} y1={tick.c} x2={tick.x2} y2={tick.y2} />
  </svg>
  {#if label}<span class="label">{label}</span>{/if}
</button>

<style>
  .knob {
    background: none;
    border: none;
    padding: 0;
    margin: 0;
    cursor: ns-resize;
    display: inline-flex;
    flex-direction: column;
    align-items: center;
    gap: var(--sp-2);
    touch-action: none;
    user-select: none;
    -webkit-user-select: none;
    font: inherit;
  }
  .frame {
    fill: var(--inset);
    stroke: var(--line);
    stroke-width: 2;
    transition: stroke var(--t-fast);
  }
  .knob:focus-visible { outline: none; }
  .knob:focus-visible .frame { stroke: var(--knob); }
  .fill { fill: var(--knob); }
  .tick { stroke: var(--text); stroke-width: 2; stroke-linecap: round; }
  .center { stroke: var(--muted); stroke-width: 2; stroke-linecap: round; }
  .label {
    font-size: var(--fs-xs);
    letter-spacing: var(--track);
    text-transform: uppercase;
    color: var(--muted);
  }
</style>
