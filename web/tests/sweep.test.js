import { describe, it, expect } from 'vitest';
import {
  axisValues, axisError, defaultAxis, gridSize, sweepError, buildSweepRequest,
  ideasByDataset, heatmapCells, histogram, METRICS,
} from '../src/lib/sweep.js';

const lookback = { name: 'lookback', default: 35, min: 1, max: 500, integer: true };
const fraction = { name: 'fraction', default: 1, min: 0, max: 1, integer: false };

// These mirror the rules the server enforces (so the form can say what is wrong as you type); the server checks again.
describe('axisValues', () => {
  it('includes both ends when the step lands on them', () => {
    const v = axisValues({ from: '5', to: '200', step: '5' });
    expect(v.length).toBe(40);
    expect(v[0]).toBe(5);
    expect(v[39]).toBe(200);
  });
  it('stops below the end when the step does not land on it', () => {
    expect(axisValues({ from: '1', to: '10', step: '4' })).toEqual([1, 5, 9]);
  });
  it('reaches the end with decimal steps', () => {
    const v = axisValues({ from: '0', to: '1', step: '0.1' });
    expect(v.length).toBe(11);
    expect(v[10]).toBeCloseTo(1, 12);
  });
  it('gives nothing for a bad axis', () => {
    for (const bad of [
      { from: '', to: '5', step: '1' }, { from: '1', to: 'x', step: '1' }, { from: '1', to: '5', step: '0' },
      { from: '1', to: '5', step: '-1' }, { from: '5', to: '1', step: '1' }, { from: '1', to: '5', step: '' },
    ]) expect(axisValues(bad)).toEqual([]);
  });
  it('refuses to build an enormous list', () => {
    expect(axisValues({ from: '0', to: '1000000000', step: '1' })).toEqual([]);
  });
});

describe('axisError', () => {
  it('accepts a good axis, including values on the limits', () => {
    expect(axisError(lookback, { from: '5', to: '200', step: '5' })).toBe(null);
    expect(axisError(lookback, { from: '1', to: '500', step: '1' })).toBe(null);
    expect(axisError(fraction, { from: '0', to: '1', step: '0.25' })).toBe(null);
  });
  it('says what is wrong', () => {
    expect(axisError(lookback, { from: '', to: '5', step: '1' })).toMatch(/number/i);
    expect(axisError(lookback, { from: '1', to: '5', step: '0' })).toMatch(/step/i);
    expect(axisError(lookback, { from: '9', to: '5', step: '1' })).toMatch(/from.*to|below/i);
    expect(axisError(lookback, { from: '0', to: '5', step: '1' })).toMatch(/1.*500/);
    expect(axisError(lookback, { from: '1', to: '501', step: '1' })).toMatch(/1.*500/);
    expect(axisError(lookback, { from: '1', to: '10', step: '0.5' })).toMatch(/whole/i);
    expect(axisError(lookback, { from: '1.5', to: '10', step: '1' })).toMatch(/whole/i);
    expect(axisError(lookback, { from: '0', to: '1000000000', step: '1' })).toMatch(/1.*500|many/i);
  });
  it('allows a decimal axis for a decimal parameter', () => {
    expect(axisError(fraction, { from: '0.1', to: '0.9', step: '0.1' })).toBe(null);
  });
});

describe('defaultAxis', () => {
  it('starts at the minimum with a step that gives about twenty values', () => {
    expect(defaultAxis(lookback)).toEqual({ from: '1', to: '20', step: '1' });
    const f = defaultAxis(fraction);
    expect(f.from).toBe('0');
    expect(axisValues(f).length).toBe(20);
    expect(Number(f.to)).toBeLessThanOrEqual(1);
  });
  it('stays inside the limits for a narrow parameter', () => {
    const narrow = { name: 'delay', default: 0, min: 0, max: 3, integer: true };
    const a = defaultAxis(narrow);
    expect(axisError(narrow, a)).toBe(null);
    expect(Number(a.to)).toBe(3);
  });
});

describe('gridSize', () => {
  it('is cells and cells times one plus the shuffled copies', () => {
    const g = gridSize([{ from: '5', to: '200', step: '5' }, { from: '1', to: '60', step: '1' }], '200');
    expect(g.cells).toBe(2400);
    expect(g.backtests).toBe(2400 * 201);
  });
  it('is zero for a bad axis or a bad number of copies', () => {
    expect(gridSize([{ from: '', to: '', step: '' }, { from: '1', to: '2', step: '1' }], '10').cells).toBe(0);
    expect(gridSize([{ from: '1', to: '2', step: '1' }, { from: '1', to: '2', step: '1' }], 'x').backtests).toBe(0);
  });
});

describe('sweepError', () => {
  const ok = { cells: 100, backtests: 20100 };
  it('is fine for a normal grid', () => expect(sweepError(ok, 200)).toBe(null));
  it('mirrors the server limits', () => {
    expect(sweepError({ cells: 50001, backtests: 60000 }, 1)).toMatch(/50,000/);
    expect(sweepError({ cells: 10000, backtests: 2010000 }, 200)).toMatch(/2,000,000/);
    expect(sweepError({ cells: 0, backtests: 0 }, 200)).toMatch(/axes/i);
    expect(sweepError(ok, 0)).toMatch(/1.*1000/);
    expect(sweepError(ok, 1001)).toMatch(/1.*1000/);
    expect(sweepError(ok, 1.5)).toMatch(/whole/i);
  });
});

describe('buildSweepRequest', () => {
  it('makes the body the API expects, with numbers not text', () => {
    const body = buildSweepRequest({
      strategy: 'inertia_breakout', dataset: 'btc.v.txt',
      axes: [
        { param: 'lookback', from: '5', to: '200', step: '5' },
        { param: 'allowance', from: '1', to: '60', step: '1' },
      ],
      params: { fraction: '1', quantity: '1' },
      metric: 'mar', threshold: '0', radius: '1', minTrades: '30', nullRuns: '200', block: '5',
      cash: '10000', periodsPerYear: '260', note: '  PASS if ...  ',
    });
    expect(body).toEqual({
      strategy: 'inertia_breakout', dataset: 'btc.v.txt',
      params: { fraction: 1, quantity: 1 },
      axes: [
        { param: 'lookback', from: 5, to: 200, step: 5 },
        { param: 'allowance', from: 1, to: 60, step: 1 },
      ],
      metric: 'mar', threshold: 0, radius: 1, minTrades: 30, nullRuns: 200, block: 5,
      cash: 10000, periodsPerYear: 260, note: 'PASS if ...',
    });
  });
});

describe('METRICS', () => {
  it('lists the seven the server knows', () => {
    expect(METRICS.map((m) => m.value)).toEqual(
      ['mar', 'cagr', 'sharpe', 'sortino', 'total-return', 'profit-factor', 'expectancy'],
    );
  });
});

describe('ideasByDataset', () => {
  it('adds up the ideas tried on each dataset, counting a run as one', () => {
    const runs = [
      { spec: { dataset: 'btc.v.txt' }, ideas: 482400, kind: 'sweep' },
      { spec: { dataset: 'btc.v.txt' }, ideas: 1, kind: 'run' },
      { spec: { dataset: 'xrp.v.txt' }, ideas: 1100, kind: 'sweep' },
      { spec: { dataset: 'xrp.v.txt' } },                       // an old record with no count is one idea
    ];
    expect(ideasByDataset(runs)).toEqual({ 'btc.v.txt': 482401, 'xrp.v.txt': 1101 });
  });
  it('is empty with no runs', () => expect(ideasByDataset([])).toEqual({}));
});

describe('heatmapCells', () => {
  // a 2 x 3 grid: rows are the first axis, columns the second; the data is row-major
  const result = {
    axes: [{ param: 'a', values: [10, 20] }, { param: 'b', values: [1, 2, 3] }],
    grid: { values: [0.1, 0.2, null, 0.4, 0.5, 0.6], valid: [true, true, false, true, true, true], trades: [1, 2, 3, 4, 5, 6] },
    spec: { threshold: 0.35 },
  };
  it('gives [column, row, value] for every cell in row-major order', () => {
    const cells = heatmapCells(result);
    expect(cells.length).toBe(6);
    expect(cells[0]).toMatchObject({ col: 0, row: 0, value: 0.1, valid: true, pass: false, trades: 1 });
    expect(cells[2]).toMatchObject({ col: 2, row: 0, value: null, valid: false, pass: false });
    expect(cells[3]).toMatchObject({ col: 0, row: 1, value: 0.4, pass: true });
    expect(cells[5]).toMatchObject({ col: 2, row: 1, value: 0.6, pass: true });
  });
  it('a cell passes only when valid and strictly above the threshold', () => {
    const r = { ...result, spec: { threshold: 0.4 }, grid: { ...result.grid } };
    expect(heatmapCells(r)[3].pass).toBe(false);            // equal is not above
    expect(heatmapCells(r)[4].pass).toBe(true);
    const invalidHigh = { ...result, grid: { values: [9, 9, 9, 9, 9, 9], valid: [false, true, true, true, true, true], trades: [0, 0, 0, 0, 0, 0] } };
    expect(heatmapCells(invalidHigh)[0].pass).toBe(false);  // invalid never passes
  });
  it('carries the axis values for the tooltip', () => {
    const c = heatmapCells(result)[4];
    expect(c.rowValue).toBe(20);
    expect(c.colValue).toBe(2);
  });
});

describe('histogram', () => {
  it('counts values into equal bins from 0 to 1, with 1 in the last bin', () => {
    const h = histogram([0, 0.04, 0.05, 0.5, 0.99, 1], 10);
    expect(h.length).toBe(10);
    expect(h[0]).toMatchObject({ from: 0, to: 0.1, count: 3 });
    expect(h[5].count).toBe(1);
    expect(h[9].count).toBe(2);
    expect(h.reduce((s, b) => s + b.count, 0)).toBe(6);
  });
  it('is all zero for no values', () => {
    expect(histogram([], 5).every((b) => b.count === 0)).toBe(true);
  });
  it('ignores values that are not numbers', () => {
    expect(histogram([0.5, null, NaN], 4).reduce((s, b) => s + b.count, 0)).toBe(1);
  });
});
