<script>
  import { defaultParams, paramError } from '../lib/params.js';
  import { METRICS, axisError, defaultAxis, gridSize, sweepError, buildSweepRequest } from '../lib/sweep.js';
  import { num } from '../lib/format.js';

  let { strategies, datasets, ideasTried, onsubmit } = $props();

  let strategyName = $state('');
  let dataset = $state('');
  let params = $state({});
  let axes = $state([
    { param: '', from: '', to: '', step: '' },
    { param: '', from: '', to: '', step: '' },
  ]);

  // The test settings start at the values of the research program density_sweep. Nothing is hidden: change them before
  // running; they are not picked for you.
  let metric = $state('mar');
  let threshold = $state('0');
  let radius = $state('1');
  let minTrades = $state('30');
  let nullRuns = $state('200');
  let block = $state('5');
  let cash = $state('10000');
  let periodsPerYear = $state('260');
  let note = $state('');

  let busy = $state(false);
  let error = $state('');

  const strategy = $derived(strategies.find((s) => s.name === strategyName));
  const specOf = (name) => strategy?.params.find((p) => p.name === name);

  $effect(() => {
    if (!strategyName && strategies.length > 0) strategyName = strategies[0].name;
  });
  $effect(() => {
    if (!dataset && datasets.length > 0) dataset = datasets[0];
  });

  // A new strategy: its first two parameters become the axes (each with a starting range) and the rest are fixed values.
  $effect(() => {
    const s = strategy;
    if (!s) return;
    const first = s.params[0], second = s.params[1];
    axes = [first, second].map((p) => (p ? { param: p.name, ...defaultAxis(p) } : { param: '', from: '', to: '', step: '' }));
  });

  const sweptNames = $derived(axes.map((a) => a.param));
  const fixedSpecs = $derived((strategy?.params ?? []).filter((p) => !sweptNames.includes(p.name)));

  // Fixed values: reset to the defaults whenever the set of fixed parameters changes.
  $effect(() => {
    params = defaultParams({ params: fixedSpecs });
  });

  // Choosing a parameter for an axis gives it a starting range; the other axis cannot use the same parameter.
  function chooseParam(index, name) {
    const spec = specOf(name);
    if (!spec) return;
    const other = 1 - index;
    const next = axes.map((a) => ({ ...a }));
    next[index] = { param: name, ...defaultAxis(spec) };
    if (next[other].param === name) {
      const swap = strategy.params.find((p) => p.name !== name && p.name !== next[index].param);
      next[other] = swap ? { param: swap.name, ...defaultAxis(swap) } : { param: '', from: '', to: '', step: '' };
    }
    axes = next;
  }

  const axisErrors = $derived(axes.map((a) => (specOf(a.param) ? axisError(specOf(a.param), a) : 'Choose a parameter')));
  const fixedErrors = $derived(Object.fromEntries(fixedSpecs.map((p) => [p.name, paramError(p, params[p.name])])));
  const grid = $derived(gridSize(axes, nullRuns));
  const sizeError = $derived(axisErrors.every((e) => e === null) ? sweepError(grid, nullRuns) : null);

  const intError = (text, min) => (Number.isInteger(Number(text)) && String(text).trim() !== '' && Number(text) >= min ? null : `Whole number, ${min} or more`);
  const radiusError = $derived(intError(radius, 0) ?? (Number(radius) > 20 ? 'At most 20' : null));
  const minTradesError = $derived(intError(minTrades, 0));
  const blockError = $derived(intError(block, 1) ?? (Number(block) > 1000 ? 'At most 1000' : null));
  const thresholdError = $derived(String(threshold).trim() !== '' && Number.isFinite(Number(threshold)) ? null : 'Enter a number');
  const cashError = $derived(Number(cash) > 0 && Number.isFinite(Number(cash)) ? null : 'Must be above 0');
  const periodsError = $derived(Number(periodsPerYear) > 0 && Number.isFinite(Number(periodsPerYear)) ? null : 'Must be above 0');
  const noteMissing = $derived(note.trim() === '');

  const valid = $derived(
    !!strategy && !!dataset && axisErrors.every((e) => e === null) && Object.values(fixedErrors).every((e) => e === null) &&
      !sizeError && !radiusError && !minTradesError && !blockError && !thresholdError && !cashError && !periodsError && !noteMissing,
  );

  const alreadyTried = $derived(ideasTried?.[dataset] ?? 0);

  async function run(event) {
    event.preventDefault();
    if (!valid || busy) return;
    busy = true;
    error = '';
    try {
      await onsubmit(buildSweepRequest({
        strategy: strategyName, dataset, params, axes, metric, threshold, radius, minTrades, nullRuns, block, cash, periodsPerYear, note,
      }));
    } catch (e) {
      error = e.message;
    } finally {
      busy = false;
    }
  }
</script>

<form class="panel" onsubmit={run}>
  <h2>Density sweep</h2>

  <div class="row">
    <div class="field">
      <label for="s-dataset">Dataset</label>
      <select id="s-dataset" bind:value={dataset}>{#each datasets as name}<option value={name}>{name}</option>{/each}</select>
    </div>
    <div class="field">
      <label for="s-strategy">Strategy</label>
      <select id="s-strategy" bind:value={strategyName}>{#each strategies as s}<option value={s.name}>{s.name}</option>{/each}</select>
    </div>
  </div>

  {#each axes as axis, i}
    <div class="group">
      <span class="group-title">{i === 0 ? 'Rows' : 'Columns'} (axis {i + 1})</span>
      <div class="field">
        <label for={'s-axis-' + i}>Parameter</label>
        <select id={'s-axis-' + i} value={axis.param} onchange={(e) => chooseParam(i, e.currentTarget.value)}>
          {#each strategy?.params ?? [] as p}<option value={p.name}>{p.name}</option>{/each}
        </select>
      </div>
      <div class="triple">
        <div class="field"><label for={'s-from-' + i}>From</label><input id={'s-from-' + i} class="mono" inputmode="decimal" bind:value={axis.from} aria-invalid={axisErrors[i] ? 'true' : 'false'} /></div>
        <div class="field"><label for={'s-to-' + i}>To</label><input id={'s-to-' + i} class="mono" inputmode="decimal" bind:value={axis.to} aria-invalid={axisErrors[i] ? 'true' : 'false'} /></div>
        <div class="field"><label for={'s-step-' + i}>Step</label><input id={'s-step-' + i} class="mono" inputmode="decimal" bind:value={axis.step} aria-invalid={axisErrors[i] ? 'true' : 'false'} /></div>
      </div>
      {#if axisErrors[i]}
        <p class="hint bad">{axisErrors[i]}</p>
      {:else if specOf(axis.param)}
        <p class="hint">{specOf(axis.param).description} · {specOf(axis.param).min} to {specOf(axis.param).max}{specOf(axis.param).integer ? ', whole' : ''}</p>
      {/if}
    </div>
  {/each}

  {#if fixedSpecs.length}
    <div class="group">
      <span class="group-title">Fixed parameters</span>
      {#each fixedSpecs as p (p.name)}
        <div class="field">
          <label for={'s-fixed-' + p.name}>{p.name}</label>
          <input id={'s-fixed-' + p.name} class="mono" inputmode="decimal" bind:value={params[p.name]} aria-invalid={fixedErrors[p.name] ? 'true' : 'false'} />
          {#if fixedErrors[p.name]}<p class="hint bad">{fixedErrors[p.name]}</p>{:else}<p class="hint">{p.description} · default {p.default}</p>{/if}
        </div>
      {/each}
    </div>
  {/if}

  <div class="group">
    <span class="group-title">What counts as good</span>
    <p class="hint first">Starts at the settings of density_sweep. Change them now; they are not picked for you.</p>
    <div class="field">
      <label for="s-metric">Metric</label>
      <select id="s-metric" bind:value={metric}>{#each METRICS as m}<option value={m.value}>{m.label}</option>{/each}</select>
    </div>
    <div class="row">
      <div class="field"><label for="s-threshold">Passes when above</label><input id="s-threshold" class="mono" inputmode="decimal" bind:value={threshold} aria-invalid={thresholdError ? 'true' : 'false'} />{#if thresholdError}<p class="hint bad">{thresholdError}</p>{/if}</div>
      <div class="field"><label for="s-minTrades">Min closed trades</label><input id="s-minTrades" class="mono" inputmode="numeric" bind:value={minTrades} aria-invalid={minTradesError ? 'true' : 'false'} />{#if minTradesError}<p class="hint bad">{minTradesError}</p>{/if}</div>
    </div>
    <div class="field">
      <label for="s-radius">Plateau radius</label>
      <input id="s-radius" class="mono" inputmode="numeric" bind:value={radius} aria-invalid={radiusError ? 'true' : 'false'} />
      {#if radiusError}<p class="hint bad">{radiusError}</p>{:else}<p class="hint">1 = a cell and its 8 neighbours must all pass</p>{/if}
    </div>
  </div>

  <div class="group">
    <span class="group-title">Luck baseline</span>
    <div class="row">
      <div class="field"><label for="s-null">Shuffled copies</label><input id="s-null" class="mono" inputmode="numeric" bind:value={nullRuns} /></div>
      <div class="field"><label for="s-block">Block (bars)</label><input id="s-block" class="mono" inputmode="numeric" bind:value={block} aria-invalid={blockError ? 'true' : 'false'} />{#if blockError}<p class="hint bad">{blockError}</p>{/if}</div>
    </div>
    <p class="hint">Block 1 destroys every dependency. Seeds are 1 to the number of copies, so a run can be repeated exactly.</p>
  </div>

  <div class="row">
    <div class="field"><label for="s-cash">Starting cash</label><input id="s-cash" class="mono" inputmode="decimal" bind:value={cash} aria-invalid={cashError ? 'true' : 'false'} />{#if cashError}<p class="hint bad">{cashError}</p>{/if}</div>
    <div class="field"><label for="s-ppy">Bars per year</label><input id="s-ppy" class="mono" inputmode="decimal" bind:value={periodsPerYear} aria-invalid={periodsError ? 'true' : 'false'} />{#if periodsError}<p class="hint bad">{periodsError}</p>{/if}</div>
  </div>

  <div class="field">
    <label for="s-note">Your pass rule (required)</label>
    <textarea
      id="s-note" rows="4" bind:value={note} aria-invalid={noteMissing ? 'true' : 'false'}
      placeholder="Write this BEFORE running: what you expect and what counts as a pass. For example: PASS if the real plateau density is above the shuffled 95th percentile AND at most 10 of 200 copies are at or above it."
    ></textarea>
    {#if noteMissing}<p class="hint">The sweep is refused without it, and it is saved with the result.</p>{/if}
  </div>

  <div class="size" class:bad={sizeError}>
    {#if grid.cells > 0}
      <b class="mono">{num(grid.cells, 0)}</b> cells × (1 + {Number(nullRuns) || '?'}) = <b class="mono">{num(grid.backtests, 0)}</b> backtests
      <p class="hint">Every one is an idea tried on {dataset || 'this data'}{alreadyTried ? `; ${num(alreadyTried, 0)} already tried there` : ''}.</p>
    {/if}
    {#if sizeError}<p class="hint bad">{sizeError}</p>{/if}
  </div>

  {#if error}<div class="error-box">{error}</div>{/if}

  <button class="primary" type="submit" disabled={!valid || busy}>{busy ? 'Starting…' : 'Run sweep once'}</button>
</form>

<style>
  form { display: flex; flex-direction: column; gap: 12px; }
  .field { display: flex; flex-direction: column; }
  .row { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
  .triple { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 8px; }
  .group { display: flex; flex-direction: column; gap: 10px; padding: 10px 12px; background: var(--bg); border: 1px solid var(--border); border-radius: 8px; }
  .group-title { font-size: 11px; text-transform: uppercase; letter-spacing: 0.06em; color: var(--muted); }
  .hint { margin: 4px 0 0; font-size: 12px; color: var(--muted); }
  .hint.first { margin: -4px 0 0; }
  .hint.bad { color: var(--bad); }
  textarea { width: 100%; resize: vertical; background: var(--panel-2); color: var(--text); border: 1px solid var(--border); border-radius: 7px; padding: 8px 10px; font: inherit; outline: none; }
  textarea:focus { border-color: var(--accent); }
  textarea[aria-invalid='true'] { border-color: var(--warn); }
  .size { padding: 10px 12px; border-radius: 8px; background: var(--panel-2); font-size: 13px; }
  .size.bad { border: 1px solid rgba(239, 107, 99, 0.4); }
  .size .hint { margin: 2px 0 0; }
</style>
