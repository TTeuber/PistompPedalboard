<script lang="ts">
  import { board, setTempo, tapTempo } from './store.svelte.js';

  // Local mirror of the BPM field so typing feels immediate; we commit on Enter
  // or blur (not per keystroke) and adopt the board value when it changes under
  // us -- unless the field is focused, so an SSE echo never fights your typing.
  let text = $state('120');
  let editing = $state(false);

  $effect.pre(() => {
    const incoming = Math.round(board.bpm);
    if (!editing) text = String(incoming);
  });

  function commit() {
    editing = false;
    const v = Math.round(Number(text));
    if (Number.isFinite(v) && v > 0) {
      const clamped = Math.min(300, Math.max(20, v));
      setTempo(clamped);
      text = String(clamped);
    } else {
      text = String(Math.round(board.bpm)); // reject garbage, snap back
    }
  }

  function onKey(e: KeyboardEvent) {
    if (e.key === 'Enter') (e.currentTarget as HTMLInputElement).blur();
  }
</script>

<div class="tempo">
  <span class="tempo-label">Tempo</span>
  <input
    class="bpm"
    type="number"
    min="20"
    max="300"
    bind:value={text}
    onfocus={() => (editing = true)}
    onblur={commit}
    onkeydown={onKey}
  />
  <span class="unit">BPM</span>
  <button class="tap" onclick={tapTempo} title="Tap to set tempo">Tap</button>
</div>

<style>
  .tempo {
    display: flex;
    align-items: center;
    gap: var(--sp-2);
    color: var(--muted);
    font-size: var(--fs-sm);
  }
  .tempo-label {
    letter-spacing: var(--track);
    text-transform: uppercase;
    font-size: var(--fs-xs);
  }
  .bpm {
    width: 56px;
    background: var(--panel-2);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    padding: 5px 6px;
    font-variant-numeric: tabular-nums;
    text-align: right;
  }
  .unit { font-size: var(--fs-xs); }
  .tap {
    padding: 6px 12px;
    border-radius: var(--r-sm);
    cursor: pointer;
    background: var(--panel-2);
    color: var(--text);
    border: 1px solid var(--line);
    font-weight: 600;
    letter-spacing: var(--track);
    text-transform: uppercase;
    font-size: var(--fs-xs);
  }
  .tap:active { background: var(--accent); color: var(--ink); border-color: var(--accent); }
</style>
