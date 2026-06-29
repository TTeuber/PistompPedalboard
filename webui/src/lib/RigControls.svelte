<script lang="ts">
  // Top-bar rig controls: Save / New / Prev / Next, sharing browsing state with
  // the sidebar (rigs.svelte.ts).
  //   Save -- overwrites the loaded rig in place; if nothing's loaded it falls
  //           back to naming a new one (so it covers "save existing OR save new").
  //   New  -- always names a brand-new rig from the live board.
  // Naming uses a small themed modal rather than the browser's prompt().
  import { rigState, step, saveRig } from './rigs.svelte.js';
  import Button from './controls/Button.svelte';

  let asking = $state(false);
  let draft = $state('');
  let saved = $state(false); // brief "Saved" flash after an in-place overwrite
  let savedTimer: ReturnType<typeof setTimeout> | undefined;

  function flashSaved() {
    saved = true;
    clearTimeout(savedTimer);
    savedTimer = setTimeout(() => (saved = false), 1100);
  }

  function clickSave() {
    if (rigState.activeRig) {
      saveRig(rigState.activeRig).then(flashSaved).catch(console.error);
    } else {
      ask(); // nothing loaded -> name a new rig
    }
  }
  function clickNew() {
    ask();
  }
  function ask() {
    draft = '';
    asking = true;
  }
  function confirm() {
    const n = draft.trim();
    if (!n) return;
    saveRig(n).catch(console.error);
    asking = false;
  }
  function cancel() {
    asking = false;
  }
  function onkeydown(e: KeyboardEvent) {
    if (!asking) return;
    if (e.key === 'Enter') confirm();
    else if (e.key === 'Escape') cancel();
  }
</script>

<svelte:window {onkeydown} />

<div class="rig-controls">
  <span class="now-playing" class:none={!rigState.activeRig} title="Current rig">
    {rigState.activeRig || 'No rig loaded'}
  </span>
  <Button square title="Previous rig" disabled={!rigState.viewRigs.length} onclick={() => step(-1)}>◀</Button>
  <Button square title="Next rig" disabled={!rigState.viewRigs.length} onclick={() => step(1)}>▶</Button>
  <Button
    active={saved}
    color="var(--ok)"
    title={rigState.activeRig ? `Save “${rigState.activeRig}”` : 'Save current board as a rig'}
    onclick={clickSave}>{saved ? 'Saved ✓' : 'Save rig'}</Button
  >
  <Button title="Save the current board as a new rig" onclick={clickNew}>New rig</Button>
</div>

{#if asking}
  <div class="backdrop">
    <button class="scrim" aria-label="Cancel" onclick={cancel}></button>
    <div class="dialog" role="dialog" aria-modal="true" aria-label="Name rig">
      <h3>Name the rig</h3>
      <!-- svelte-ignore a11y_autofocus -->
      <input
        type="text"
        placeholder="rig name…"
        bind:value={draft}
        autofocus
      />
      <div class="actions">
        <button class="btn ghost" onclick={cancel}>Cancel</button>
        <button class="btn primary" onclick={confirm} disabled={!draft.trim()}>Save</button>
      </div>
    </div>
  </div>
{/if}

<style>
  .rig-controls { display: inline-flex; align-items: center; gap: var(--sp-2); }

  /* Current rig name -- the marquee of the top bar. */
  .now-playing {
    max-width: 220px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    font-weight: 600;
    color: var(--accent);
    padding: 0 var(--sp-3) 0 var(--sp-2);
    margin-right: var(--sp-1);
  }
  .now-playing.none { color: var(--faint); font-weight: 500; font-style: italic; }

  /* .btn here now styles only the name-modal's Cancel / Save (the top-bar rig
     buttons use the shared Button control). */
  .btn {
    background: var(--panel-2);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    padding: var(--sp-3) var(--sp-4);
    font: inherit;
    font-weight: 600;
    cursor: pointer;
    transition: color var(--t-fast), border-color var(--t-fast), background var(--t-fast);
  }
  .btn:hover:not(:disabled) { border-color: var(--accent); }
  .btn:disabled { opacity: .4; cursor: default; }

  /* Name modal -- matches the tuner overlay's blurred-backdrop language. */
  .backdrop {
    position: fixed;
    inset: 0;
    z-index: 120;
    display: flex;
    align-items: center;
    justify-content: center;
    background: rgba(0, 0, 0, .5);
    backdrop-filter: blur(6px);
    -webkit-backdrop-filter: blur(6px);
  }
  .scrim { position: absolute; inset: 0; background: none; border: none; padding: 0; cursor: default; }
  .dialog {
    position: relative;
    z-index: 1;
    width: min(360px, 92vw);
    background: var(--panel);
    border: 1px solid var(--line-2);
    border-radius: var(--r-xl);
    box-shadow: var(--shadow-2);
    padding: var(--sp-6);
  }
  .dialog h3 {
    margin: 0 0 var(--sp-4);
    font-size: var(--fs-sm);
    letter-spacing: var(--track-wide);
    text-transform: uppercase;
    color: var(--muted);
    font-weight: 600;
  }
  .dialog input {
    width: 100%;
    background: var(--panel-2);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    padding: var(--sp-3) var(--sp-4);
    font: inherit;
    margin-bottom: var(--sp-5);
  }
  .dialog input:focus { outline: none; border-color: var(--accent); }
  .actions { display: flex; justify-content: flex-end; gap: var(--sp-3); }
  .btn.ghost { background: none; color: var(--muted); }
  .btn.ghost:hover { color: var(--text); }
  .btn.primary { background: var(--accent); color: var(--ink); border-color: var(--accent); }
  .btn.primary:hover:not(:disabled) { box-shadow: var(--glow) var(--accent); }
</style>
