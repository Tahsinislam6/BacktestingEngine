import { describe, it, expect } from 'vitest';
import {
  newSleeve, sleeveErrors, portfolioError, totalCash, buildPortfolioRequest, summaryRows, stackedSeries, tradeKey, MAX_SLEEVES,
} from '../src/lib/portfolio.js';
import { ideasByDataset } from '../src/lib/sweep.js';

const strategies = [
  { name: 'buy_and_hold', params: [{ name: 'fraction', default: 1, min: 0, max: 1, integer: false, description: '' }] },
  {
    name: 'inertia_breakout',
    params: [
      { name: 'lookback', default: 35, min: 1, max: 500, integer: true, description: '' },
      { name: 'allowance', default: 5, min: 1, max: 100, integer: true, description: '' },
    ],
  },
];
const datasets = ['btc.v.txt', 'eth.v.txt'];

describe('newSleeve', () => {
  it('starts from the first dataset and strategy with default parameters', () => {
    const s = newSleeve(strategies, datasets, []);
    expect(s.dataset).toBe('btc.v.txt');
    expect(s.strategy).toBe('buy_and_hold');
    expect(s.params).toEqual({ fraction: '1' });
    expect(s.name).toBe('BTC');
    expect(s.cash).toBe('1000');
  });
  it('names it after the dataset and keeps names unique', () => {
    expect(newSleeve(strategies, datasets, ['BTC']).name).toBe('BTC_2');
    expect(newSleeve(strategies, datasets, ['BTC', 'BTC_2']).name).toBe('BTC_3');
  });
  it('can be given a dataset and a strategy', () => {
    const s = newSleeve(strategies, datasets, [], { dataset: 'eth.v.txt', strategy: 'inertia_breakout' });
    expect(s.name).toBe('ETH');
    expect(s.params).toEqual({ lookback: '35', allowance: '5' });
  });
  it('copes with no datasets or strategies yet', () => {
    const s = newSleeve([], [], []);
    expect(s.dataset).toBe('');
    expect(s.strategy).toBe('');
    expect(s.params).toEqual({});
  });
});

describe('sleeveErrors', () => {
  const good = { name: 'BTC', dataset: 'btc.v.txt', strategy: 'inertia_breakout', params: { lookback: '35', allowance: '5' }, cash: '3000' };
  it('is null for a good sleeve', () => expect(sleeveErrors(good, strategies, ['ETH'])).toBe(null));
  it('says what is wrong with each field', () => {
    expect(sleeveErrors({ ...good, name: '  ' }, strategies, [])?.name).toMatch(/name/i);
    expect(sleeveErrors({ ...good, name: 'x'.repeat(41) }, strategies, [])?.name).toMatch(/40/);
    expect(sleeveErrors({ ...good, name: 'ETH' }, strategies, ['ETH'])?.name).toMatch(/already/i);
    expect(sleeveErrors({ ...good, cash: '0' }, strategies, [])?.cash).toMatch(/above 0/);
    expect(sleeveErrors({ ...good, cash: 'abc' }, strategies, [])?.cash).toMatch(/above 0/);
    expect(sleeveErrors({ ...good, dataset: '' }, strategies, [])?.dataset).toMatch(/dataset/i);
    expect(sleeveErrors({ ...good, params: { lookback: '0', allowance: '5' } }, strategies, [])?.params.lookback).toMatch(/1.*500/);
    expect(sleeveErrors({ ...good, params: { lookback: '35', allowance: '5.5' } }, strategies, [])?.params.allowance).toMatch(/whole/i);
  });
  it('only reports the fields that are wrong', () => {
    const e = sleeveErrors({ ...good, cash: '-1' }, strategies, []);
    expect(Object.keys(e)).toEqual(['cash']);
  });
});

describe('portfolioError', () => {
  it('needs at least one sleeve and at most 50', () => {
    expect(portfolioError([], '260')).toMatch(/one sleeve/i);
    expect(portfolioError(new Array(MAX_SLEEVES + 1).fill({}), '260')).toMatch(/50/);
    expect(portfolioError([{}], '260')).toBe(null);
  });
  it('checks bars per year', () => {
    expect(portfolioError([{}], '0')).toMatch(/bars per year/i);
    expect(portfolioError([{}], 'x')).toMatch(/bars per year/i);
  });
});

describe('totalCash', () => {
  it('adds the cash of every sleeve, ignoring bad entries', () => {
    expect(totalCash([{ cash: '3000' }, { cash: '5000' }, { cash: '2000' }, { cash: '500' }])).toBe(10500);
    expect(totalCash([{ cash: '100' }, { cash: 'x' }, { cash: '' }])).toBe(100);
    expect(totalCash([])).toBe(0);
  });
});

describe('buildPortfolioRequest', () => {
  it('makes the body the API expects, with numbers not text', () => {
    const body = buildPortfolioRequest({
      sleeves: [
        { name: 'BTC', dataset: 'btc.v.txt', strategy: 'inertia_breakout', params: { lookback: '35', allowance: '5' }, cash: '3000' },
        { name: 'ETH', dataset: 'eth.v.txt', strategy: 'buy_and_hold', params: { fraction: '1' }, cash: '5000' },
      ],
      commonPeriodOnly: true,
      periodsPerYear: '365',
    });
    expect(body).toEqual({
      sleeves: [
        { name: 'BTC', dataset: 'btc.v.txt', strategy: 'inertia_breakout', params: { lookback: 35, allowance: 5 }, cash: 3000 },
        { name: 'ETH', dataset: 'eth.v.txt', strategy: 'buy_and_hold', params: { fraction: 1 }, cash: 5000 },
      ],
      commonPeriodOnly: true,
      periodsPerYear: 365,
    });
  });
  it('trims names', () => {
    const body = buildPortfolioRequest({
      sleeves: [{ name: '  BTC ', dataset: 'a', strategy: 'b', params: {}, cash: '1' }], commonPeriodOnly: false, periodsPerYear: '260',
    });
    expect(body.sleeves[0].name).toBe('BTC');
  });
});

const result = {
  timestamps: [1000, 2000, 3000],
  sleeves: [
    { name: 'A', dataset: 'a.txt', strategy: 's', cash: 100, finalEquity: 130, equity: [100, 110, 130], metrics: { totalReturn: 0.3, cagr: 1, maxDrawdown: 0.1, mar: 10, sharpe: 2, trades: 3, winRate: 0.5 } },
    { name: 'B', dataset: 'b.txt', strategy: 's', cash: 50, finalEquity: 45, equity: [50, 48, 45], metrics: { totalReturn: -0.1, cagr: -0.5, maxDrawdown: 0.1, mar: -5, sharpe: -1, trades: 1, winRate: 0 } },
  ],
  combined: { cash: 150, finalEquity: 175, equity: [150, 158, 175], metrics: { totalReturn: 175 / 150 - 1, cagr: 0.4, maxDrawdown: 0.02, mar: 20, sharpe: 3, trades: 4, winRate: 0.4 } },
};

describe('summaryRows', () => {
  it('has one row per sleeve and then the portfolio', () => {
    const rows = summaryRows(result);
    expect(rows.map((r) => r.name)).toEqual(['A', 'B', 'PORTFOLIO']);
    expect(rows[0]).toMatchObject({ cash: 100, finalEquity: 130, totalReturn: 0.3, trades: 3, total: false });
    expect(rows[2]).toMatchObject({ cash: 150, finalEquity: 175, trades: 4, total: true });
    expect(rows[1].dataset).toBe('b.txt');
    expect(rows[2].dataset).toBe('');
  });
});

describe('stackedSeries', () => {
  it('gives one [time, equity] series per sleeve, in order, that add up to the combined curve', () => {
    const series = stackedSeries(result);
    expect(series.map((s) => s.name)).toEqual(['A', 'B']);
    expect(series[0].data).toEqual([[1000, 100], [2000, 110], [3000, 130]]);
    const summed = series[0].data.map((p, i) => p[1] + series[1].data[i][1]);
    expect(summed).toEqual(result.combined.equity);
  });
});

describe('ideasByDataset with portfolios', () => {
  it('counts a portfolio once on each distinct dataset it uses', () => {
    const runs = [
      { kind: 'portfolio', ideas: 1, spec: { sleeves: [{ dataset: 'btc.v.txt' }, { dataset: 'eth.v.txt' }, { dataset: 'btc.v.txt' }] } },
      { kind: 'run', ideas: 1, spec: { dataset: 'btc.v.txt' } },
    ];
    expect(ideasByDataset(runs)).toEqual({ 'btc.v.txt': 2, 'eth.v.txt': 1 });
  });
});

// Every sleeve numbers its own trades from 1, so a portfolio's trade list has repeated ids; a list drawn with the id as its
// key would crash ("each_key_duplicate") the moment such a result arrived.
describe('tradeKey', () => {
  it('differs for the same trade id in different sleeves', () => {
    expect(tradeKey({ id: 1, sleeve: 'BTC' })).not.toBe(tradeKey({ id: 1, sleeve: 'ETH' }));
  });
  it('is unique across a realistic portfolio trade list', () => {
    const trades = [];
    for (const sleeve of ['BTC', 'ETH', 'XRP']) for (let id = 1; id <= 50; id++) trades.push({ id, sleeve });
    expect(new Set(trades.map(tradeKey)).size).toBe(trades.length);
  });
  it('still works for a single run, where trades have no sleeve', () => {
    expect(tradeKey({ id: 7 })).toBe(tradeKey({ id: 7 }));
    expect(tradeKey({ id: 7 })).not.toBe(tradeKey({ id: 8 }));
  });
  it('does not confuse a sleeve name that ends in digits', () => {
    expect(tradeKey({ id: 11, sleeve: 'A' })).not.toBe(tradeKey({ id: 1, sleeve: 'A1' }));
  });
});
