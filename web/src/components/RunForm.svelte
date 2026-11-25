<script>
  import LayoutSelect from './LayoutSelect.svelte';
  import { defaultParams, paramError, buildRequest } from '../lib/params.js';

  let { strategies, datasets, layouts = [], onsubmit } = $props();

  let strategyName = $state('');
  let dataset = $state('');
  let layout = $state('auto');
  let params = $state({});
  let cash = $state('10000');
  let periodsPerYear = $state('260');
  let busy = $state(false);
  let error = $state('');

  const strategy = $derived(strategies.find((s) => s.name === strategyName));

  // Pick the first strategy and dataset as soon as the lists arrive.
  $effect(() => {
    if (!strategyName && strategies.length > 0) strategyName = strategies[0].name;
  });
  $effect(() => {
    if (!dataset && datasets.length > 0) dataset = datasets[0];
  });
  // A new strategy brings its own parameters, each at its default.
  $effect(() => {
    params = defaultParams(strategy);
  });

  const paramErrors = $derived(
    Object.fromEntries((strategy?.params ?? []).map((p) => [p.name, paramError(p, params[p.name])])),
  );
  const cashError = $derived(Number(cash) > 0 && Number.isFinite(Number(cash)) ? null : 'Must be above 0');
  const periodsError = $derived(
    Number(periodsPerYear) > 0 && Number.isFinite(Number(periodsPerYear)) ? null : 'Must be above 0',
  );
  const valid = $derived(
    !!strategy && !!dataset && !cashError && !periodsError && Object.values(paramErrors).every((e) => e === null),
  );

  async function run(event) {
    event.preventDefault();
    if (!valid || busy) return;
    busy = true;
    error = '';
    try {
      await onsubmit(buildRequest({ strategy: strategyName, dataset, params, cash, periodsPerYear, layout }));
    } catch (e) {
      error = e.message;
    } finally {
      busy = false;
    }
  }
</script>

<form class="panel" onsubmit={run}>
  <h2>New run</h2>

  <div class="field">
    <label for="dataset">Dataset</label>
    <select id="dataset" bind:value={dataset}>
      {#each datasets as name}
        <option value={name}>{name}</option>
      {/each}
    </select>
  </div>

  <div class="field">
    <label for="layout">Layout</label>
    <LayoutSelect id="layout" {layouts} bind:value={layout} />
  </div>

  <div class="field">
    <label for="strategy">Strategy</label>
    <select id="strategy" bind:value={strategyName}>
      {#each strategies as s}
        <option value={s.name}>{s.name}</option>
      {/each}
    </select>
    {#if strategy?.description}<p class="hint">{strategy.description}</p>{/if}
  </div>

  {#if strategy?.params.length}
    <div class="group">
      <span class="group-title">Parameters</span>
      {#each strategy.params as p (p.name)}
        <div class="field">
          <label for={'p-' + p.name}>{p.name}</label>
          <input
            id={'p-' + p.name}
            class="mono"
            inputmode="decimal"
            bind:value={params[p.name]}
            aria-invalid={paramErrors[p.name] ? 'true' : 'false'}
          />
          {#if paramErrors[p.name]}
            <p class="hint bad">{paramErrors[p.name]}</p>
          {:else}
            <p class="hint">{p.description} · {p.min} to {p.max}{p.integer ? ', whole' : ''} · default {p.default}</p>
          {/if}
        </div>
      {/each}
    </div>
  {/if}

  <div class="row">
    <div class="field">
      <label for="cash">Starting cash</label>
      <input id="cash" class="mono" inputmode="decimal" bind:value={cash} aria-invalid={cashError ? 'true' : 'false'} />
      {#if cashError}<p class="hint bad">{cashError}</p>{/if}
    </div>
    <div class="field">
      <label for="ppy">Bars per year</label>
      <input id="ppy" class="mono" inputmode="decimal" bind:value={periodsPerYear} aria-invalid={periodsError ? 'true' : 'false'} />
      {#if periodsError}<p class="hint bad">{periodsError}</p>{/if}
    </div>
  </div>
  <p class="hint">260 for daily FX, 365 for crypto.</p>

  {#if error}<div class="error-box">{error}</div>{/if}

  <button class="primary" type="submit" disabled={!valid || busy}>{busy ? 'Starting…' : 'Run backtest'}</button>
</form>

<style>
  form { display: flex; flex-direction: column; gap: 12px; }
  .field { display: flex; flex-direction: column; }
  .row { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
  .group { display: flex; flex-direction: column; gap: 10px; padding: 10px 12px; background: var(--bg); border: 1px solid var(--border); border-radius: 8px; }
  .group-title { font-size: 11px; text-transform: uppercase; letter-spacing: 0.06em; color: var(--muted); }
  .hint { margin: 4px 0 0; font-size: 12px; color: var(--muted); }
  .hint.bad { color: var(--bad); }
  form > .hint { margin: -6px 0 0; }
</style>
