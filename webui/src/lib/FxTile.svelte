<script lang="ts">
  import { api } from './api.js';
  import { applyState, fxKind } from './store.svelte.js';
  import { FS_COLORS } from './fsColors.js';
  import type { BoardState, Effect } from './types.js';
  import FxIcon from './FxIcon.svelte';
  import Switch from './controls/Switch.svelte';

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

  // Drop the dragged effect onto this one: they swap cells. Free-form, so the
  // grid keeps its exact positions and any gaps (the server doesn't repack).
  async function onDrop(e: DragEvent) {
    e.preventDefault();
    dragOver = false;
    const from = Number(e.dataTransfer?.getData('text/plain'));
    if (!Number.isInteger(from) || from === fx.slot) return;
    applyState(await api<BoardState>('/api/fx/moveto', { slot: from, to: fx.slot }));
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
  <Switch vertical on={fx.enabled} label="Enable / disable {fx.name}" onclick={togglePower} />
  <span class="icon"><FxIcon {kind} color={iconColor} /></span>
  <span class="name">{fx.name}</span>
</div>

<style>
  /* Filled tile -- a resting fill lifts it clear of the page so an unselected
     pedal still reads as an object; the accent border + rail + brighter fill
     mark selection. */
  .tile {
    display: flex;
    align-items: center;
    gap: var(--sp-3);
    padding: var(--sp-3);
    min-height: 78px;
    border: 1px solid var(--line);
    border-radius: var(--r-md);
    background: var(--panel);
    cursor: pointer;
    transition: border-color var(--t-fast), background var(--t-fast), opacity var(--t-med);
  }
  .tile:hover { border-color: var(--accent); }
  .tile.off { opacity: .5; }
  .tile.selected { border-color: var(--accent); background: var(--panel-2); box-shadow: inset 3px 0 0 var(--accent); }
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
</style>
