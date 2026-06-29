<script lang="ts">
  // Top-bar tuner trigger. Presentational: App owns the open state and renders the
  // modal (which does the real engage/mute). Lit when the tuner overlay is open.
  import Button from './controls/Button.svelte';
  let { active = false, onclick }: { active?: boolean; onclick: () => void } = $props();
</script>

<Button {active} toggle title="Tuner — silent tuning" {onclick}>
  <!-- Tuning fork: two prongs joined by a rounded base, with a stem below. -->
  <svg
    class="ico"
    viewBox="0 0 24 24"
    width="15"
    height="15"
    fill="none"
    stroke="currentColor"
    stroke-width="2"
    stroke-linecap="round"
    stroke-linejoin="round"
    aria-hidden="true"
  >
    <path d="M8 3 v7 a4 4 0 0 0 8 0 V3" />
    <path d="M12 14 v7" />
  </svg>
  Tuner
</Button>
