<script>
  import { pct, num, days } from '../lib/format.js';

  let { metrics, finalEquity, cash } = $props();

  // label, text, the sign to colour by (or null), and a short explanation shown on hover
  const cards = $derived([
    { label: 'Total return', text: pct(metrics.totalReturn), sign: metrics.totalReturn, help: 'Final equity compared with the starting cash' },
    { label: 'CAGR', text: pct(metrics.cagr), sign: metrics.cagr, help: 'Compound annual growth rate' },
    { label: 'Max drawdown', text: pct(metrics.maxDrawdown), sign: null, help: 'The largest fall from a peak of the equity' },
    { label: 'Sharpe', text: num(metrics.sharpe), sign: null, help: 'Return per unit of volatility (annualised)' },
    { label: 'Sortino', text: num(metrics.sortino), sign: null, help: 'Like Sharpe, but only downside volatility counts' },
    { label: 'MAR', text: num(metrics.mar), sign: null, help: 'CAGR divided by max drawdown' },
    { label: 'Trades', text: String(metrics.trades), sign: null, help: 'Closed trades (open ones are not counted)' },
    { label: 'Win rate', text: metrics.trades > 0 ? pct(metrics.winRate) : '—', sign: null, help: 'Share of closed trades that made money' },
    { label: 'Profit factor', text: metrics.trades > 0 ? num(metrics.profitFactor) : '—', sign: null, help: 'Total won divided by total lost; — when there is nothing to divide' },
    { label: 'Expectancy', text: metrics.trades > 0 ? num(metrics.expectancy) : '—', sign: metrics.trades > 0 ? metrics.expectancy : null, help: 'Average profit per closed trade, in account money' },
    { label: 'Expectancy (R)', text: metrics.tradesWithRisk > 0 ? num(metrics.expectancyR) + ' R' : '—', sign: metrics.tradesWithRisk > 0 ? metrics.expectancyR : null, help: `Average profit in units of the initial risk (${metrics.tradesWithRisk} trades had a stop)` },
    { label: 'Avg holding', text: metrics.trades > 0 ? days(metrics.avgHoldingTime) : '—', sign: null, help: 'Average time a closed trade was open' },
  ]);
</script>

<div class="summary panel">
  <div>
    <span class="muted small">Final equity</span>
    <div class="big mono">{num(finalEquity, 2)}</div>
  </div>
  <div class="muted small">from {num(cash, 0)}</div>
</div>

<div class="grid">
  {#each cards as c (c.label)}
    <div class="card panel" title={c.help}>
      <span class="label muted">{c.label}</span>
      <span class="value mono" class:pos={c.sign > 0} class:neg={c.sign < 0}>{c.text}</span>
    </div>
  {/each}
</div>

<style>
  .summary { display: flex; align-items: baseline; justify-content: space-between; }
  .small { font-size: 12px; }
  .big { font-size: 26px; font-weight: 600; letter-spacing: -0.02em; }
  .grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(150px, 1fr)); gap: 12px; }
  .card { display: flex; flex-direction: column; gap: 4px; padding: 12px 14px; }
  .label { font-size: 12px; }
  .value { font-size: 18px; font-weight: 600; }
</style>
