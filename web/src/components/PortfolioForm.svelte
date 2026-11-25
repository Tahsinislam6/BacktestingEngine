<script>
  import { defaultParams } from '../lib/params.js';
  import { newSleeve, sleeveErrors, portfolioError, totalCash, buildPortfolioRequest, MAX_SLEEVES } from '../lib/portfolio.js';
  import { num } from '../lib/format.js';

  let { strategies, datasets, onsubmit } = $props();

  let sleeves = $state([]);
  let commonPeriodOnly = $state(false);
  let periodsPerYear = $state('260');
  let busy = $state(false);
  let error = $state('');

  // Start with one sleeve as soon as the lists arrive.
  $effect(() => {
    if (sleeves.length === 0 && strategies.length > 0 && datasets.length > 0) {
      sleeves = [newSleeve(strategies, datasets, [])];
    }
  });

  const specOf = (sleeve) => strategies.find((s) => s.name === sleeve.strategy);

  function add() {
    sleeves.push(newSleeve(strategies, datasets, sleeves.map((s) => s.name)));
  }
  function remove(index) {
    sleeves.splice(index, 1);
  }
  // A new strategy brings its own parameters, each at its default.
  function chooseStrategy(sleeve, name) {
    sleeve.strategy = name;
    sleeve.params = defaultParams(strategies.find((s) => s.name === name));
  }

  const errors = $derived(
    sleeves.map((s, i) =>
      sleeveErrors(s, strategies, sleeves.filter((_, j) => j !== i).map((o) => o.name.trim())),
    ),
  );
  const overall = $derived(portfolioError(sleeves, periodsPerYear));
  const valid = $derived(!overall && errors.every((e) => e === null));
  const cash = $derived(totalCash(sleeves));

  async function run(event) {
    event.preventDefault();
    if (!valid || busy) return;
    busy = true;
    error = '';
    try {
      await onsubmit(buildPortfolioRequest({ sleeves, commonPeriodOnly, periodsPerYear }));
    } catch (e) {
      error = e.message;
    } finally {
      busy = false;
    }
  }
</script>

<form class="panel" onsubmit={run}>
  <h2>Portfolio</h2>
  <p class="hint first">Each sleeve is one dataset with its own strategy, settings and cash. Nothing moves between sleeves.</p>

  {#each sleeves as sleeve, i (i)}
    <div class="sleeve">
      <div class="top">
        <input class="name" aria-label="Sleeve name" bind:value={sleeve.name} aria-invalid={errors[i]?.name ? 'true' : 'false'} />
        <button type="button" class="x" title="Remove this sleeve" onclick={() => remove(i)}>✕</button>
      </div>
      {#if errors[i]?.name}<p class="hint bad">{errors[i].name}</p>{/if}

      <div class="row">
        <div class="field">
          <label for={'p-ds-' + i}>Dataset</label>
          <select id={'p-ds-' + i} bind:value={sleeve.dataset}>{#each datasets as name}<option value={name}>{name}</option>{/each}</select>
        </div>
        <div class="field">
          <label for={'p-st-' + i}>Strategy</label>
          <select id={'p-st-' + i} value={sleeve.strategy} onchange={(e) => chooseStrategy(sleeve, e.currentTarget.value)}>
            {#each strategies as s}<option value={s.name}>{s.name}</option>{/each}
          </select>
        </div>
      </div>

      <div class="row">
        <div class="field">
          <label for={'p-cash-' + i}>Cash</label>
          <input id={'p-cash-' + i} class="mono" inputmode="decimal" bind:value={sleeve.cash} aria-invalid={errors[i]?.cash ? 'true' : 'false'} />
          {#if errors[i]?.cash}<p class="hint bad">{errors[i].cash}</p>{/if}
        </div>
        {#each specOf(sleeve)?.params ?? [] as p (p.name)}
          <div class="field">
            <label for={'p-' + i + '-' + p.name} title={p.description}>{p.name}</label>
            <input id={'p-' + i + '-' + p.name} class="mono" inputmode="decimal" bind:value={sleeve.params[p.name]} aria-invalid={errors[i]?.params?.[p.name] ? 'true' : 'false'} />
            {#if errors[i]?.params?.[p.name]}<p class="hint bad">{errors[i].params[p.name]}</p>{/if}
          </div>
        {/each}
      </div>
    </div>
  {/each}

  <button type="button" onclick={add} disabled={sleeves.length >= MAX_SLEEVES}>＋ Add sleeve</button>

  <div class="group">
    <label class="check"><input type="checkbox" bind:checked={commonPeriodOnly} /> Start when every asset has data</label>
    <p class="hint">Otherwise an asset whose data starts years later sits in cash and dilutes the result.</p>
    <div class="field">
      <label for="p-ppy">Bars per year</label>
      <input id="p-ppy" class="mono" inputmode="decimal" bind:value={periodsPerYear} aria-invalid={overall && overall.startsWith('Bars') ? 'true' : 'false'} />
      <p class="hint">260 for daily FX, 365 for crypto.</p>
    </div>
  </div>

  <div class="size">
    <b class="mono">{sleeves.length}</b> {sleeves.length === 1 ? 'sleeve' : 'sleeves'} · total cash <b class="mono">{num(cash, 0)}</b>
    {#if overall}<p class="hint bad">{overall}</p>{/if}
  </div>

  {#if error}<div class="error-box">{error}</div>{/if}

  <button class="primary" type="submit" disabled={!valid || busy}>{busy ? 'Starting…' : 'Run portfolio'}</button>
</form>

<style>
  form { display: flex; flex-direction: column; gap: 12px; }
  .field { display: flex; flex-direction: column; min-width: 0; }
  .row { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
  .sleeve { display: flex; flex-direction: column; gap: 10px; padding: 10px 12px; background: var(--bg); border: 1px solid var(--border); border-radius: 8px; }
  .top { display: flex; gap: 8px; }
  .name { font-weight: 600; }
  .x { padding: 4px 10px; color: var(--muted); }
  .x:hover:not(:disabled) { color: var(--bad); border-color: var(--bad); }
  .group { display: flex; flex-direction: column; gap: 6px; padding: 10px 12px; background: var(--bg); border: 1px solid var(--border); border-radius: 8px; }
  .check { display: flex; align-items: center; gap: 8px; margin: 0; color: var(--text); font-size: 13px; cursor: pointer; }
  .check input { width: auto; }
  .hint { margin: 4px 0 0; font-size: 12px; color: var(--muted); }
  .hint.first { margin: -6px 0 0; }
  .hint.bad { color: var(--bad); }
  .size { padding: 10px 12px; border-radius: 8px; background: var(--panel-2); font-size: 13px; }
  .size .hint { margin: 4px 0 0; }
</style>
