<script>
  import { onMount } from 'svelte';
  import * as echarts from 'echarts/core';
  import { LineChart } from 'echarts/charts';
  import { GridComponent, TooltipComponent, DataZoomComponent } from 'echarts/components';
  import { CanvasRenderer } from 'echarts/renderers';
  import { drawdownSeries, toPoints } from '../lib/series.js';
  import { num, pct } from '../lib/format.js';

  echarts.use([LineChart, GridComponent, TooltipComponent, DataZoomComponent, CanvasRenderer]);

  let { timestamps, equity } = $props();

  let element;
  let chart;
  let logScale = $state(false);

  const canLog = $derived(equity.length > 0 && Math.min(...equity) > 0);

  const axisText = '#8b95a5';
  const gridLine = '#222a35';

  function options(points, drawdown, log) {
    return {
      animation: false,
      backgroundColor: 'transparent',
      textStyle: { color: axisText },
      grid: [
        { left: 64, right: 20, top: 16, height: '56%' },
        { left: 64, right: 20, top: '73%', height: '14%' },
      ],
      tooltip: {
        trigger: 'axis',
        axisPointer: { type: 'line', link: [{ xAxisIndex: 'all' }] },
        backgroundColor: '#1b212b',
        borderColor: '#262d39',
        textStyle: { color: '#e6e9ef' },
        formatter: (items) => {
          const date = new Date(items[0].value[0]).toISOString().slice(0, 10);
          const rows = items.map((i) => `${i.marker} ${i.seriesName}: <b>${i.seriesIndex === 0 ? num(i.value[1]) : pct(i.value[1])}</b>`);
          return [date, ...rows].join('<br/>');
        },
      },
      xAxis: [
        { type: 'time', gridIndex: 0, axisLabel: { show: false }, axisLine: { lineStyle: { color: gridLine } }, splitLine: { show: false }, axisTick: { show: false } },
        { type: 'time', gridIndex: 1, axisLabel: { color: axisText }, axisLine: { lineStyle: { color: gridLine } }, splitLine: { show: false } },
      ],
      yAxis: [
        {
          type: log ? 'log' : 'value', gridIndex: 0, scale: true,
          axisLabel: { color: axisText, formatter: (v) => (Math.abs(v) >= 1000 ? num(v, 0) : num(v, v < 10 ? 2 : 0)) },
          splitLine: { lineStyle: { color: gridLine } },
        },
        {
          type: 'value', gridIndex: 1, max: 0,
          axisLabel: { color: axisText, formatter: (v) => pct(v, 0) },
          splitLine: { lineStyle: { color: gridLine } }, splitNumber: 2,
        },
      ],
      dataZoom: [
        { type: 'inside', xAxisIndex: [0, 1] },
        { type: 'slider', xAxisIndex: [0, 1], height: 18, bottom: 6, borderColor: '#262d39', backgroundColor: '#151a22', fillerColor: 'rgba(110,168,254,0.15)', textStyle: { color: axisText } },
      ],
      series: [
        { name: 'Equity', type: 'line', xAxisIndex: 0, yAxisIndex: 0, data: points, showSymbol: false, sampling: 'lttb', lineStyle: { width: 1.6, color: '#6ea8fe' }, itemStyle: { color: '#6ea8fe' } },
        { name: 'Drawdown', type: 'line', xAxisIndex: 1, yAxisIndex: 1, data: drawdown, showSymbol: false, sampling: 'lttb', lineStyle: { width: 1, color: '#ef6b63' }, itemStyle: { color: '#ef6b63' }, areaStyle: { color: 'rgba(239,107,99,0.25)' } },
      ],
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
    const log = logScale && canLog;
    chart.setOption(
      options(toPoints(timestamps, equity), toPoints(timestamps, drawdownSeries(equity)), log),
      true,
    );
  });
</script>

<div class="panel">
  <div class="top">
    <h2>Equity and drawdown</h2>
    <label class="toggle" class:disabled={!canLog} title={canLog ? '' : 'The equity is not above zero everywhere'}>
      <input type="checkbox" bind:checked={logScale} disabled={!canLog} />
      Log scale
    </label>
  </div>
  <div class="chart" bind:this={element}></div>
</div>

<style>
  .top { display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px; }
  .toggle { display: flex; align-items: center; gap: 6px; margin: 0; cursor: pointer; user-select: none; }
  .toggle input { width: auto; }
  .toggle.disabled { opacity: 0.5; cursor: not-allowed; }
  .chart { width: 100%; height: 440px; }
</style>
