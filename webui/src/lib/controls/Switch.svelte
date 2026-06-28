<script lang="ts">
  // Slide switch (the sketch shape): a rounded-rectangle track with a sliding
  // block handle. Off rests at the start in a dim grey; on slides to the far end
  // and lights up in `color` (the knob/fader accent by default). `vertical` makes
  // it slide up/down (FX tiles) instead of left/right (input/output strips).
  let {
    on = false,
    vertical = false,
    color = 'var(--accent)',
    label = '',
    onclick,
  }: {
    on?: boolean;
    vertical?: boolean;
    color?: string;
    label?: string;
    onclick?: (e: MouseEvent) => void;
  } = $props();
</script>

<button
  class="switch"
  class:on
  class:vertical
  style="--sw:{color}"
  role="switch"
  aria-checked={on}
  aria-label={label}
  title={label}
  {onclick}
>
  <span class="handle"></span>
</button>

<style>
  .switch {
    position: relative;
    flex: 0 0 auto;
    width: 40px;
    height: 22px;
    padding: 0;
    border: 1px solid var(--line);
    border-radius: 6px;
    background: var(--inset);
    cursor: pointer;
    transition: border-color var(--t-fast);
  }
  .switch.vertical { width: 22px; height: 40px; }
  .switch:hover { border-color: var(--line-2); }

  /* Handle fills the full inner height (horizontal) flush to the track, half its
     width, and slides the other half across when on. */
  .handle {
    position: absolute;
    top: 0;
    left: 0;
    width: 50%;
    height: 100%;
    border-radius: 5px;
    background: var(--line-2);
    transition: transform var(--t-fast), background var(--t-fast), box-shadow var(--t-fast);
  }
  .switch.on .handle {
    background: var(--sw);
    transform: translateX(100%);
    box-shadow: var(--glow) var(--sw);
  }

  /* Vertical: handle fills the full inner width, pinned to the bottom (off), and
     slides up when on. */
  .switch.vertical .handle {
    top: auto;
    bottom: 0;
    left: 0;
    width: 100%;
    height: 50%;
  }
  .switch.vertical.on .handle { transform: translateY(-100%); }

  .switch:focus-visible { outline: none; border-color: var(--sw); }
</style>
