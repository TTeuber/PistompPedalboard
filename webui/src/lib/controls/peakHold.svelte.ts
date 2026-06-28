// Peak-hold for the level meters: snaps up to any new high instantly, then eases
// back down at a fixed dB/sec so the bright peak tick lingers where the audio is
// cresting before drifting back. Driven by its own slow timer (independent of the
// 15 Hz meter poll) so it keeps falling even when the level holds steady.
import { onMount } from 'svelte';

export function usePeakHold(read: () => number, floor = -60, decayDbPerSec = 14) {
  const peak = $state({ value: floor });
  onMount(() => {
    const ms = 50;
    const step = decayDbPerSec * (ms / 1000); // dB shed per tick on the way down
    const id = setInterval(() => {
      const v = read();
      peak.value = v >= peak.value ? v : Math.max(v, peak.value - step);
    }, ms);
    return () => clearInterval(id);
  });
  return peak;
}
