<script>
  import { dateOf, num } from '../lib/format.js';
  import { tradeKey } from '../lib/portfolio.js';

  let { trades, openTrades, showSleeve = false } = $props();

  const LIMIT = 200;
  let showAll = $state(false);

  // Newest first; the open trades (still running) go on top.
  const closedNewestFirst = $derived([...trades].reverse());
  const shown = $derived(showAll ? closedNewestFirst : closedNewestFirst.slice(0, LIMIT));
</script>

<div class="panel">
  <h2>Trades</h2>
  {#if trades.length === 0 && openTrades.length === 0}
    <p class="muted none">No trades.</p>
  {:else}
    <div class="scroll">
      <table class="mono">
        <thead>
          <tr>{#if showSleeve}<th>Sleeve</th>{/if}<th>Side</th><th>Entry</th><th>Exit</th><th class="r">Entry price</th><th class="r">Exit price</th><th class="r">P&amp;L</th><th>Reason</th></tr>
        </thead>
        <tbody>
          {#each openTrades as t (tradeKey(t))}
            <tr>
              {#if showSleeve}<td>{t.sleeve}</td>{/if}
              <td class={t.direction}>{t.direction}</td>
              <td>{dateOf(t.entryTime)}</td>
              <td><span class="open">open</span></td>
              <td class="r">{num(t.entryPrice, 4)}</td>
              <td class="r muted">—</td>
              <td class="r muted">—</td>
              <td class="muted">still open</td>
            </tr>
          {/each}
          {#each shown as t (tradeKey(t))}
            <tr>
              {#if showSleeve}<td>{t.sleeve}</td>{/if}
              <td class={t.direction}>{t.direction}</td>
              <td>{dateOf(t.entryTime)}</td>
              <td>{dateOf(t.exitTime)}</td>
              <td class="r">{num(t.entryPrice, 4)}</td>
              <td class="r">{num(t.exitPrice, 4)}</td>
              <td class="r" class:pos={t.pnl > 0} class:neg={t.pnl < 0}>{num(t.pnl)}</td>
              <td class="muted">{t.reason}</td>
            </tr>
          {/each}
        </tbody>
      </table>
    </div>
    {#if !showAll && closedNewestFirst.length > LIMIT}
      <p class="more muted">Showing the latest {LIMIT} of {closedNewestFirst.length} closed trades. <button onclick={() => (showAll = true)}>Show all</button></p>
    {/if}
  {/if}
</div>

<style>
  .none { margin: 10px 0 0; }
  .scroll { overflow: auto; max-height: 420px; margin-top: 10px; }
  table { width: 100%; border-collapse: collapse; font-size: 13px; }
  th { text-align: left; font-weight: 500; color: var(--muted); font-size: 12px; padding: 6px 10px; position: sticky; top: 0; background: var(--panel); border-bottom: 1px solid var(--border); }
  td { padding: 6px 10px; border-bottom: 1px solid var(--border); white-space: nowrap; }
  .r { text-align: right; }
  .long { color: var(--good); }
  .short { color: var(--bad); }
  .open { color: var(--warn); }
  .more { margin: 10px 0 0; }
  .more button { padding: 2px 10px; margin-left: 6px; }
</style>
