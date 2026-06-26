<script lang="ts">
  // Top-bar tuner trigger. Presentational: App owns the open state and renders the
  // modal (which does the real engage/mute). Lit when the tuner overlay is open.
  let { active = false, onclick }: { active?: boolean; onclick: () => void } = $props();
</script>

<button class="tuner" class:engaged={active} title="Tuner — silent tuning" {onclick}>
  <span class="ico">♪</span>
  Tuner
</button>

<style>
  .tuner {
    display: inline-flex;
    align-items: center;
    gap: var(--sp-2);
    background: var(--panel-2);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: var(--r-sm);
    padding: var(--sp-3) var(--sp-4);
    font: inherit;
    font-weight: 600;
    cursor: pointer;
    transition: color var(--t-fast), border-color var(--t-fast), background var(--t-fast), box-shadow var(--t-fast);
  }
  .tuner .ico { color: var(--muted); transition: color var(--t-fast); }
  .tuner:hover { border-color: var(--accent); }
  .tuner.engaged {
    background: var(--accent);
    color: var(--ink);
    border-color: var(--accent);
    box-shadow: var(--glow) var(--accent);
  }
  .tuner.engaged .ico { color: var(--ink); }
</style>
