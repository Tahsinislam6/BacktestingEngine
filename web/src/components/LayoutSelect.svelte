<script>
  // Picks how a data file is read: a layout name from the server (auto, simple, stooq, and your own from layouts.json).
  let { id, layouts, value = $bindable('auto'), showHint = true } = $props();

  const description = $derived(layouts.find((l) => l.name === value)?.description ?? '');
</script>

<select {id} bind:value>
  {#each layouts as l}<option value={l.name}>{l.name}</option>{/each}
</select>
{#if showHint && description}<p class="hint">{description}</p>{/if}
