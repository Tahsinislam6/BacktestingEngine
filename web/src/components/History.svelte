<script>
  import { pct, whenText } from '../lib/format.js';

  let { runs, selectedId, onselect } = $props();
</script>

<div class="panel">
  <h2>History</h2>
  {#if runs.length === 0}
    <p class="muted empty">Nothing run yet.</p>
  {:else}
    <ul>
      {#each runs as r (r.id)}
        <li>
          <button class:active={r.id === selectedId} onclick={() => onselect(r.id)}>
            <span class="dot {r.status}" title={r.status}></span>
            <span class="main">
              <span class="title">#{r.id} · {r.spec.strategy}</span>
              <span class="sub muted">{r.spec.dataset} · {whenText(r.createdAt)}</span>
            </span>
            {#if r.status === 'done'}
              <span class="mono ret" class:pos={r.metrics?.totalReturn > 0} class:neg={r.metrics?.totalReturn < 0}>
                {pct(r.metrics?.totalReturn, 0)}
              </span>
            {:else}
              <span class="status muted">{r.status}</span>
            {/if}
          </button>
        </li>
      {/each}
    </ul>
  {/if}
</div>

<style>
  .empty { margin: 10px 0 0; }
  ul { list-style: none; margin: 10px 0 0; padding: 0; display: flex; flex-direction: column; gap: 4px; max-height: 360px; overflow-y: auto; }
  li button {
    width: 100%; display: flex; align-items: center; gap: 10px; text-align: left;
    background: transparent; border-color: transparent; padding: 8px 8px;
  }
  li button:hover:not(:disabled) { background: var(--panel-2); border-color: transparent; }
  li button.active { background: var(--panel-2); border-color: var(--border); }
  .main { display: flex; flex-direction: column; min-width: 0; flex: 1; }
  .title { white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .sub { font-size: 12px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .ret { font-size: 13px; }
  .status { font-size: 12px; }
  .dot { width: 8px; height: 8px; border-radius: 50%; flex: none; background: var(--muted); }
  .dot.done { background: var(--good); }
  .dot.failed { background: var(--bad); }
  .dot.running, .dot.queued { background: var(--warn); }
</style>
