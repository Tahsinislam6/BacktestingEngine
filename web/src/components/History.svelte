<script>
  import { pct, whenText, num } from '../lib/format.js';

  let { runs, selectedId, onselect, ideasTried } = $props();

  const datasetsTried = $derived(Object.entries(ideasTried ?? {}).sort((a, b) => a[0].localeCompare(b[0])));
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
              <span class="title">#{r.id} · {r.spec.strategy}{#if r.kind === 'sweep'} <span class="tag">sweep</span>{/if}</span>
              <span class="sub muted">{r.spec.dataset} · {whenText(r.createdAt)}</span>
            </span>
            {#if r.status === 'done' && r.kind === 'sweep' && r.headline}
              <span class="mono ret muted" title="plateau density of the real data">plateau {pct(r.headline.plateauDensity, 0)}</span>
            {:else if r.status === 'done'}
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
    {#if datasetsTried.length}
      <div class="tried">
        <span class="tried-title muted">Ideas tried per dataset</span>
        {#each datasetsTried as [name, count]}
          <div class="tried-row"><span class="muted">{name}</span><span class="mono">{num(count, 0)}</span></div>
        {/each}
      </div>
    {/if}
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
  .tag { font-size: 10px; font-weight: 600; text-transform: uppercase; letter-spacing: 0.04em; color: var(--accent); background: rgba(110, 168, 254, 0.14); padding: 1px 6px; border-radius: 99px; margin-left: 4px; }
  .tried { margin-top: 12px; padding-top: 10px; border-top: 1px solid var(--border); display: flex; flex-direction: column; gap: 4px; font-size: 12px; }
  .tried-title { font-size: 11px; text-transform: uppercase; letter-spacing: 0.06em; }
  .tried-row { display: flex; justify-content: space-between; }
  .status { font-size: 12px; }
  .dot { width: 8px; height: 8px; border-radius: 50%; flex: none; background: var(--muted); }
  .dot.done { background: var(--good); }
  .dot.failed { background: var(--bad); }
  .dot.running, .dot.queued { background: var(--warn); }
</style>
