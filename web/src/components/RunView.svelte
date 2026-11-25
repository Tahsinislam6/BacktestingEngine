<script>
  import Metrics from './Metrics.svelte';
  import EquityChart from './EquityChart.svelte';
  import TradesTable from './TradesTable.svelte';
  import CancelButton from './CancelButton.svelte';
  import { whenText, num, layoutText } from '../lib/format.js';

  let { run } = $props();

  const spec = $derived(run.spec);
  const params = $derived(Object.entries(spec.params ?? {}));
</script>

<div class="view">
  <div class="panel head">
    <div>
      <h3>Run #{run.id} <span class="badge {run.status}">{run.status}</span></h3>
      <p class="muted">
        {spec.strategy} on {spec.dataset} · cash {num(spec.cash, 0)} · {spec.periodsPerYear} bars/year{layoutText(spec.layout)} · {whenText(run.createdAt)} UTC
      </p>
      {#if params.length}
        <p class="params mono">{#each params as [name, value]}<span>{name} = {value}</span>{/each}</p>
      {/if}
    </div>
  </div>

  {#if run.status === 'queued' || run.status === 'running'}
    <div class="panel waiting"><span class="spinner"></span> {run.status === 'queued' ? 'Waiting in the queue…' : 'Running…'}
      {#if run.status === 'queued'}<CancelButton id={run.id} />{/if}</div>
  {:else if run.status === 'cancelled'}
    <div class="panel waiting">This run was cancelled before it started, so it has no result.</div>
  {:else if run.status === 'failed'}
    <div class="error-box"><b>This run failed.</b> {run.error}</div>
  {:else if run.result}
    <Metrics metrics={run.result.metrics} finalEquity={run.result.finalEquity} cash={spec.cash} />
    <EquityChart timestamps={run.result.timestamps} equity={run.result.equity} trades={run.result.trades} />
    <TradesTable trades={run.result.trades} openTrades={run.result.openTrades} />
  {/if}
</div>

<style>
  .view { display: flex; flex-direction: column; gap: 16px; }
  h3 { font-size: 16px; display: flex; align-items: center; gap: 10px; }
  .head p { margin: 4px 0 0; }
  .params { display: flex; flex-wrap: wrap; gap: 6px 14px; font-size: 12px; color: var(--muted); }
  .badge { font-size: 11px; font-weight: 600; padding: 2px 8px; border-radius: 99px; text-transform: uppercase; letter-spacing: 0.04em; background: var(--panel-2); color: var(--muted); }
  .badge.done { color: var(--good); background: rgba(76, 195, 138, 0.12); }
  .badge.failed { color: var(--bad); background: rgba(239, 107, 99, 0.12); }
  .badge.running, .badge.queued { color: var(--warn); background: rgba(229, 180, 90, 0.12); }
  .waiting { display: flex; align-items: center; gap: 12px; color: var(--muted); }
  .spinner { width: 14px; height: 14px; border-radius: 50%; border: 2px solid var(--border); border-top-color: var(--accent); animation: spin 0.8s linear infinite; }
  @keyframes spin { to { transform: rotate(360deg); } }
</style>
