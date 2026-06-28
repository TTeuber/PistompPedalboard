<script lang="ts">
  // Add-effect picker: choose a KIND and (optionally) one of its saved presets in
  // one place, then drop it into the target FX slot. The server places + voices
  // the new pedal atomically (POST /api/fx/add with an optional `preset`).
  import { api } from './api.js';
  import { board, applyState, loadPedalPresetNamesByKind } from './store.svelte.js';
  import type { BoardState } from './types.js';
  import FxIcon from './FxIcon.svelte';

  let { slot, onclose }: { slot: number; onclose: () => void } = $props();

  // Selected kind = index into board.fxKinds (the server's add() key); -1 = none.
  let kindIdx = $state(-1);
  let presetNames = $state<string[]>([]);
  let presetSel = $state('');
  let busy = $state(false);

  const selectedKind = $derived(kindIdx >= 0 ? board.fxKinds[kindIdx] : null);

  // When the kind changes, load that kind's presets and reset the choice.
  async function pickKind(i: number) {
    kindIdx = i;
    presetSel = '';
    presetNames = [];
    const k = board.fxKinds[i];
    if (k) presetNames = await loadPedalPresetNamesByKind(k.type);
  }

  async function add() {
    if (kindIdx < 0 || busy) return;
    busy = true;
    try {
      applyState(
        await api<BoardState>('/api/fx/add', {
          slot,
          kind: kindIdx,
          preset: presetSel || undefined,
        }),
      );
      onclose();
    } catch (e) {
      console.error(e);
      busy = false;
    }
  }

  function onkeydown(e: KeyboardEvent) {
    if (e.key === 'Escape') onclose();
    else if (e.key === 'Enter' && kindIdx >= 0) add();
  }
</script>

<svelte:window {onkeydown} />

<div class="backdrop">
  <button class="scrim" aria-label="Cancel" onclick={onclose}></button>
  <div class="modal" role="dialog" aria-modal="true" aria-label="Add effect">
    <button class="close" title="Close (Esc)" onclick={onclose}>✕</button>

    <h2 class="title">Add Effect</h2>

    <div class="kinds">
      {#each board.fxKinds as k, i}
        <button
          class="kind"
          class:selected={i === kindIdx}
          onclick={() => pickKind(i)}
        >
          <span class="kicon"><FxIcon kind={k.type} /></span>
          <span class="kname">{k.name}</span>
        </button>
      {/each}
    </div>

    <label class="preset-row">
      <span class="lbl">Preset</span>
      <select bind:value={presetSel} disabled={!selectedKind}>
        <option value="">Default</option>
        {#each presetNames as n}
          <option value={n}>{n}</option>
        {/each}
      </select>
    </label>

    <div class="actions">
      <button class="mini" onclick={onclose}>Cancel</button>
      <button class="mini add" disabled={kindIdx < 0 || busy} onclick={add}>
        {#if selectedKind}Add {selectedKind.name}{:else}Add{/if}
      </button>
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
  .scrim { position: absolute; inset: 0; background: none; border: none; padding: 0; cursor: default; }
  .modal {
    position: relative;
    z-index: 1;
    width: min(460px, 92vw);
    background: var(--panel);
    border: 1px solid var(--line-2);
    border-radius: var(--r-xl);
    box-shadow: var(--shadow-2);
    padding: var(--sp-6);
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

  .title {
    font-size: var(--fs-sm);
    letter-spacing: var(--track-wide);
    text-transform: uppercase;
    color: var(--accent);
    margin: 0 0 var(--sp-5);
  }

  /* Effect kinds as a tappable grid of icon tiles. */
  .kinds {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(96px, 1fr));
    gap: var(--sp-3);
    margin-bottom: var(--sp-5);
  }
  .kind {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: var(--sp-2);
    padding: var(--sp-4) var(--sp-2);
    border: 1px solid var(--line);
    border-radius: var(--r-md);
    background: var(--inset);
    color: var(--text);
    cursor: pointer;
    transition: border-color var(--t-fast), background var(--t-fast);
  }
  .kind:hover { border-color: var(--accent); }
  .kind.selected { border-color: var(--accent); background: var(--panel-2); }
  .kicon { line-height: 0; color: var(--muted); }
  .kind.selected .kicon { color: var(--accent); }
  .kname { font-size: var(--fs-sm); font-weight: 600; letter-spacing: var(--track); }

  .preset-row { display: flex; align-items: center; gap: var(--sp-3); margin-bottom: var(--sp-5); }
  .preset-row .lbl {
    font-size: var(--fs-xs);
    letter-spacing: var(--track);
    text-transform: uppercase;
    color: var(--muted);
  }
  .preset-row select {
    flex: 1;
    min-width: 0;
    background: var(--inset);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    padding: var(--sp-2) var(--sp-3);
    font: inherit;
    font-size: var(--fs-sm);
  }
  .preset-row select:disabled { opacity: .4; }

  .actions { display: flex; gap: var(--sp-3); }
  .actions .mini { flex: 1; padding: var(--sp-3) 0; }
  .mini.add { background: var(--accent); color: var(--ink); border-color: var(--accent); }
  .mini.add:hover:not(:disabled) { filter: brightness(1.08); }
</style>
