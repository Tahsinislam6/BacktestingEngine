<script>
  import { onMount } from 'svelte';
  import * as echarts from 'echarts/core';
  import { HeatmapChart } from 'echarts/charts';
  import { GridComponent, TooltipComponent, VisualMapComponent } from 'echarts/components';
  import { CanvasRenderer } from 'echarts/renderers';
  import { heatmapCells } from '../lib/sweep.js';
  import { num } from '../lib/format.js';

  echarts.use([HeatmapChart, GridComponent, TooltipComponent, VisualMapComponent, CanvasRenderer]);

  // result: the sweep's result document
  let { result } = $props();

  let element;
  let chart;

  const axisText = '#8b95a5';
  const rowName = $derived(result.axes[0].param);
  const colName = $derived(result.axes[1].param);

  // Seeing too many labels is noise: show about twenty per axis.
  const labelEvery = (n) => Math.max(0, Math.ceil(n / 20) - 1);

  function options() {
    const cells = heatmapCells(result);
    const validValues = cells.filter((c) => c.valid && c.value !== null).map((c) => c.value);
    const lo = validValues.length ? Math.min(...validValues) : 0;
    const hi = validValues.length ? Math.max(...validValues) : 1;
    const threshold = result.spec.threshold;
    const metric = result.spec.metric;

    const valid = cells
      .filter((c) => c.valid && c.value !== null)
      .map((c) => ({
        value: [c.col, c.row, c.value],
        // a cell that passes the rule gets a light outline
        itemStyle: c.pass ? { borderColor: '#ffffff', borderWidth: 1.5 } : { borderColor: 'transparent', borderWidth: 0 },
      }));
    const invalid = cells
      .filter((c) => !(c.valid && c.value !== null))
      .map((c) => ({ value: [c.col, c.row, 0] }));

    return {
      animation: false,
      backgroundColor: 'transparent',
      textStyle: { color: axisText },
      grid: { left: 74, right: 96, top: 12, bottom: 56 },
      tooltip: {
        backgroundColor: '#1b212b',
        borderColor: '#262d39',
        textStyle: { color: '#e6e9ef' },
        formatter: (p) => {
          const [col, row] = p.value;
          const c = cells[row * result.axes[1].values.length + col];
          const status = !c.valid ? 'invalid (too few trades)' : c.pass ? `passes (above ${threshold})` : `does not pass (not above ${threshold})`;
          return [
            `${rowName} = <b>${c.rowValue}</b>, ${colName} = <b>${c.colValue}</b>`,
            `${metric}: <b>${c.value === null ? '—' : num(c.value, 4)}</b>`,
            `closed trades: ${c.trades}`,
            status,
          ].join('<br/>');
        },
      },
      xAxis: {
        type: 'category', data: result.axes[1].values.map(String), name: colName, nameLocation: 'middle', nameGap: 34,
        axisLabel: { color: axisText, interval: labelEvery(result.axes[1].values.length) }, axisLine: { lineStyle: { color: '#262d39' } },
        splitArea: { show: false }, axisTick: { show: false }, nameTextStyle: { color: axisText },
      },
      yAxis: {
        type: 'category', data: result.axes[0].values.map(String), name: rowName, nameLocation: 'middle', nameGap: 52, inverse: true,
        axisLabel: { color: axisText, interval: labelEvery(result.axes[0].values.length) }, axisLine: { lineStyle: { color: '#262d39' } },
        splitArea: { show: false }, axisTick: { show: false }, nameTextStyle: { color: axisText },
      },
      visualMap: {
        type: 'continuous', seriesIndex: 0, min: lo, max: hi, orient: 'vertical', right: 6, top: 12, bottom: 56, itemWidth: 12,
        precision: 3, calculable: false, text: [metric, ''], textStyle: { color: axisText },
        inRange: { color: ['#2b2d6e', '#2a6f97', '#3fb68b', '#e9d758'] },
      },
      series: [
        { type: 'heatmap', data: valid, progressive: 0, emphasis: { itemStyle: { borderColor: '#fff', borderWidth: 1 } } },
        { type: 'heatmap', data: invalid, progressive: 0, itemStyle: { color: '#232a35' }, emphasis: { itemStyle: { borderColor: '#fff', borderWidth: 1 } } },
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
  <div class="top">
    <h2>Grid: {result.spec.metric} per cell</h2>
    <span class="legend muted"><i class="outline"></i> above {result.spec.threshold}<i class="gray"></i> invalid</span>
  </div>
  <div class="chart" bind:this={element}></div>
</div>

<style>
  .top { display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px; }
  .legend { display: flex; align-items: center; gap: 6px; font-size: 12px; }
  .legend i { display: inline-block; width: 12px; height: 12px; border-radius: 2px; margin-left: 10px; }
  .legend .outline { border: 1.5px solid #fff; background: transparent; margin-left: 0; }
  .legend .gray { background: #232a35; border: 1px solid var(--border); }
  .chart { width: 100%; height: 420px; }
</style>
