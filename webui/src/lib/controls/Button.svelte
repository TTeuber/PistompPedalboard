<script lang="ts">
  // Button in the same visual language as the Knob / Fader / Switch: a recessed
  // inset well behind a 2px frame that lights to the accent on hover/focus. When
  // `active` it fills with the accent and glows, the way Switch's on-state and the
  // knob/fader fills do -- so a toggle (e.g. the tuner) reads as "engaged".
  //
  //   color   -- the accent it lights to (defaults to the gold --accent)
  //   active  -- filled + glowing (toggle on / engaged / a brief flash)
  //   toggle  -- mark it a toggle for assistive tech (aria-pressed)
  //   square  -- icon-only square (the prev/next style steppers)
  //   href    -- render an <a> instead of a <button> (keeps links semantic)
  import type { Snippet } from 'svelte';

  let {
    active = false,
    disabled = false,
    toggle = false,
    square = false,
    color = 'var(--accent)',
    title = '',
    href = undefined,
    onclick = undefined,
    children,
  }: {
    active?: boolean;
    disabled?: boolean;
    toggle?: boolean;
    square?: boolean;
    color?: string;
    title?: string;
    href?: string;
    onclick?: (e: MouseEvent) => void;
    children?: Snippet;
  } = $props();
</script>

{#if href}
  <a class="btn" class:active class:square style="--btn:{color}" {href} {title} {onclick}>
    {@render children?.()}
  </a>
{:else}
  <button
    class="btn"
    class:active
    class:square
    style="--btn:{color}"
    {disabled}
    {title}
    aria-pressed={toggle ? active : undefined}
    {onclick}
  >
    {@render children?.()}
  </button>
{/if}

<style>
  .btn {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    gap: var(--sp-2);
    background: var(--inset);
    color: var(--text);
    border: 2px solid var(--line);
    border-radius: var(--r-sm);
    padding: var(--sp-2) var(--sp-4);
    font: inherit;
    font-weight: 600;
    letter-spacing: var(--track);
    line-height: 1;
    text-decoration: none;
    cursor: pointer;
    transition: color var(--t-fast), border-color var(--t-fast), background var(--t-fast),
      box-shadow var(--t-fast);
  }
  .btn.square { padding: 0; width: 32px; height: 32px; }

  .btn:hover:not(:disabled) { border-color: var(--btn); color: var(--btn); }
  .btn:focus-visible { outline: none; border-color: var(--btn); }

  /* Engaged: the accent fill + glow that the Switch's on-state uses. */
  .btn.active {
    background: var(--btn);
    color: var(--ink);
    border-color: var(--btn);
    box-shadow: var(--glow) var(--btn);
  }
  .btn.active:hover:not(:disabled) { color: var(--ink); }

  .btn:disabled { opacity: .4; cursor: default; }
</style>
