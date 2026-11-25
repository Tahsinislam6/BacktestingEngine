<script>
  import { postCancel } from '../lib/api.js';

  // Asks the server to cancel run `id`. The page notices the new state by itself, because it keeps polling the run.
  let { id } = $props();

  let busy = $state(false);
  let problem = $state('');

  async function cancel() {
    busy = true;
    problem = '';
    try {
      await postCancel(id);
    } catch (e) {
      problem = e.message;
    }
    busy = false;
  }
</script>

<span class="cancel">
  <button class="ghost" onclick={cancel} disabled={busy}>{busy ? 'Cancelling…' : 'Cancel'}</button>
  {#if problem}<span class="bad small">{problem}</span>{/if}
</span>

<style>
  .cancel { display: inline-flex; align-items: center; gap: 10px; }
  .ghost { background: transparent; border: 1px solid var(--border, #2a3140); color: var(--text, inherit); padding: 4px 12px; border-radius: 6px; cursor: pointer; }
  .ghost:disabled { opacity: 0.6; cursor: default; }
  .bad { color: var(--bad); }
</style>
