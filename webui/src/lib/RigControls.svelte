<script lang="ts">
  // Top-bar rig controls, mirroring the setlist editor's header language: the
  // loaded rig's name (with a "*" when the board has unsaved edits), an inline
  // rename pencil, the Prev/Next stepper, then icon actions -- New, Save, Delete.
  //   Save   -- overwrites the loaded rig in place; if nothing's loaded it falls
  //             back to naming a new one (covers "save existing OR save new").
  //   New    -- always names a brand-new rig from the live board.
  //   Rename -- inline-renames the loaded rig (id-preserving, server-side).
  //   Delete -- removes the loaded rig after a confirm.
  import { rigState, step, saveRig, renameRig, deleteRig, rigDirty } from './rigs.svelte.js';
  import Button from './controls/Button.svelte';

  let asking = $state(false); // name-a-new-rig modal
  let draft = $state('');
  let saved = $state(false); // brief "saved" flash on the Save icon
  let savedTimer: ReturnType<typeof setTimeout> | undefined;

  let renaming = $state(false); // inline rename of the loaded rig
  let renameDraft = $state('');
  let confirmingDelete = $state(false);
  let saveChoice = $state(false); // overwrite vs save-as-new fork
  let pendingStep = $state<null | (() => void)>(null); // step parked behind the dirty prompt

  function flashSaved() {
    saved = true;
    clearTimeout(savedTimer);
    savedTimer = setTimeout(() => (saved = false), 1100);
  }

  // ---- save / new --------------------------------------------------------
  // Save forks: overwrite the loaded rig, or save the board as a new one. With
  // nothing loaded there's nothing to overwrite, so go straight to naming a new.
  function clickSave() {
    if (rigState.activeRig) saveChoice = true;
    else ask();
  }
  function overwriteSave() {
    saveChoice = false;
    saveRig(rigState.activeRig).then(flashSaved).catch(console.error);
  }
  function saveAsNew() {
    saveChoice = false;
    ask();
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

  // ---- rename ------------------------------------------------------------
  function startRename() {
    if (!rigState.activeRig) return;
    renameDraft = rigState.activeRig;
    renaming = true;
  }
  function commitRename() {
    const t = renameDraft.trim();
    if (t && t !== rigState.activeRig) renameRig(rigState.activeRig, t).catch(console.error);
    renaming = false;
  }
  function cancelRename() {
    renaming = false;
  }
  function autofocus(el: HTMLInputElement) {
    el.focus();
    el.select();
  }

  // ---- prev / next (guarded when the loaded rig has unsaved edits) -------
  function tryStep(dir: number) {
    if (rigDirty()) pendingStep = () => step(dir);
    else step(dir);
  }
  async function stepSave() {
    const next = pendingStep;
    pendingStep = null;
    await saveRig(rigState.activeRig);
    next?.();
  }
  function stepDiscard() {
    const next = pendingStep;
    pendingStep = null;
    next?.();
  }

  // ---- delete ------------------------------------------------------------
  function requestDelete() {
    if (rigState.activeRig) confirmingDelete = true;
  }
  function doDelete() {
    confirmingDelete = false;
    deleteRig(rigState.activeRig).catch(console.error);
  }

  function onkeydown(e: KeyboardEvent) {
    if (!asking) return;
    if (e.key === 'Enter') confirm();
    else if (e.key === 'Escape') cancel();
  }
</script>

<svelte:window {onkeydown} />

<div class="rig-controls">
  {#if renaming}
    <input
      class="rename-input"
      type="text"
      placeholder="rig name…"
      bind:value={renameDraft}
      use:autofocus
      onkeydown={(e) => {
        if (e.key === 'Enter') commitRename();
        else if (e.key === 'Escape') cancelRename();
      }}
      onblur={commitRename}
    />
  {:else}
    <span class="now-playing" class:none={!rigState.activeRig} title="Current rig">
      {rigState.activeRig || 'No rig loaded'}{#if rigDirty()}<span class="star" title="Unsaved changes"
          >*</span
        >{/if}
    </span>
    <Button square title="Rename rig" disabled={!rigState.activeRig} onclick={startRename}>
      <svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <path d="M12 20h9" /><path d="M16.5 3.5a2.12 2.12 0 0 1 3 3L7 19l-4 1 1-4z" />
      </svg>
    </Button>
  {/if}

  <span class="sep"></span>

  <Button square title="Previous rig" disabled={!rigState.viewRigs.length} onclick={() => tryStep(-1)}>◀</Button>
  <Button square title="Next rig" disabled={!rigState.viewRigs.length} onclick={() => tryStep(1)}>▶</Button>

  <span class="sep"></span>

  <Button square title="Save the current board as a new rig" onclick={clickNew}>
    <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round">
      <path d="M12 5v14M5 12h14" />
    </svg>
  </Button>
  <Button
    square
    active={saved}
    color="var(--ok)"
    title={rigState.activeRig ? `Save “${rigState.activeRig}”` : 'Save current board as a rig'}
    onclick={clickSave}
  >
    {#if saved}
      <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round">
        <path d="M20 6L9 17l-5-5" />
      </svg>
    {:else}
      <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z" />
        <path d="M17 21v-8H7v8M7 3v5h8" />
      </svg>
    {/if}
  </Button>
  <Button square color="var(--danger)" title="Delete rig" disabled={!rigState.activeRig} onclick={requestDelete}>
    <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
      <path d="M3 6h18M8 6V4a1 1 0 0 1 1-1h6a1 1 0 0 1 1 1v2m2 0v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6" />
      <path d="M10 11v6M14 11v6" />
    </svg>
  </Button>
</div>

{#if asking}
  <div class="backdrop">
    <button class="scrim" aria-label="Cancel" onclick={cancel}></button>
    <div class="dialog" role="dialog" aria-modal="true" aria-label="Name rig">
      <h3>Name the rig</h3>
      <!-- svelte-ignore a11y_autofocus -->
      <input type="text" placeholder="rig name…" bind:value={draft} autofocus />
      <div class="actions">
        <button class="btn ghost" onclick={cancel}>Cancel</button>
        <button class="btn primary" onclick={confirm} disabled={!draft.trim()}>Save</button>
      </div>
    </div>
  </div>
{/if}

{#if saveChoice}
  <div class="backdrop">
    <button class="scrim" aria-label="Cancel" onclick={() => (saveChoice = false)}></button>
    <div class="dialog" role="dialog" aria-modal="true" aria-label="Save rig">
      <h3>Save rig</h3>
      <p class="msg">Overwrite “{rigState.activeRig}”, or save the current board as a new rig?</p>
      <div class="actions">
        <button class="btn ghost" onclick={() => (saveChoice = false)}>Cancel</button>
        <button class="btn" onclick={saveAsNew}>Save as new…</button>
        <button class="btn primary" onclick={overwriteSave}>Save “{rigState.activeRig}”</button>
      </div>
    </div>
  </div>
{/if}

{#if pendingStep}
  <div class="backdrop">
    <button class="scrim" aria-label="Cancel" onclick={() => (pendingStep = null)}></button>
    <div class="dialog" role="dialog" aria-modal="true" aria-label="Unsaved changes">
      <h3>Unsaved changes</h3>
      <p class="msg">
        “{rigState.activeRig}” has unsaved changes. Save them before switching rigs?
      </p>
      <div class="actions">
        <button class="btn ghost" onclick={() => (pendingStep = null)}>Cancel</button>
        <button class="btn danger" onclick={stepDiscard}>Discard</button>
        <button class="btn primary" onclick={stepSave}>Save</button>
      </div>
    </div>
  </div>
{/if}

{#if confirmingDelete}
  <div class="backdrop">
    <button class="scrim" aria-label="Cancel" onclick={() => (confirmingDelete = false)}></button>
    <div class="dialog" role="dialog" aria-modal="true" aria-label="Delete rig">
      <h3>Delete rig</h3>
      <p class="msg">
        Delete “{rigState.activeRig}”? This can't be undone. (Setlists referencing it will show it
        as missing.)
      </p>
      <div class="actions">
        <button class="btn ghost" onclick={() => (confirmingDelete = false)}>Cancel</button>
        <button class="btn danger" onclick={doDelete}>Delete</button>
      </div>
    </div>
  </div>
{/if}

<style>
  .rig-controls { display: inline-flex; align-items: center; gap: var(--sp-2); }
  .sep { width: 1px; align-self: stretch; margin: var(--sp-1) var(--sp-1); background: var(--line); }

  /* Current rig name -- the marquee of the top bar. */
  .now-playing {
    max-width: 220px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    font-weight: 600;
    color: var(--accent);
    padding: 0 var(--sp-2);
    margin-right: var(--sp-1);
  }
  .now-playing.none { color: var(--faint); font-weight: 500; font-style: italic; }
  .star { color: var(--accent); margin-left: 1px; }

  .rename-input {
    width: 200px;
    background: var(--panel-2);
    color: var(--text);
    border: 1px solid var(--accent);
    border-radius: var(--r-sm);
    padding: var(--sp-2) var(--sp-3);
    font: inherit;
    font-weight: 600;
    margin-right: var(--sp-1);
  }
  .rename-input:focus { outline: none; }

  /* .btn here styles the modal's Cancel / Save / Delete (the top-bar rig
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

  /* Modal -- matches the tuner overlay's blurred-backdrop language. */
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
  .dialog .msg { margin: 0 0 var(--sp-5); color: var(--muted); font-size: var(--fs-sm); line-height: 1.5; }
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
  .btn.danger { color: var(--danger); border-color: var(--danger); }
  .btn.danger:hover { background: var(--danger); color: var(--ink); }
</style>
