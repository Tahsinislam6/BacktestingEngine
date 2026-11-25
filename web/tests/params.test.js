import { describe, it, expect } from 'vitest';
import { defaultParams, paramError, buildRequest } from '../src/lib/params.js';

const schema = {
  name: 'demo',
  params: [
    { name: 'lookback', default: 20, min: 2, max: 200, integer: true, description: '' },
    { name: 'fraction', default: 0.5, min: 0, max: 1, integer: false, description: '' },
  ],
};

describe('params', () => {
  it('defaultParams gives every default, as text for the inputs', () => {
    expect(defaultParams(schema)).toEqual({ lookback: '20', fraction: '0.5' });
    expect(defaultParams({ name: 'x', params: [] })).toEqual({});
    expect(defaultParams(null)).toEqual({});
  });
  it('paramError accepts values inside the limits, including the limits', () => {
    const [lookback, fraction] = schema.params;
    expect(paramError(lookback, '20')).toBe(null);
    expect(paramError(lookback, '2')).toBe(null);
    expect(paramError(lookback, '200')).toBe(null);
    expect(paramError(fraction, '0')).toBe(null);
    expect(paramError(fraction, '1')).toBe(null);
    expect(paramError(fraction, '0.25')).toBe(null);
  });
  it('paramError explains what is wrong', () => {
    const [lookback, fraction] = schema.params;
    expect(paramError(lookback, '')).toMatch(/number/i);
    expect(paramError(lookback, 'abc')).toMatch(/number/i);
    expect(paramError(lookback, '1')).toMatch(/2.*200/);
    expect(paramError(lookback, '201')).toMatch(/2.*200/);
    expect(paramError(lookback, '20.5')).toMatch(/whole/i);
    expect(paramError(fraction, '-0.1')).toMatch(/0.*1/);
    expect(paramError(fraction, 'Infinity')).toMatch(/number/i);
  });
  it('buildRequest makes the body the API expects, with numbers not text', () => {
    const body = buildRequest({
      strategy: 'demo',
      dataset: 'x.txt',
      params: { lookback: '30', fraction: '0.5' },
      cash: '5000',
      periodsPerYear: '365',
    });
    expect(body).toEqual({
      strategy: 'demo',
      dataset: 'x.txt',
      params: { lookback: 30, fraction: 0.5 },
      cash: 5000,
      periodsPerYear: 365,
    });
  });
});
