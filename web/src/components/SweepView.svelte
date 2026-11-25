<script>
  import SweepHeatmap from './SweepHeatmap.svelte';
  import NullHistogram from './NullHistogram.svelte';
  import { whenText, num, pct } from '../lib/format.js';

  let { run } = $props();

  const spec = $derived(run.spec);
  const result = $derived(run.result);
  const progress = $derived(run.progress && run.progress.total > 0 ? run.progress.done / run.progress.total : 0);
  const axisText = (a) => `${a.param} ${a.from} to ${a.to} step ${a.step}`;
</script>

<div class="view">
  <div class="panel head">
    <h3>Sweep #{run.id} <span class="kind">density</span> <span class="badge {run.status}">{run.status}</span></h3>
    <p class="muted">{spec.strategy} on {spec.dataset} · {whenText(run.createdAt)} UTC</p>
    <p class="muted small">
      {spec.axes.map(axisText).join(' · ')}<br />
      {spec.metric} above {spec.threshold} · radius {spec.radius} · min {spec.minTrades} trades ·
      {spec.nullRuns} shuffled copies, block {spec.block} · cash {num(spec.cash, 0)} · {spec.periodsPerYear} bars/year
      {#if Object.keys(spec.params ?? {}).length}<br />fixed: {Object.entries(spec.params).map(([k, v]) => `${k} = ${v}`).join(', ')}{/if}
    </p>
  </div>

  <div class="panel rule">
    <h2>Your pass rule, written before running</h2>
    <p>{spec.note}</p>
  </div>

  {#if run.status === 'queued' || run.status === 'running'}
    <div class="panel waiting">
      {#if run.status === 'queued'}
        <span class="spinner"></span> Waiting in the queue…
      {:else}
        <div class="bar-wrap">
          <div class="bar"><div class="fill" style:width={`${(progress * 100).toFixed(1)}%`}></div></div>
          <span class="muted small">
            {run.progress ? `${run.progress.done} of ${run.progress.total} grids done` : 'Starting…'} (the real data, then each shuffled copy)
          </span>
        </div>
      {/if}
    </div>
  {:else if run.status === 'failed'}
    <div class="error-box"><b>This sweep failed.</b> {run.error}</div>
  {:else if result}
    <div class="cards">
      <div class="card panel"><span class="label muted">Cells</span><span class="value mono">{num(result.real.validCells, 0)} / {num(result.cells, 0)}</span><span class="sub muted">valid / total</span></div>
      <div class="card panel"><span class="label muted">Ideas tried</span><span class="value mono">{num(result.ideasTried, 0)}</span><span class="sub muted">cells × (1 + {result.shuffled.runs})</span></div>
      <div class="card panel"><span class="label muted">Pass density</span><span class="value mono">{pct(result.real.passDensity)}</span><span class="sub muted">cells above {spec.threshold}</span></div>
      <div class="card panel"><span class="label muted">Plateau density</span><span class="value mono">{pct(result.real.plateauDensity)}</span><span class="sub muted">whole neighbourhood passes</span></div>
      <div class="card panel"><span class="label muted">Peak over neighbours</span><span class="value mono">{num(result.real.peakOverNeighbours, 4)}</span><span class="sub muted">a big number is a spike</span></div>
    </div>

    <div class="panel">
      <h2>Real data against shuffled data</h2>
      <table class="mono">
        <thead><tr><th></th><th class="r">Real</th><th class="r">Shuffled median</th><th class="r">Shuffled 95th</th><th class="r">Copies at or above real</th></tr></thead>
        <tbody>
          <tr>
            <td>Plateau density</td>
            <td class="r">{pct(result.real.plateauDensity)}</td>
            <td class="r">{pct(result.shuffled.plateauDensity.median)}</td>
            <td class="r">{pct(result.shuffled.plateauDensity.p95)}</td>
            <td class="r">{result.shuffled.plateauDensity.atLeastReal} / {result.shuffled.runs}</td>
          </tr>
          <tr>
            <td>Pass density</td>
            <td class="r">{pct(result.real.passDensity)}</td>
            <td class="r">{pct(result.shuffled.passDensity.median)}</td>
            <td class="r">{pct(result.shuffled.passDensity.p95)}</td>
            <td class="r">{result.shuffled.passDensity.atLeastReal} / {result.shuffled.runs}</td>
          </tr>
        </tbody>
      </table>
      <p class="muted small">Numbers only. Judge them against the rule you wrote before running; do not re-run with other settings to rescue a result.</p>
    </div>

    <SweepHeatmap {result} />
    <NullHistogram {result} />
  {/if}
</div>

<style>
  .view { display: flex; flex-direction: column; gap: 16px; }
  h3 { font-size: 16px; display: flex; align-items: center; gap: 10px; }
  .head p { margin: 4px 0 0; }
  .small { font-size: 12px; }
  .kind { font-size: 11px; font-weight: 600; padding: 2px 8px; border-radius: 99px; text-transform: uppercase; letter-spacing: 0.04em; background: rgba(110, 168, 254, 0.14); color: var(--accent); }
  .badge { font-size: 11px; font-weight: 600; padding: 2px 8px; border-radius: 99px; text-transform: uppercase; letter-spacing: 0.04em; background: var(--panel-2); color: var(--muted); }
  .badge.done { color: var(--good); background: rgba(76, 195, 138, 0.12); }
  .badge.failed { color: var(--bad); background: rgba(239, 107, 99, 0.12); }
  .badge.running, .badge.queued { color: var(--warn); background: rgba(229, 180, 90, 0.12); }
  .rule { border-color: rgba(229, 180, 90, 0.35); background: rgba(229, 180, 90, 0.05); }
  .rule p { margin: 8px 0 0; white-space: pre-wrap; }
  .waiting { display: flex; align-items: center; gap: 12px; color: var(--muted); }
  .bar-wrap { display: flex; flex-direction: column; gap: 8px; width: 100%; }
  .bar { height: 8px; border-radius: 99px; background: var(--panel-2); overflow: hidden; }
  .fill { height: 100%; background: var(--accent); transition: width 0.3s ease; }
  .spinner { width: 14px; height: 14px; border-radius: 50%; border: 2px solid var(--border); border-top-color: var(--accent); animation: spin 0.8s linear infinite; }
  @keyframes spin { to { transform: rotate(360deg); } }
  .cards { display: grid; grid-template-columns: repeat(auto-fill, minmax(170px, 1fr)); gap: 12px; }
  .card { display: flex; flex-direction: column; gap: 2px; padding: 12px 14px; }
  .label { font-size: 12px; }
  .value { font-size: 20px; font-weight: 600; }
  .sub { font-size: 11px; }
  table { width: 100%; border-collapse: collapse; font-size: 13px; margin-top: 10px; }
  th { text-align: left; font-weight: 500; color: var(--muted); font-size: 12px; padding: 6px 10px; border-bottom: 1px solid var(--border); }
  td { padding: 8px 10px; border-bottom: 1px solid var(--border); }
  .r { text-align: right; }
  .panel > .small { margin: 10px 0 0; }
</style>
