<script lang="ts">
  // Per-pedal preset bar, mirroring the rig/setlist editing language: a ▼
  // disclosure that lists the kind's presets, the loaded preset's name (with a
  // "*" when the knobs have unsaved edits), and inline rename / save / delete.
  // Switching presets while dirty prompts to save or discard; Save itself forks
  // into "overwrite the current preset" or "save as a new one".
  import { pedalPresets, loadPedalPresetNames, fxKind } from './store.svelte.js';
  import {
    activePresetName,
    presetDirty,
    loadPresetFor,
    savePresetAs,
    renamePresetFor,
    deletePresetFor,
  } from './presets.svelte.js';
  import type { Effect } from './types.js';

  let { fx }: { fx: Effect } = $props();

  // Presets are scoped by KIND, so every Drive shares one list.
  const kind = $derived(fxKind(fx.type));
  const presetNames = $derived(pedalPresets[kind] ?? []);
  const activeName = $derived(activePresetName(fx.type));
  const dirty = $derived(presetDirty(fx.type));

  // (Re)load the names whenever the selected pedal's kind changes.
  $effect(() => {
    loadPedalPresetNames(fx.type);
  });

  let menuOpen = $state(false);
  let renaming = $state(false);
  let renameDraft = $state('');
  let confirmingDelete = $state(false);
  let saveChoice = $state(false); // overwrite vs save-as-new fork
  let asking = $state(false); // name-a-new-preset input
  let draft = $state('');
  let pendingLoad = $state<null | (() => void)>(null); // load parked behind the dirty prompt

  function autofocus(el: HTMLInputElement) {
    el.focus();
    el.select();
  }

  // ---- pick / switch (guarded when the loaded preset has unsaved edits) ---
  function pick(name: string) {
    menuOpen = false;
    if (!name || name === activeName) return;
    const act = () => loadPresetFor(fx.type, name).catch(console.error);
    if (dirty) pendingLoad = act;
    else act();
  }
  async function loadSave() {
    const next = pendingLoad;
    pendingLoad = null;
    await savePresetAs(fx.type, activeName);
    next?.();
  }
  function loadDiscard() {
    const next = pendingLoad;
    pendingLoad = null;
    next?.();
  }

  // ---- rename ------------------------------------------------------------
  function startRename() {
    if (!activeName) return;
    menuOpen = false;
    renameDraft = activeName;
    renaming = true;
  }
  function commitRename() {
    const t = renameDraft.trim();
    if (t && t !== activeName) renamePresetFor(fx.type, activeName, t).catch(console.error);
    renaming = false;
  }
  function cancelRename() {
    renaming = false;
  }

  // ---- save (overwrite current / save as new) ----------------------------
  function clickSave() {
    if (activeName) saveChoice = true;
    else {
      draft = '';
      asking = true;
    }
  }
  function overwrite() {
    saveChoice = false;
    savePresetAs(fx.type, activeName).catch(console.error);
  }
  function saveAsNew() {
    saveChoice = false;
    draft = '';
    asking = true;
  }
  function confirmNew() {
    const n = draft.trim();
    if (!n) return;
    asking = false;
    savePresetAs(fx.type, n).catch(console.error);
  }

  // ---- delete ------------------------------------------------------------
  function requestDelete() {
    if (!activeName) return;
    menuOpen = false;
    confirmingDelete = true;
  }
  function doDelete() {
    confirmingDelete = false;
    deletePresetFor(fx.type, activeName).catch(console.error);
  }

  // Close the dropdown on outside click / Escape.
  $effect(() => {
    if (!menuOpen) return;
    const onDown = (e: PointerEvent) => {
      if (!(e.target as HTMLElement).closest('.dropdown')) menuOpen = false;
    };
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') menuOpen = false;
    };
    window.addEventListener('pointerdown', onDown);
    window.addEventListener('keydown', onKey);
    return () => {
      window.removeEventListener('pointerdown', onDown);
      window.removeEventListener('keydown', onKey);
    };
  });
</script>

<div class="preset-bar">
  {#if renaming}
    <input
      class="rename-input"
      type="text"
      placeholder="preset name…"
      bind:value={renameDraft}
      use:autofocus
      onkeydown={(e) => {
        if (e.key === 'Enter') commitRename();
        else if (e.key === 'Escape') cancelRename();
      }}
      onblur={commitRename}
    />
  {:else}
    <div class="dropdown">
      <button
        class="disclosure"
        class:open={menuOpen}
        class:placeholder={!activeName}
        onclick={() => (menuOpen = !menuOpen)}
        title="Choose a preset"
      >
        <svg class="tri" viewBox="0 0 16 16" width="11" height="11" aria-hidden="true">
          <path d="M3 5.5h10L8 12z" fill="currentColor" />
        </svg>
        <span class="label">{activeName || 'No preset'}</span>{#if dirty}<span
            class="star"
            title="Unsaved changes">*</span
          >{/if}
      </button>
      {#if menuOpen}
        <ul class="menu">
          {#each presetNames as n}
            <li>
              <button class:active={n === activeName} onclick={() => pick(n)}>{n}</button>
            </li>
          {/each}
          {#if !presetNames.length}<li class="empty">No presets saved.</li>{/if}
        </ul>
      {/if}
    </div>
  {/if}

  <div class="preset-actions">
    <button class="pbtn" disabled={!activeName} title="Rename preset" aria-label="Rename preset" onclick={startRename}>
      <svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <path d="M12 20h9" /><path d="M16.5 3.5a2.12 2.12 0 0 1 3 3L7 19l-4 1 1-4z" />
      </svg>
    </button>
    <button class="pbtn" title="Save preset" aria-label="Save preset" onclick={clickSave}>
      <svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z" />
        <path d="M17 21v-8H7v8M7 3v5h8" />
      </svg>
    </button>
    <button class="pbtn danger" disabled={!activeName} title="Delete preset" aria-label="Delete preset" onclick={requestDelete}>
      <svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
        <path d="M3 6h18M8 6V4a1 1 0 0 1 1-1h6a1 1 0 0 1 1 1v2m2 0v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6" />
        <path d="M10 11v6M14 11v6" />
      </svg>
    </button>
  </div>
</div>

{#if pendingLoad}
  <div class="scrim">
    <button class="veil" aria-label="Cancel" onclick={() => (pendingLoad = null)}></button>
    <div class="dialog" role="dialog" aria-modal="true" aria-label="Unsaved changes">
      <h4>Unsaved changes</h4>
      <p>“{activeName}” has unsaved edits. Save them before switching presets?</p>
      <div class="dlg-actions">
        <button class="db" onclick={() => (pendingLoad = null)}>Cancel</button>
        <button class="db danger" onclick={loadDiscard}>Discard</button>
        <button class="db primary" onclick={loadSave}>Save</button>
      </div>
    </div>
  </div>
{/if}

{#if saveChoice}
  <div class="scrim">
    <button class="veil" aria-label="Cancel" onclick={() => (saveChoice = false)}></button>
    <div class="dialog" role="dialog" aria-modal="true" aria-label="Save preset">
      <h4>Save preset</h4>
      <p>Overwrite “{activeName}”, or save the current knobs as a new preset?</p>
      <div class="dlg-actions">
        <button class="db" onclick={() => (saveChoice = false)}>Cancel</button>
        <button class="db" onclick={saveAsNew}>Save as new…</button>
        <button class="db primary" onclick={overwrite}>Save “{activeName}”</button>
      </div>
    </div>
  </div>
{/if}

{#if asking}
  <div class="scrim">
    <button class="veil" aria-label="Cancel" onclick={() => (asking = false)}></button>
    <div class="dialog" role="dialog" aria-modal="true" aria-label="Name preset">
      <h4>Save as new preset</h4>
      <input
        class="dlg-input"
        type="text"
        placeholder="preset name…"
        bind:value={draft}
        use:autofocus
        onkeydown={(e) => {
          if (e.key === 'Enter') confirmNew();
          else if (e.key === 'Escape') asking = false;
        }}
      />
      <div class="dlg-actions">
        <button class="db" onclick={() => (asking = false)}>Cancel</button>
        <button class="db primary" onclick={confirmNew} disabled={!draft.trim()}>Save</button>
      </div>
    </div>
  </div>
{/if}

{#if confirmingDelete}
  <div class="scrim">
    <button class="veil" aria-label="Cancel" onclick={() => (confirmingDelete = false)}></button>
    <div class="dialog" role="dialog" aria-modal="true" aria-label="Delete preset">
      <h4>Delete preset</h4>
      <p>Delete “{activeName}”? This can't be undone.</p>
      <div class="dlg-actions">
        <button class="db" onclick={() => (confirmingDelete = false)}>Cancel</button>
        <button class="db danger" onclick={doDelete}>Delete</button>
      </div>
    </div>
  </div>
{/if}

<style>
  .preset-bar { display: flex; align-items: center; gap: var(--sp-2); margin-bottom: var(--sp-3); }

  /* Disclosure picker (mirrors the sidebar/editor menus, sized for the panel). */
  .dropdown { position: relative; flex: 1; min-width: 0; }
  .disclosure {
    display: flex;
    align-items: center;
    gap: var(--sp-2);
    width: 100%;
    text-align: left;
    background: var(--inset);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    padding: var(--sp-2) var(--sp-3);
    font: inherit;
    font-size: var(--fs-xs);
    cursor: pointer;
    transition: border-color var(--t-fast);
  }
  .disclosure:hover { border-color: var(--accent); }
  .disclosure.placeholder .label { color: var(--muted); }
  .disclosure .label { flex: 1; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .tri { flex: 0 0 auto; color: var(--muted); transition: transform var(--t-fast); }
  .disclosure.open .tri { transform: rotate(180deg); }
  .star { flex: 0 0 auto; color: var(--accent); }

  .menu {
    position: absolute;
    top: calc(100% + var(--sp-1));
    left: 0;
    right: 0;
    z-index: 40;
    max-height: 240px;
    overflow-y: auto;
    list-style: none;
    margin: 0;
    padding: var(--sp-1);
    background: var(--panel-2);
    border: 1px solid var(--line-2);
    border-radius: var(--r-md);
    box-shadow: var(--shadow-2);
  }
  .menu li { list-style: none; }
  .menu li button {
    display: block;
    width: 100%;
    text-align: left;
    background: none;
    border: none;
    color: var(--text);
    font: inherit;
    font-size: var(--fs-xs);
    padding: var(--sp-2) var(--sp-3);
    border-radius: var(--r-sm);
    cursor: pointer;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .menu li button:hover { background: var(--inset); }
  .menu li button.active { color: var(--accent); font-weight: 600; }
  .menu .empty { color: var(--muted); font-size: var(--fs-xs); padding: var(--sp-2) var(--sp-3); }

  .rename-input {
    flex: 1;
    min-width: 0;
    background: var(--inset);
    color: var(--text);
    border: 1px solid var(--accent);
    border-radius: var(--r-sm);
    padding: var(--sp-2) var(--sp-3);
    font: inherit;
    font-size: var(--fs-xs);
  }
  .rename-input:focus { outline: none; }

  .preset-actions { display: flex; gap: var(--sp-1); flex: 0 0 auto; }
  .pbtn {
    width: 26px;
    height: 26px;
    display: grid;
    place-items: center;
    background: var(--inset);
    color: var(--muted);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    cursor: pointer;
    transition: color var(--t-fast), border-color var(--t-fast);
  }
  .pbtn:hover:not(:disabled) { color: var(--accent); border-color: var(--accent); }
  .pbtn.danger:hover:not(:disabled) { color: var(--danger); border-color: var(--danger); }
  .pbtn:disabled { opacity: .4; cursor: default; }

  /* Dialogs -- blurred-backdrop language, sized small for these quick prompts. */
  .scrim {
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
  .veil { position: absolute; inset: 0; background: none; border: none; padding: 0; cursor: default; }
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
  .dialog h4 {
    margin: 0 0 var(--sp-3);
    font-size: var(--fs-sm);
    letter-spacing: var(--track-wide);
    text-transform: uppercase;
    color: var(--muted);
    font-weight: 600;
  }
  .dialog p { margin: 0 0 var(--sp-5); color: var(--muted); font-size: var(--fs-sm); line-height: 1.5; }
  .dlg-input {
    width: 100%;
    background: var(--panel-2);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    padding: var(--sp-3) var(--sp-4);
    font: inherit;
    margin-bottom: var(--sp-5);
  }
  .dlg-input:focus { outline: none; border-color: var(--accent); }
  .dlg-actions { display: flex; justify-content: flex-end; gap: var(--sp-3); flex-wrap: wrap; }
  .db {
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
  .db:hover:not(:disabled) { border-color: var(--accent); }
  .db:disabled { opacity: .4; cursor: default; }
  .db.primary { background: var(--accent); color: var(--ink); border-color: var(--accent); }
  .db.primary:hover:not(:disabled) { box-shadow: var(--glow) var(--accent); }
  .db.danger { color: var(--danger); border-color: var(--danger); }
  .db.danger:hover { background: var(--danger); color: var(--ink); }
</style>
