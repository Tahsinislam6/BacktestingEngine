// The portfolio screens' logic that is not drawing: new sleeves with sensible starting values, checking a sleeve as you type
// (the server checks again), the request body, and turning a result into table rows and chart series.

import { defaultParams, paramError } from './params.js';
import { toPoints } from './series.js';

export const MAX_SLEEVES = 50;
export const MAX_NAME_LENGTH = 40;

// A new sleeve. It starts on the first dataset and strategy (or the ones given) with default parameters, named after its
// dataset ("btc.v.txt" -> "BTC"), with a number added if that name is taken.
export function newSleeve(strategies, datasets, usedNames, { dataset, strategy } = {}) {
  const chosenDataset = dataset ?? datasets[0] ?? '';
  const chosenStrategy = strategy ?? strategies[0]?.name ?? '';
  const base = chosenDataset ? chosenDataset.split('.')[0].toUpperCase() : 'SLEEVE';
  let name = base;
  for (let n = 2; usedNames.includes(name); n++) name = `${base}_${n}`;
  return {
    name,
    dataset: chosenDataset,
    strategy: chosenStrategy,
    params: defaultParams(strategies.find((s) => s.name === chosenStrategy)),
    cash: '1000',
    layout: 'auto',
  };
}

// What is wrong with a sleeve, as { name?, dataset?, strategy?, cash?, params?: { param: message } }, or null if nothing.
// `otherNames` are the names of the other sleeves.
export function sleeveErrors(sleeve, strategies, otherNames) {
  const errors = {};
  const name = sleeve.name.trim();
  if (name === '') errors.name = 'Give the sleeve a name';
  else if (name.length > MAX_NAME_LENGTH) errors.name = `At most ${MAX_NAME_LENGTH} characters`;
  else if (otherNames.includes(name)) errors.name = 'This name is already used by another sleeve';

  if (!sleeve.dataset) errors.dataset = 'Choose a dataset';

  const strategy = strategies.find((s) => s.name === sleeve.strategy);
  if (!strategy) errors.strategy = 'Choose a strategy';

  const cash = Number(sleeve.cash);
  if (String(sleeve.cash).trim() === '' || !Number.isFinite(cash) || cash <= 0) errors.cash = 'Must be above 0';

  if (strategy) {
    const bad = {};
    for (const p of strategy.params) {
      const message = paramError(p, sleeve.params[p.name]);
      if (message) bad[p.name] = message;
    }
    if (Object.keys(bad).length > 0) errors.params = bad;
  }
  return Object.keys(errors).length > 0 ? errors : null;
}

// What is wrong with the portfolio as a whole, or null.
export function portfolioError(sleeves, periodsPerYear) {
  if (sleeves.length === 0) return 'Add at least one sleeve';
  if (sleeves.length > MAX_SLEEVES) return `At most ${MAX_SLEEVES} sleeves`;
  const ppy = Number(periodsPerYear);
  if (String(periodsPerYear).trim() === '' || !Number.isFinite(ppy) || ppy <= 0) return 'Bars per year must be above 0';
  return null;
}

// The sum of the sleeves' cash (entries that are not a positive number count as nothing).
export function totalCash(sleeves) {
  let total = 0;
  for (const s of sleeves) {
    const cash = Number(s.cash);
    if (String(s.cash).trim() !== '' && Number.isFinite(cash) && cash > 0) total += cash;
  }
  return total;
}

// The body of POST /api/portfolios from the form's values (text) with numbers where numbers belong.
export function buildPortfolioRequest({ sleeves, commonPeriodOnly, periodsPerYear }) {
  return {
    sleeves: sleeves.map((s) => {
      const params = {};
      for (const [name, text] of Object.entries(s.params)) params[name] = Number(text);
      return { name: s.name.trim(), dataset: s.dataset, strategy: s.strategy, params, cash: Number(s.cash), layout: s.layout || 'auto' };
    }),
    commonPeriodOnly: !!commonPeriodOnly,
    periodsPerYear: Number(periodsPerYear),
  };
}

// One row per sleeve and then one for the whole portfolio, for the results table.
export function summaryRows(result) {
  const row = (name, dataset, strategy, cash, finalEquity, m, total) => ({
    name, dataset, strategy, cash, finalEquity, total,
    totalReturn: m.totalReturn, cagr: m.cagr, maxDrawdown: m.maxDrawdown, mar: m.mar, sharpe: m.sharpe,
    trades: m.trades, winRate: m.winRate,
  });
  return [
    ...result.sleeves.map((s) => row(s.name, s.dataset, s.strategy, s.cash, s.finalEquity, s.metrics, false)),
    row('PORTFOLIO', '', '', result.combined.cash, result.combined.finalEquity, result.combined.metrics, true),
  ];
}

// One { name, data: [[time, equity], ...] } per sleeve, in order. Stacked, they add up to the combined curve.
export function stackedSeries(result) {
  return result.sleeves.map((s) => ({ name: s.name, data: toPoints(result.timestamps, s.equity) }));
}

// A key for one trade in a list. Every sleeve numbers its own trades from 1, so the id alone repeats in a portfolio's list.
export function tradeKey(trade) {
  return `${trade.sleeve ?? ''}\u0000${trade.id}`;
}
