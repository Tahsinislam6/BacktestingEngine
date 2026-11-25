import { describe, it, expect } from 'vitest';
import { drawdownSeries, toPoints, maxOf } from '../src/lib/series.js';

describe('series', () => {
  it('drawdown is 0 at a new high and negative below the running peak', () => {
    const dd = drawdownSeries([100, 110, 99, 120, 60]);
    expect(dd[0]).toBe(0);
    expect(dd[1]).toBe(0);
    expect(dd[2]).toBeCloseTo(99 / 110 - 1, 12);
    expect(dd[3]).toBe(0);
    expect(dd[4]).toBeCloseTo(-0.5, 12);
  });
  it('drawdown of nothing is nothing, and a flat curve never draws down', () => {
    expect(drawdownSeries([])).toEqual([]);
    expect(drawdownSeries([5, 5, 5])).toEqual([0, 0, 0]);
  });
  it('drawdown tolerates a zero or negative first value without NaN', () => {
    const dd = drawdownSeries([0, 0, 10]);
    expect(dd.every(Number.isFinite)).toBe(true);
  });
  it('toPoints pairs timestamps with values and stops at the shorter list', () => {
    expect(toPoints([1, 2, 3], [10, 20, 30])).toEqual([[1, 10], [2, 20], [3, 30]]);
    expect(toPoints([1, 2, 3], [10, 20])).toEqual([[1, 10], [2, 20]]);
    expect(toPoints([], [])).toEqual([]);
  });
  it('maxOf ignores nulls and gives null for nothing', () => {
    expect(maxOf([1, null, 5, 3])).toBe(5);
    expect(maxOf([])).toBe(null);
    expect(maxOf([null])).toBe(null);
  });
});
