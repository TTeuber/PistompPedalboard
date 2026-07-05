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
  {#if kind === 'overdrive'}
    <!-- clipped waveform: a sine softly squashed flat at the rails. -->
    <path d="M2 12h3l1-6h4l1 12h4l1-6h3" />
  {:else if kind === 'distortion'}
    <!-- harder clip: steeper, more squared-off than the overdrive. -->
    <path d="M2 18h2l1-12h5l1 12h5l1-12h2" />
  {:else if kind === 'fuzz'}
    <!-- near-square wave: the most extreme, blocky clipping. -->
    <path d="M2 16v-8h4v8h4v-8h4v8h4v-8h2" />
  {:else if kind === 'chorus'}
    <!-- two offset sine waves: the doubling/detune of modulation. -->
    <path d="M2 9q3-5 6 0t6 0 6 0" />
    <path d="M2 15q3-5 6 0t6 0 6 0" opacity="0.55" />
  {:else if kind === 'vibrato'}
    <!-- one continuous sine: the pure pitch wobble. -->
    <path d="M2 12q2-6 4 0t4 0 4 0 4 0 4 0" />
  {:else if kind === 'tremolo'}
    <!-- amplitude pulse: bars rising and falling like a volume LFO. -->
    <path d="M3 11v2" />
    <path d="M8 8v8" />
    <path d="M13 4v16" />
    <path d="M18 8v8" />
    <path d="M22 11v2" opacity="0.7" />
  {:else if kind === 'phaser'}
    <!-- a sweep passing through a moving notch. -->
    <path d="M2 11q5 7 10 0t10 0" />
    <path d="M12 4v16" opacity="0.45" stroke-dasharray="2 2" />
  {:else if kind === 'flanger'}
    <!-- comb filter: evenly spaced teeth of a swept comb. -->
    <path d="M3 6v12" />
    <path d="M8 6v12" opacity="0.8" />
    <path d="M13 6v12" opacity="0.6" />
    <path d="M18 6v12" opacity="0.4" />
  {:else if kind === 'dimension'}
    <!-- widening: centre bars with chevrons opening outward left and right. -->
    <path d="M10.5 8v8" />
    <path d="M13.5 8v8" />
    <path d="M7 8l-4 4 4 4" opacity="0.6" />
    <path d="M17 8l4 4-4 4" opacity="0.6" />
  {:else if kind === 'detune'}
    <!-- twin near-identical sines, barely offset: the few-cents double. -->
    <path d="M2 11q3-5 6 0t6 0 6 0" />
    <path d="M3.2 13q3-5 6 0t6 0 5 0" opacity="0.45" />
  {:else if kind === 'vibe'}
    <!-- the lamp: a bulb with radiating throb ticks over a wave. -->
    <circle cx="12" cy="8" r="3" />
    <path d="M12 2.5v1.5" opacity="0.6" />
    <path d="M6.8 4.5l1.1 1.1" opacity="0.6" />
    <path d="M17.2 4.5l-1.1 1.1" opacity="0.6" />
    <path d="M3 17q3-4.5 6 0t6 0 6 0" />
  {:else if kind === 'rotary'}
    <!-- rotor swirl: a circle with a rotation arrow, horn dot on the rim. -->
    <path d="M19 12a7 7 0 1 1-2-4.9" />
    <path d="M19.5 3.5v4h-4" />
    <circle cx="12" cy="19" r="1.3" fill={color} stroke="none" />
  {:else if kind === 'octave'}
    <!-- two tones an octave apart: a low and a high cycle stacked. -->
    <path d="M2 8q4-5 8 0t8 0" />
    <path d="M2 17q2-3 4 0t4 0 4 0 4 0" opacity="0.6" />
  {:else if kind === 'swell'}
    <!-- a volume ramp swelling in from silence. -->
    <path d="M3 19h18" opacity="0.4" />
    <path d="M3 19c7 0 11-13 18-13" />
  {:else if kind === 'delay'}
    <!-- a tap and its decaying echoes. -->
    <path d="M4 5v14" />
    <path d="M10 8v8" opacity="0.7" />
    <path d="M15 10v4" opacity="0.45" />
    <path d="M20 11.5v1" opacity="0.25" />
  {:else if kind === 'tape'}
    <!-- tape echo: two reels with the tape path running between them. -->
    <circle cx="7" cy="10" r="3.5" />
    <circle cx="17" cy="10" r="3.5" />
    <path d="M7 13.5v0" />
    <path d="M4 18h16" opacity="0.6" />
    <path d="M7 13.5l1 4.5" opacity="0.6" />
    <path d="M17 13.5l-1 4.5" opacity="0.6" />
  {:else if kind === 'analog'}
    <!-- BBD: decaying echoes riding a slow modulation wave. -->
    <path d="M4 4v11" />
    <path d="M10 7v8" opacity="0.7" />
    <path d="M15 9v6" opacity="0.45" />
    <path d="M20 11v4" opacity="0.25" />
    <path d="M3 19q3-3 6 0t6 0 6 0" opacity="0.6" />
  {:else if kind === 'multitap'}
    <!-- taps at uneven spacing: the rhythm pattern. -->
    <path d="M4 5v14" />
    <path d="M11 8v8" opacity="0.75" />
    <path d="M14 9.5v5" opacity="0.55" />
    <path d="M20 8v8" opacity="0.65" />
  {:else if kind === 'reverse'}
    <!-- backwards echoes: the taps GROW left to right into the hit. -->
    <path d="M4 11.5v1" opacity="0.25" />
    <path d="M9 10v4" opacity="0.45" />
    <path d="M14 8v8" opacity="0.7" />
    <path d="M20 5v14" />
  {:else if kind === 'ambient'}
    <!-- repeats dissolving into a cloud: echoes fading into dots. -->
    <path d="M4 5v14" />
    <path d="M10 8v8" opacity="0.6" />
    <circle cx="15" cy="10" r="0.8" fill={color} stroke="none" opacity="0.5" />
    <circle cx="17.5" cy="14" r="0.8" fill={color} stroke="none" opacity="0.4" />
    <circle cx="20" cy="9" r="0.8" fill={color} stroke="none" opacity="0.3" />
    <circle cx="21" cy="13" r="0.8" fill={color} stroke="none" opacity="0.2" />
  {:else if kind === 'reverb' || kind === 'hall'}
    <!-- sound spreading outward in a room: nested arcs from a source. -->
    <path d="M4 20a14 14 0 0 1 14-14" />
    <path d="M4 20a9 9 0 0 1 9-9" opacity="0.6" />
    <path d="M4 20a4 4 0 0 1 4-4" opacity="0.35" />
    <circle cx="4" cy="20" r="1.4" fill={color} stroke="none" />
  {:else if kind === 'plate'}
    <!-- a vibrating metal plate: a tilted sheet with ripple lines. -->
    <path d="M3 15l8-8h10l-8 8z" />
    <path d="M7 13l6-6" opacity="0.5" />
    <path d="M11 15l6-6" opacity="0.35" />
  {:else if kind === 'shimmer'}
    <!-- rising arcs with sparkles: the octave-up wash climbing. -->
    <path d="M4 20a14 14 0 0 1 14-14" />
    <path d="M4 20a9 9 0 0 1 9-9" opacity="0.55" />
    <path d="M17 4l0.7 2 2 0.7-2 0.7-0.7 2-0.7-2-2-0.7 2-0.7z" fill={color} stroke="none" />
    <path d="M10 3l0.5 1.4 1.4 0.5-1.4 0.5-0.5 1.4-0.5-1.4-1.4-0.5 1.4-0.5z" fill={color} stroke="none" opacity="0.6" />
  {:else}
    <!-- fallback: a simple pedal dot. -->
    <circle cx="12" cy="12" r="6" />
  {/if}
</svg>
