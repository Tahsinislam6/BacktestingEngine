<script>
  import { onMount } from 'svelte';
  import * as echarts from 'echarts/core';
  import { LineChart } from 'echarts/charts';
  import { GridComponent, TooltipComponent, DataZoomComponent, LegendComponent } from 'echarts/components';
  import { CanvasRenderer } from 'echarts/renderers';
  import { stackedSeries } from '../lib/portfolio.js';
  import { drawdownSeries, toPoints } from '../lib/series.js';
  import { num, pct } from '../lib/format.js';

  echarts.use([LineChart, GridComponent, TooltipComponent, DataZoomComponent, LegendComponent, CanvasRenderer]);

  // result: the portfolio's result document
  let { result } = $props();

  let element;
  let chart;

  const axisText = '#8b95a5';
  const gridLine = '#222a35';
  // One colour per sleeve, cycling; the combined drawdown is red as elsewhere.
  const palette = ['#6ea8fe', '#4cc38a', '#e5b45a', '#c792ea', '#5ad1d1', '#f78fb3', '#9aa7b8', '#b8d46a'];

  function options() {
    const sleeves = stackedSeries(result);
    const drawdown = toPoints(result.timestamps, drawdownSeries(result.combined.equity));
    return {
      animation: false,
      backgroundColor: 'transparent',
      textStyle: { color: axisText },
      color: palette,
      legend: { top: 0, textStyle: { color: axisText }, icon: 'roundRect', itemWidth: 10, itemHeight: 10 },
      grid: [
        { left: 72, right: 20, top: 34, height: '52%' },
        { left: 72, right: 20, top: '74%', height: '12%' },
      ],
      tooltip: {
        trigger: 'axis',
        axisPointer: { type: 'line', link: [{ xAxisIndex: 'all' }] },
        backgroundColor: '#1b212b', borderColor: '#262d39', textStyle: { color: '#e6e9ef' },
        formatter: (items) => {
          const date = new Date(items[0].value[0]).toISOString().slice(0, 10);
          const parts = items.filter((i) => i.seriesIndex < sleeves.length);
          const total = parts.reduce((sum, i) => sum + i.value[1], 0);
          const rows = parts.map((i) => `${i.marker} ${i.seriesName}: <b>${num(i.value[1])}</b>`);
          const dd = items.find((i) => i.seriesIndex === sleeves.length);
          return [date, ...rows, `Total: <b>${num(total)}</b>`, dd ? `Drawdown: <b>${pct(dd.value[1])}</b>` : ''].filter(Boolean).join('<br/>');
        },
      },
      xAxis: [
        { type: 'time', gridIndex: 0, axisLabel: { show: false }, axisLine: { lineStyle: { color: gridLine } }, splitLine: { show: false }, axisTick: { show: false } },
        { type: 'time', gridIndex: 1, axisLabel: { color: axisText }, axisLine: { lineStyle: { color: gridLine } }, splitLine: { show: false } },
      ],
      yAxis: [
        { type: 'value', gridIndex: 0, axisLabel: { color: axisText, formatter: (v) => num(v, 0) }, splitLine: { lineStyle: { color: gridLine } } },
        { type: 'value', gridIndex: 1, max: 0, axisLabel: { color: axisText, formatter: (v) => pct(v, 0) }, splitLine: { lineStyle: { color: gridLine } }, splitNumber: 2 },
      ],
      dataZoom: [
        { type: 'inside', xAxisIndex: [0, 1] },
        { type: 'slider', xAxisIndex: [0, 1], height: 18, bottom: 6, borderColor: '#262d39', backgroundColor: '#151a22', fillerColor: 'rgba(110,168,254,0.15)', textStyle: { color: axisText } },
      ],
      series: [
        ...sleeves.map((s) => ({
          name: s.name, type: 'line', stack: 'total', xAxisIndex: 0, yAxisIndex: 0, data: s.data, showSymbol: false,
          sampling: 'lttb', lineStyle: { width: 1 }, areaStyle: { opacity: 0.55 },
        })),
        {
          name: 'Drawdown', type: 'line', xAxisIndex: 1, yAxisIndex: 1, data: drawdown, showSymbol: false, sampling: 'lttb',
          lineStyle: { width: 1, color: '#ef6b63' }, itemStyle: { color: '#ef6b63' }, areaStyle: { color: 'rgba(239,107,99,0.25)' },
        },
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
    chart.setOption(options(), true);
  });
</script>

<div class="panel">
  <h2>Equity by sleeve (stacked) and portfolio drawdown</h2>
  <div class="chart" bind:this={element}></div>
</div>

<style>
  h2 { margin-bottom: 8px; }
  .chart { width: 100%; height: 460px; }
</style>
