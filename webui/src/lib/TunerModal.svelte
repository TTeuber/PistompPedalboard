<script lang="ts">
  // Full-screen tuner overlay with a blurred backdrop. While mounted it engages
  // the hidden tuner effect (mutes the output for silent tuning) and polls
  // /api/tuner fast for a lively needle; unmounting disengages it again.
  import { onMount } from 'svelte';
  import { api } from './api.js';
  import type { TunerReading } from './types.js';

  let { onclose }: { onclose: () => void } = $props();

  const NOTES = ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'];

  let r = $state<TunerReading>({ engaged: false, note: -1, octave: 0, cents: 0, freq: 0 });

  const hasPitch = $derived(r.note >= 0 && r.freq > 0);
  const noteName = $derived(hasPitch ? NOTES[r.note] : '—');
  const inTune = $derived(hasPitch && Math.abs(r.cents) <= 4);
  const cents = $derived(Math.max(-50, Math.min(50, r.cents))); // clamp needle to dial
  const status = $derived(
    !hasPitch ? 'play a note' : inTune ? 'in tune' : r.cents < 0 ? 'flat' : 'sharp',
  );

  onMount(() => {
    api('/api/enabled', { effect: 'tuner', enabled: true }).catch(console.error);
    const tick = async () => {
      try {
        r = await api<TunerReading>('/api/tuner');
      } catch {
        /* ignore a dropped poll */
      }
    };
    const id = setInterval(tick, 80);
    tick();
    return () => {
      clearInterval(id);
      api('/api/enabled', { effect: 'tuner', enabled: false }).catch(console.error);
    };
  });

  function onkeydown(e: KeyboardEvent) {
    if (e.key === 'Escape') onclose();
  }
</script>

<svelte:window {onkeydown} />

<div class="backdrop">
  <button class="scrim" aria-label="Close tuner" onclick={onclose}></button>
  <div class="modal" role="dialog" aria-modal="true" aria-label="Tuner">
    <button class="close" title="Close (Esc)" onclick={onclose}>✕</button>

    <div class="note" class:dim={!hasPitch} class:tuned={inTune}>
      {noteName}{#if hasPitch}<sub>{r.octave}</sub>{/if}
    </div>

    <div class="dial">
      <div class="band"></div>
      <div class="ticks">
        {#each [-50, -40, -30, -20, -10, 0, 10, 20, 30, 40, 50] as c}
          <span class="tick" class:major={c === 0} style="left:{((c + 50) / 100) * 100}%"></span>
        {/each}
      </div>
      <div class="needle" class:tuned={inTune} style="left:{((cents + 50) / 100) * 100}%"></div>
    </div>

    <div class="readout">
      <span class="status" class:tuned={inTune}>{status}</span>
      <span class="meta">{hasPitch ? `${r.cents > 0 ? '+' : ''}${r.cents.toFixed(1)}¢` : ''}</span>
      <span class="meta">{hasPitch ? `${r.freq.toFixed(1)} Hz` : ''}</span>
    </div>
  </div>
</div>

<style>
  .backdrop {
    position: fixed;
    inset: 0;
    z-index: 100;
    display: flex;
    align-items: center;
    justify-content: center;
    background: rgba(0, 0, 0, .5);
    backdrop-filter: blur(8px);
    -webkit-backdrop-filter: blur(8px);
  }
  /* Full-screen close target behind the modal (clicking outside closes). */
  .scrim { position: absolute; inset: 0; background: none; border: none; padding: 0; cursor: default; }
  .modal {
    position: relative;
    z-index: 1;
    width: min(420px, 92vw);
    background: var(--panel);
    border: 1px solid var(--line-2);
    border-radius: var(--r-xl);
    box-shadow: var(--shadow-2);
    padding: var(--sp-7) var(--sp-6) var(--sp-6);
    text-align: center;
  }
  .close {
    position: absolute;
    top: var(--sp-4);
    right: var(--sp-4);
    width: 30px;
    height: 30px;
    border-radius: 50%;
    border: 1px solid var(--line);
    background: var(--panel-2);
    color: var(--muted);
    cursor: pointer;
    font-size: var(--fs-sm);
  }
  .close:hover { color: var(--text); border-color: var(--accent); }

  .note {
    font-size: 96px;
    line-height: 1;
    font-weight: 600;
    color: var(--accent);
    margin: var(--sp-3) 0 var(--sp-6);
  }
  .note sub { font-size: 28px; color: var(--muted); font-weight: 400; }
  .note.dim { color: var(--faint); }
  .note.tuned { color: var(--ok); }

  .dial {
    position: relative;
    height: 54px;
    margin: 0 var(--sp-2) var(--sp-5);
    border-bottom: 1px solid var(--line);
  }
  .band {
    position: absolute;
    left: 46%;
    width: 8%;
    top: 0;
    bottom: 0;
    background: color-mix(in srgb, var(--ok) 16%, transparent);
    border-left: 1px solid color-mix(in srgb, var(--ok) 40%, transparent);
    border-right: 1px solid color-mix(in srgb, var(--ok) 40%, transparent);
  }
  .ticks { position: absolute; inset: 0; }
  .tick {
    position: absolute;
    bottom: 0;
    width: 1px;
    height: 12px;
    background: var(--line-2);
    transform: translateX(-50%);
  }
  .tick.major { height: 22px; background: var(--muted); }
  .needle {
    position: absolute;
    top: 0;
    bottom: 0;
    width: 3px;
    border-radius: var(--r-pill);
    background: var(--accent);
    box-shadow: var(--glow) var(--accent);
    transform: translateX(-50%);
    transition: left .07s linear, background .12s;
  }
  .needle.tuned { background: var(--ok); box-shadow: var(--glow) var(--ok); }

  .readout {
    display: flex;
    align-items: baseline;
    justify-content: center;
    gap: var(--sp-5);
    font-variant-numeric: tabular-nums;
  }
  .status {
    font-size: var(--fs-md);
    letter-spacing: var(--track-wide);
    text-transform: uppercase;
    color: var(--muted);
  }
  .status.tuned { color: var(--ok); }
  .meta { font-size: var(--fs-sm); color: var(--muted); min-width: 64px; }
</style>
