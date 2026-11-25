<script>
  import { onMount } from 'svelte';
  import * as echarts from 'echarts/core';
  import { BarChart } from 'echarts/charts';
  import { GridComponent, TooltipComponent, MarkLineComponent } from 'echarts/components';
  import { CanvasRenderer } from 'echarts/renderers';
  import { histogram } from '../lib/sweep.js';
  import { pct, num } from '../lib/format.js';

  echarts.use([BarChart, GridComponent, TooltipComponent, MarkLineComponent, CanvasRenderer]);

  // result: the sweep's result document
  let { result } = $props();

  let which = $state('plateauDensity');   // plateauDensity | passDensity
  let element;
  let chart;

  const BINS = 20;
  const shuffled = $derived(result.shuffled[which]);
  const realValue = $derived(result.real[which]);
  const bins = $derived(histogram(shuffled.values, BINS));
  const label = $derived(which === 'plateauDensity' ? 'Plateau density' : 'Pass density');

  const axisText = '#8b95a5';

  function options() {
    const realBin = realValue === null ? null : Math.min(BINS - 1, Math.floor(realValue * BINS));
    return {
      animation: false,
      backgroundColor: 'transparent',
      textStyle: { color: axisText },
      grid: { left: 44, right: 16, top: 28, bottom: 44 },
      tooltip: {
        trigger: 'axis', axisPointer: { type: 'shadow' },
        backgroundColor: '#1b212b', borderColor: '#262d39', textStyle: { color: '#e6e9ef' },
        formatter: (items) => {
          const b = bins[items[0].dataIndex];
          return `${pct(b.from, 0)} to ${pct(b.to, 0)}<br/><b>${b.count}</b> of ${shuffled.values.length} shuffled copies`;
        },
      },
      xAxis: {
        type: 'category', data: bins.map((b) => pct(b.from, 0)), name: `${label} on a shuffled copy`, nameLocation: 'middle', nameGap: 28,
        nameTextStyle: { color: axisText }, axisLabel: { color: axisText, interval: 1 }, axisLine: { lineStyle: { color: '#262d39' } }, axisTick: { show: false },
      },
      yAxis: { type: 'value', minInterval: 1, name: 'copies', nameTextStyle: { color: axisText }, axisLabel: { color: axisText }, splitLine: { lineStyle: { color: '#222a35' } } },
      series: [{
        type: 'bar', data: bins.map((b) => b.count), barCategoryGap: '12%', itemStyle: { color: '#4f6d99' },
        markLine: realBin === null ? undefined : {
          symbol: 'none', silent: true, lineStyle: { color: '#e9d758', width: 2, type: 'solid' },
          label: { color: '#e9d758', formatter: `real ${pct(realValue, 1)}`, position: 'end' },
          data: [{ xAxis: realBin }],
        },
      }],
    };
  }

  onMount(() => {
    chart = echarts.init(element, null, { renderer: 'canvas' });
    const observer = new ResizeObserver(() => chart.resize());
    observer.observe(element);
    return () => {
      observer.disconnect();
      chart.dispose();
    };
  });

  $effect(() => {
    if (!chart) return;
    chart.setOption(options(), true);
  });
</script>

<div class="panel">
  <div class="top">
    <h2>The same grid on shuffled data</h2>
    <div class="seg">
      <button class:on={which === 'plateauDensity'} onclick={() => (which = 'plateauDensity')}>Plateau</button>
      <button class:on={which === 'passDensity'} onclick={() => (which = 'passDensity')}>Pass</button>
    </div>
  </div>
  <p class="muted note">
    Each bar counts shuffled copies of the data with that {label.toLowerCase()}. The yellow line is the real data
    ({pct(realValue, 1)}); {shuffled.atLeastReal} of {shuffled.values.length} copies reach it or more.
    Median {pct(shuffled.median, 1)}, 95th percentile {pct(shuffled.p95, 1)}.
  </p>
  <div class="chart" bind:this={element}></div>
</div>

<style>
  .top { display: flex; justify-content: space-between; align-items: center; }
  .note { margin: 6px 0 8px; font-size: 12px; }
  .chart { width: 100%; height: 260px; }
  .seg { display: flex; }
  .seg button { border-radius: 0; padding: 4px 12px; font-size: 12px; }
  .seg button:first-child { border-radius: 7px 0 0 7px; }
  .seg button:last-child { border-radius: 0 7px 7px 0; border-left: 0; }
  .seg button.on { background: var(--accent-strong); border-color: var(--accent-strong); color: #fff; }
</style>
