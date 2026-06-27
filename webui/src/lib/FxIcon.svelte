<script lang="ts">
  // A small custom line-art glyph per movable FX kind. Drawn with currentColor so
  // the tile can tint it (e.g. to the assigned footswitch colour); pass `color`
  // to override, otherwise it inherits the surrounding text colour.
  let { kind, color = 'currentColor', size = 26 }: {
    kind: string;
    color?: string;
    size?: number;
  } = $props();
</script>

<svg
  width={size}
  height={size}
  viewBox="0 0 24 24"
  fill="none"
  stroke={color}
  stroke-width="2"
  stroke-linecap="round"
  stroke-linejoin="round"
  aria-hidden="true"
>
  {#if kind === 'drive'}
    <!-- clipped waveform: a sine squashed flat at the rails (overdrive). -->
    <path d="M2 12h3l1-6h4l1 12h4l1-6h3" />
  {:else if kind === 'chorus'}
    <!-- two offset sine waves: the doubling/detune of modulation. -->
    <path d="M2 9q3-5 6 0t6 0 6 0" />
    <path d="M2 15q3-5 6 0t6 0 6 0" opacity="0.55" />
  {:else if kind === 'delay'}
    <!-- a tap and its decaying echoes. -->
    <path d="M4 5v14" />
    <path d="M10 8v8" opacity="0.7" />
    <path d="M15 10v4" opacity="0.45" />
    <path d="M20 11.5v1" opacity="0.25" />
  {:else if kind === 'reverb'}
    <!-- sound spreading outward in a room: nested arcs from a source. -->
    <path d="M4 20a14 14 0 0 1 14-14" />
    <path d="M4 20a9 9 0 0 1 9-9" opacity="0.6" />
    <path d="M4 20a4 4 0 0 1 4-4" opacity="0.35" />
    <circle cx="4" cy="20" r="1.4" fill={color} stroke="none" />
  {:else}
    <!-- fallback: a simple pedal dot. -->
    <circle cx="12" cy="12" r="6" />
  {/if}
</svg>
