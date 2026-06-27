<script lang="ts">
  import { api } from './api.js';
  import { applyState, fxKind } from './store.svelte.js';
  import { FS_COLORS } from './fsColors.js';
  import type { BoardState, Effect } from './types.js';
  import FxIcon from './FxIcon.svelte';

  let { fx, selected = false, onselect }: {
    fx: Effect;
    selected?: boolean;
    onselect: (type: string) => void;
  } = $props();

  // Icon tint follows the assigned footswitch; unassigned reads as a neutral dot.
  const iconColor = $derived(fx.fsAssign >= 0 ? FS_COLORS[fx.fsAssign] : 'var(--muted)');
  const kind = $derived(fxKind(fx.type));

  let dragOver = $state(false);

  function togglePower(e: MouseEvent) {
    e.stopPropagation(); // power only -- never changes the selection
    const on = !fx.enabled;
    fx.enabled = on; // optimistic; the SSE echo confirms
    api('/api/enabled', { effect: fx.type, enabled: on }).catch(console.error);
  }

  function onDragStart(e: DragEvent) {
    e.dataTransfer?.setData('text/plain', String(fx.slot));
    if (e.dataTransfer) e.dataTransfer.effectAllowed = 'move';
  }

  // Drop the dragged effect just before this one; the server repacks the chain.
  async function onDrop(e: DragEvent) {
    e.preventDefault();
    dragOver = false;
    const from = Number(e.dataTransfer?.getData('text/plain'));
    if (!Number.isInteger(from) || from === fx.slot) return;
    applyState(await api<BoardState>('/api/fx/reorder', { slot: from, to: fx.slot }));
  }
</script>

<div
  class="tile"
  class:off={!fx.enabled}
  class:selected
  class:drag-over={dragOver}
  role="button"
  tabindex="0"
  aria-pressed={selected}
  draggable="true"
  ondragstart={onDragStart}
  ondragover={(e) => {
    e.preventDefault();
    dragOver = true;
  }}
  ondragleave={() => (dragOver = false)}
  ondrop={onDrop}
  onclick={() => onselect(fx.type)}
  onkeydown={(e) => {
    if (e.key === 'Enter' || e.key === ' ') {
      e.preventDefault();
      onselect(fx.type);
    }
  }}
>
  <button
    class="power"
    class:on={fx.enabled}
    title="Enable / disable"
    aria-label="Enable / disable {fx.name}"
    onclick={togglePower}
  ></button>
  <span class="icon"><FxIcon {kind} color={iconColor} /></span>
  <span class="name">{fx.name}</span>
</div>

<style>
  /* Flat tile -- no card chrome; a thin border separates it, the accent rail
     marks selection. */
  .tile {
    display: flex;
    align-items: center;
    gap: var(--sp-3);
    padding: var(--sp-3);
    min-height: 78px;
    border: 1px solid var(--line);
    border-radius: var(--r-md);
    background: transparent;
    cursor: pointer;
    transition: border-color var(--t-fast), background var(--t-fast), opacity var(--t-med);
  }
  .tile:hover { border-color: var(--accent); }
  .tile.off { opacity: .5; }
  .tile.selected { border-color: var(--accent); background: var(--panel-2); }
  .tile.drag-over { border-color: var(--accent); border-style: dashed; }

  .icon { flex: 0 0 auto; display: flex; line-height: 0; }
  .name {
    flex: 1;
    min-width: 0;
    font-size: var(--fs-sm);
    font-weight: 600;
    letter-spacing: var(--track);
    color: var(--text);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  /* On/off power dot -- same language as the input/output pedals. */
  .power {
    flex: 0 0 auto;
    width: 22px;
    height: 22px;
    border-radius: 50%;
    border: 2px solid var(--line);
    background: var(--inset);
    cursor: pointer;
  }
  .power.on { border-color: var(--ok); box-shadow: var(--glow) var(--ok); background: var(--ok); }
</style>
