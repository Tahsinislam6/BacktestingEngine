<script>
  import { onMount } from 'svelte';
  import { getStrategies, getDatasets, getRuns, getRun, postRun } from './lib/api.js';
  import RunForm from './components/RunForm.svelte';
  import History from './components/History.svelte';
  import RunView from './components/RunView.svelte';

  let strategies = $state([]);
  let datasets = $state([]);
  let runs = $state([]);
  let selected = $state(null);
  let loadError = $state('');
  let ready = $state(false);

  async function refreshRuns() {
    runs = await getRuns();
  }

  async function select(id) {
    try {
      selected = await getRun(id);
    } catch (e) {
      loadError = e.message;
    }
  }

  // Called by the form; the form shows any error it throws.
  async function submit(request) {
    const id = await postRun(request);
    await refreshRuns();
    await select(id);
  }

  onMount(async () => {
    try {
      [strategies, datasets, runs] = await Promise.all([getStrategies(), getDatasets(), getRuns()]);
      if (runs.length > 0) await select(runs[0].id);
    } catch (e) {
      loadError = e.message;
    }
    ready = true;
  });

  // While the selected run is queued or running, ask for it again until it is finished.
  const activeId = $derived(
    selected && (selected.status === 'queued' || selected.status === 'running') ? selected.id : null,
  );

  $effect(() => {
    const id = activeId;
    if (!id) return;
    const timer = setInterval(async () => {
      try {
        const latest = await getRun(id);
        if (selected?.id === id) selected = latest;
        if (latest.status === 'done' || latest.status === 'failed') await refreshRuns();
      } catch {
        // a missed poll is not worth showing; the next one tries again
      }
    }, 600);
    return () => clearInterval(timer);
  });
</script>

<div class="shell">
  <header>
    <h1>Backtesting</h1>
    <span class="muted">{runs.length} {runs.length === 1 ? 'run' : 'runs'} logged</span>
  </header>

  {#if loadError}
    <div class="error-box banner">{loadError}</div>
  {/if}

  <main>
    <aside>
      <RunForm {strategies} {datasets} onsubmit={submit} />
      <History {runs} selectedId={selected?.id} onselect={select} />
    </aside>
    <section>
      {#if selected}
        <RunView run={selected} />
      {:else if ready && !loadError}
        <div class="panel empty">
          <h3>No run selected</h3>
          <p class="muted">Pick a dataset and a strategy on the left and press <b>Run backtest</b>.</p>
        </div>
      {/if}
    </section>
  </main>
</div>

<style>
  .shell { max-width: 1400px; margin: 0 auto; padding: 20px 24px 48px; }
  header { display: flex; align-items: baseline; gap: 14px; margin-bottom: 18px; }
  header h1 { font-size: 20px; }
  .banner { margin-bottom: 16px; }
  main { display: grid; grid-template-columns: 340px minmax(0, 1fr); gap: 20px; align-items: start; }
  aside { display: flex; flex-direction: column; gap: 20px; position: sticky; top: 16px; }
  section { min-width: 0; }
  .empty { text-align: center; padding: 64px 24px; }
  .empty p { margin: 8px 0 0; }
  @media (max-width: 900px) {
    main { grid-template-columns: 1fr; }
    aside { position: static; }
  }
</style>
