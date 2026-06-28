<script lang="ts">
  // Rectangle that fills from one end to the other (the sketch). SVG, no assets,
  // recolours from one CSS var. Drag to change BY the drag amount (like the knob,
  // not jump-to-cursor): dragging the full track length covers the full range,
  // up / right increases, Shift = fine. Arrow keys nudge.
  //
  // Bipolar: the fill grows out from the track centre toward the value instead of
  // from one end — for params that swing either side of a neutral.
  //
  // `full`: stretch a horizontal fader to fill its container's width (used in the
  // FX params panel where the old sliders were full-width).
  let {
    value = $bindable(0),
    min = 0,
    max = 1,
    label = '',
    vertical = false,
    length = 200,
    thickness = 26,
    color = 'var(--accent)',
    bipolar = false,
    full = false,
    oninput = undefined,
  }: {
    value?: number;
    min?: number;
    max?: number;
    label?: string;
    vertical?: boolean;
    length?: number;
    thickness?: number;
    color?: string;
    bipolar?: boolean;
    full?: boolean;
    oninput?: (v: number) => void;
  } = $props();

  const PAD = 3;
  const span = $derived(max - min || 1);
  const t = $derived(Math.min(1, Math.max(0, (value - min) / span)));

  const w = $derived(vertical ? thickness : length);
  const h = $derived(vertical ? length : thickness);
  // Inner fill geometry. Unipolar grows from the left (h) or bottom (v); bipolar
  // grows out from the centre toward the value in either direction.
  const fill = $derived.by(() => {
    const innerW = w - PAD * 2;
    const innerH = h - PAD * 2;
    if (vertical) {
      if (bipolar) {
        const midY = PAD + innerH / 2;
        const valY = PAD + innerH * (1 - t);
        return { x: PAD, y: Math.min(midY, valY), width: innerW, height: Math.abs(valY - midY) };
      }
      const fh = innerH * t;
      return { x: PAD, y: PAD + (innerH - fh), width: innerW, height: fh };
    }
    if (bipolar) {
      const midX = PAD + innerW / 2;
      const valX = PAD + innerW * t;
      return { x: Math.min(midX, valX), y: PAD, width: Math.abs(valX - midX), height: innerH };
    }
    return { x: PAD, y: PAD, width: innerW * t, height: innerH };
  });

  // Centre reference line for bipolar, marking the neutral position.
  const center = $derived.by(() => {
    if (!bipolar) return null;
    if (vertical) return { x1: PAD, y1: h / 2, x2: w - PAD, y2: h / 2 };
    return { x1: w / 2, y1: PAD, x2: w / 2, y2: h - PAD };
  });

  function set(v: number) {
    value = Math.min(max, Math.max(min, v));
    oninput?.(value);
  }

  // Relative dragging: track the pointer delta and move the value by that
  // fraction of the track length — same feel as the knob, not jump-to-cursor.
  let dragging = $state(false);
  let lastPos = 0;
  function down(e: PointerEvent) {
    dragging = true;
    lastPos = vertical ? e.clientY : e.clientX;
    (e.currentTarget as Element).setPointerCapture(e.pointerId);
  }
  function move(e: PointerEvent) {
    if (!dragging) return;
    const r = (e.currentTarget as Element).getBoundingClientRect();
    const len = (vertical ? r.height : r.width) || 1;
    const pos = vertical ? e.clientY : e.clientX;
    const d = pos - lastPos;
    lastPos = pos;
    // Up (vertical) / right (horizontal) increases; full length ≈ full range.
    set(value + ((vertical ? -d : d) / len) * span * (e.shiftKey ? 0.25 : 1));
  }
  function up() {
    dragging = false;
  }
  function keydown(e: KeyboardEvent) {
    const inc = (span / 100) * (e.shiftKey ? 1 : 5);
    if (e.key === 'ArrowUp' || e.key === 'ArrowRight') set(value + inc);
    else if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') set(value - inc);
    else return;
    e.preventDefault();
  }
</script>

<div class="fader" class:vertical class:full={full && !vertical}>
  {#if label}<span class="label">{label}</span>{/if}
  <svg
    width={full && !vertical ? '100%' : w}
    height={h}
    viewBox="0 0 {w} {h}"
    preserveAspectRatio={full && !vertical ? 'none' : 'xMidYMid meet'}
    class="track"
    class:vertical
    style="--fill:{color}"
    role="slider"
    tabindex="0"
    aria-label={label || 'fader'}
    aria-valuemin={min}
    aria-valuemax={max}
    aria-valuenow={value}
    aria-orientation={vertical ? 'vertical' : 'horizontal'}
    onpointerdown={down}
    onpointermove={move}
    onpointerup={up}
    onpointercancel={up}
    onkeydown={keydown}
  >
    <rect class="frame" x={PAD} y={PAD} width={w - PAD * 2} height={h - PAD * 2} rx="2" />
    <rect class="bar" x={fill.x} y={fill.y} width={fill.width} height={fill.height} rx="1" />
    {#if center}
      <line class="center" x1={center.x1} y1={center.y1} x2={center.x2} y2={center.y2} />
    {/if}
  </svg>
</div>

<style>
  .fader { display: inline-flex; flex-direction: column; gap: var(--sp-2); align-items: flex-start; }
  .fader.vertical { align-items: center; }
  .fader.full { display: flex; width: 100%; }
  .track { cursor: ew-resize; touch-action: none; display: block; }
  .track.vertical { cursor: ns-resize; }
  .fader.full .track { width: 100%; }
  .frame { fill: var(--inset); stroke: var(--line); stroke-width: 2; }
  .bar { fill: var(--fill); }
  .center { stroke: var(--muted); stroke-width: 1.5; stroke-linecap: round; }
  .label {
    font-size: var(--fs-xs);
    letter-spacing: var(--track);
    text-transform: uppercase;
    color: var(--muted);
  }
</style>
