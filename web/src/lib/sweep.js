// Everything the density sweep screens need that is not drawing: checking the axes as you type (the same rules the server
// enforces, which checks again), the size of the grid, the request body, and turning a result into chart data.

export const MAX_AXIS_VALUES = 100000;
export const MAX_CELLS = 50000;
export const MAX_BACKTESTS = 2000000;
export const MAX_COPIES = 1000;

// The metric that decides whether a cell is "good". The names are the server's.
export const METRICS = [
  { value: 'mar', label: 'MAR', help: 'CAGR divided by max drawdown' },
  { value: 'cagr', label: 'CAGR', help: 'Compound annual growth rate' },
  { value: 'sharpe', label: 'Sharpe', help: 'Return per unit of volatility' },
  { value: 'sortino', label: 'Sortino', help: 'Return per unit of downside volatility' },
  { value: 'total-return', label: 'Total return', help: 'Final equity compared with the starting cash' },
  { value: 'profit-factor', label: 'Profit factor', help: 'Total won divided by total lost' },
  { value: 'expectancy', label: 'Expectancy', help: 'Average profit per closed trade' },
];

const num = (text) => (String(text ?? '').trim() === '' ? NaN : Number(text));

// The values of an axis: from, from + step, ... up to and including `to` when the step lands on it. Empty for a bad axis.
export function axisValues({ from, to, step }) {
  const a = num(from), b = num(to), s = num(step);
  if (![a, b, s].every(Number.isFinite) || s <= 0 || b < a) return [];
  const count = Math.floor((b - a) / s + 1e-9) + 1;
  if (count > MAX_AXIS_VALUES) return [];
  const values = new Array(count);
  for (let i = 0; i < count; i++) values[i] = a + i * s;
  return values;
}

// What is wrong with an axis for this parameter (a ParamSpec from /api/strategies), or null.
export function axisError(spec, { from, to, step }) {
  const a = num(from), b = num(to), s = num(step);
  if (![a, b, s].every(Number.isFinite)) return 'Enter numbers for from, to and step';
  if (s <= 0) return 'The step must be above 0';
  if (b < a) return "'To' is below 'from'";
  if (a < spec.min || b > spec.max) return `Must stay between ${spec.min} and ${spec.max}`;
  if (spec.integer && (!Number.isInteger(a) || !Number.isInteger(s))) return 'This parameter is a whole number: from and step must be whole';
  if (Math.floor((b - a) / s + 1e-9) + 1 > MAX_AXIS_VALUES) return 'Too many values on this axis';
  return null;
}

const tidy = (x) => String(Number(x.toFixed(10)));

// A first axis for a parameter: from its minimum, about twenty values, inside its limits. A starting point to edit.
export function defaultAxis(spec) {
  const step = spec.integer ? 1 : (spec.max - spec.min) / 20;
  const to = Math.min(spec.max, spec.min + 19 * step);
  return { from: tidy(spec.min), to: tidy(to), step: tidy(step) };
}

// The number of cells and of backtests (cells x (1 + shuffled copies)); 0 when an axis or the copy count is not usable.
export function gridSize(axes, nullRuns) {
  let cells = 1;
  for (const axis of axes) cells *= axisValues(axis).length;
  const copies = Number(nullRuns);
  const backtests = cells > 0 && Number.isInteger(copies) && copies >= 1 ? cells * (1 + copies) : 0;
  return { cells, backtests };
}

// What is wrong with the size of the sweep (the server's limits), or null.
export function sweepError(grid, nullRuns) {
  const copies = Number(nullRuns);
  if (grid.cells === 0) return 'Fix the axes first';
  if (!Number.isInteger(copies)) return 'The number of shuffled copies must be a whole number';
  if (copies < 1 || copies > MAX_COPIES) return `Shuffled copies must be from 1 to ${MAX_COPIES}`;
  if (grid.cells > MAX_CELLS) return `Too many cells (the limit is ${MAX_CELLS.toLocaleString('en-US')})`;
  if (grid.backtests > MAX_BACKTESTS) return `Too many backtests (the limit is ${MAX_BACKTESTS.toLocaleString('en-US')})`;
  return null;
}

// The body of POST /api/sweeps from the form's values (all text) with numbers where numbers belong.
export function buildSweepRequest(form) {
  const params = {};
  for (const [name, text] of Object.entries(form.params)) params[name] = Number(text);
  return {
    strategy: form.strategy,
    dataset: form.dataset,
    params,
    axes: form.axes.map((a) => ({ param: a.param, from: Number(a.from), to: Number(a.to), step: Number(a.step) })),
    metric: form.metric,
    threshold: Number(form.threshold),
    radius: Number(form.radius),
    minTrades: Number(form.minTrades),
    nullRuns: Number(form.nullRuns),
    block: Number(form.block),
    cash: Number(form.cash),
    periodsPerYear: Number(form.periodsPerYear),
    note: form.note.trim(),
  };
}

// Ideas tried on each dataset, from the run summaries: every cell of every sweep (real data and each copy) and every run.
// A record from before the count existed is one idea.
export function ideasByDataset(runs) {
  const totals = {};
  for (const r of runs) {
    const dataset = r.spec?.dataset;
    if (!dataset) continue;
    totals[dataset] = (totals[dataset] ?? 0) + (r.ideas ?? 1);
  }
  return totals;
}

// One entry per cell of a sweep result, in the result's row-major order. The first axis is the rows, the second the
// columns. A cell passes only when it is valid and strictly above the threshold.
export function heatmapCells(result) {
  const [rows, cols] = result.axes.map((a) => a.values);
  const { values, valid, trades } = result.grid;
  const threshold = result.spec.threshold;
  return values.map((value, i) => {
    const row = Math.floor(i / cols.length);
    const col = i % cols.length;
    return {
      row, col,
      rowValue: rows[row], colValue: cols[col],
      value, valid: valid[i], trades: trades[i],
      pass: valid[i] && value !== null && value > threshold,
    };
  });
}

// Counts of values (0 to 1, e.g. densities) in `bins` equal bins; 1 falls in the last bin. Non-numbers are ignored.
export function histogram(values, bins) {
  const out = Array.from({ length: bins }, (_, i) => ({ from: i / bins, to: (i + 1) / bins, count: 0 }));
  for (const v of values) {
    if (typeof v !== 'number' || !Number.isFinite(v)) continue;
    out[Math.min(bins - 1, Math.max(0, Math.floor(v * bins)))].count++;
  }
  return out;
}
