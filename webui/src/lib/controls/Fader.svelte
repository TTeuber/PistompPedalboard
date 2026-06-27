<script lang="ts">
  // Rectangle that fills from one end to the other (the sketch). SVG, no assets,
  // recolours from one CSS var. Click anywhere to jump; drag to scrub.
  //
  // Bipolar: the fill grows out from the track centre toward the value instead of
  // from one end — for params that swing either side of a neutral.
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

  let dragging = $state(false);
  function fromPointer(e: PointerEvent) {
    const r = (e.currentTarget as Element).getBoundingClientRect();
    const f = vertical ? 1 - (e.clientY - r.top) / r.height : (e.clientX - r.left) / r.width;
    set(min + Math.min(1, Math.max(0, f)) * span);
  }
  function down(e: PointerEvent) {
    dragging = true;
    (e.currentTarget as Element).setPointerCapture(e.pointerId);
    fromPointer(e);
  }
  function move(e: PointerEvent) {
    if (dragging) fromPointer(e);
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

<div class="fader" class:vertical>
  {#if label}<span class="label">{label}</span>{/if}
  <svg
    width={w}
    height={h}
    viewBox="0 0 {w} {h}"
    class="track"
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
  .track { cursor: pointer; touch-action: none; display: block; }
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
