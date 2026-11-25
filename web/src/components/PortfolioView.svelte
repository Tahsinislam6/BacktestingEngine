<script>
  import Metrics from './Metrics.svelte';
  import PortfolioChart from './PortfolioChart.svelte';
  import TradesTable from './TradesTable.svelte';
  import { whenText, num, pct } from '../lib/format.js';
  import { summaryRows } from '../lib/portfolio.js';

  let { run } = $props();

  const spec = $derived(run.spec);
  const result = $derived(run.result);
  const rows = $derived(result ? summaryRows(result) : []);
</script>

<div class="view">
  <div class="panel head">
    <h3>Portfolio #{run.id} <span class="kind">portfolio</span> <span class="badge {run.status}">{run.status}</span></h3>
    <p class="muted">
      {spec.sleeves.length} {spec.sleeves.length === 1 ? 'sleeve' : 'sleeves'} · {spec.periodsPerYear} bars/year ·
      {spec.commonPeriodOnly ? 'starts when every asset has data' : 'whole history'} · {whenText(run.createdAt)} UTC
    </p>
  </div>

  {#if run.status === 'queued' || run.status === 'running'}
    <div class="panel waiting"><span class="spinner"></span> {run.status === 'queued' ? 'Waiting in the queue…' : 'Running…'}</div>
  {:else if run.status === 'failed'}
    <div class="error-box"><b>This portfolio failed.</b> {run.error}</div>
  {:else if result}
    <Metrics metrics={result.combined.metrics} finalEquity={result.combined.finalEquity} cash={result.combined.cash} />

    <div class="panel">
      <h2>Sleeves and portfolio</h2>
      <div class="scroll">
        <table class="mono">
          <thead>
            <tr><th>Sleeve</th><th class="r">Cash</th><th class="r">Final</th><th class="r">Return</th><th class="r">CAGR</th><th class="r">Max DD</th><th class="r">MAR</th><th class="r">Sharpe</th><th class="r">Trades</th><th class="r">Win rate</th></tr>
          </thead>
          <tbody>
            {#each rows as r, i (i)}
              <tr class:total={r.total}>
                <td class="name">{r.name}{#if r.dataset}<span class="muted sub">{r.dataset} · {r.strategy}</span>{/if}</td>
                <td class="r">{num(r.cash, 0)}</td>
                <td class="r">{num(r.finalEquity, 2)}</td>
                <td class="r" class:pos={r.totalReturn > 0} class:neg={r.totalReturn < 0}>{pct(r.totalReturn)}</td>
                <td class="r">{pct(r.cagr)}</td>
                <td class="r">{pct(r.maxDrawdown)}</td>
                <td class="r">{num(r.mar)}</td>
                <td class="r">{num(r.sharpe)}</td>
                <td class="r">{r.trades}</td>
                <td class="r">{r.trades > 0 ? pct(r.winRate) : '—'}</td>
              </tr>
            {/each}
          </tbody>
        </table>
      </div>
      <p class="muted small">Each sleeve is measured against its own cash; the portfolio against all the cash together.</p>
    </div>

    <PortfolioChart {result} />
    <TradesTable trades={result.trades} openTrades={result.openTrades} showSleeve />
  {/if}
</div>

<style>
  .view { display: flex; flex-direction: column; gap: 16px; }
  h3 { font-size: 16px; display: flex; align-items: center; gap: 10px; }
  .head p { margin: 4px 0 0; }
  .small { font-size: 12px; margin: 10px 0 0; }
  .kind { font-size: 11px; font-weight: 600; padding: 2px 8px; border-radius: 99px; text-transform: uppercase; letter-spacing: 0.04em; background: rgba(110, 168, 254, 0.14); color: var(--accent); }
  .badge { font-size: 11px; font-weight: 600; padding: 2px 8px; border-radius: 99px; text-transform: uppercase; letter-spacing: 0.04em; background: var(--panel-2); color: var(--muted); }
  .badge.done { color: var(--good); background: rgba(76, 195, 138, 0.12); }
  .badge.failed { color: var(--bad); background: rgba(239, 107, 99, 0.12); }
  .badge.running, .badge.queued { color: var(--warn); background: rgba(229, 180, 90, 0.12); }
  .waiting { display: flex; align-items: center; gap: 12px; color: var(--muted); }
  .spinner { width: 14px; height: 14px; border-radius: 50%; border: 2px solid var(--border); border-top-color: var(--accent); animation: spin 0.8s linear infinite; }
  @keyframes spin { to { transform: rotate(360deg); } }
  .scroll { overflow-x: auto; margin-top: 10px; }
  table { width: 100%; border-collapse: collapse; font-size: 13px; }
  th { text-align: left; font-weight: 500; color: var(--muted); font-size: 12px; padding: 6px 10px; border-bottom: 1px solid var(--border); white-space: nowrap; }
  td { padding: 8px 10px; border-bottom: 1px solid var(--border); white-space: nowrap; }
  .r { text-align: right; }
  .name { font-weight: 600; }
  .sub { display: block; font-weight: 400; font-size: 12px; }
  tr.total td { border-top: 2px solid var(--border); font-weight: 600; background: var(--panel-2); }
</style>
