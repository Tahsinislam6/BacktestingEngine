import { describe, it, expect } from 'vitest';
import { pct, num, days, dateOf, whenText, layoutText } from '../src/lib/format.js';

// Formatting rules: a missing or non-finite value is always an em dash, never "NaN" or "null".
describe('format', () => {
  it('pct turns a fraction into a percentage', () => {
    expect(pct(0.331)).toBe('33.1%');
    expect(pct(-0.5)).toBe('-50.0%');
    expect(pct(0.12345, 2)).toBe('12.35%');
    expect(pct(0)).toBe('0.0%');
  });
  it('pct groups thousands for huge returns', () => {
    expect(pct(1306497.08)).toBe('130,649,708.0%');
  });
  it('shows a dash for missing values', () => {
    for (const bad of [null, undefined, NaN, Infinity, -Infinity]) {
      expect(pct(bad)).toBe('—');
      expect(num(bad)).toBe('—');
      expect(days(bad)).toBe('—');
    }
  });
  it('num rounds to the given digits and groups thousands', () => {
    expect(num(1.23456)).toBe('1.23');
    expect(num(1234.5, 0)).toBe('1,235');
    expect(num(6532490405.9786, 2)).toBe('6,532,490,405.98');
    expect(num(-0.004)).toBe('0.00'); // never a negative zero
    expect(pct(-0.00001)).toBe('0.0%');
    expect(num(-1.5)).toBe('-1.50');
  });
  it('days converts milliseconds', () => {
    expect(days(86_400_000)).toBe('1.0 d');
    expect(days(3 * 86_400_000 + 43_200_000)).toBe('3.5 d');
    expect(days(0)).toBe('0.0 d');
  });
  it('dateOf gives the UTC calendar date of a millisecond timestamp', () => {
    expect(dateOf(1577836800000)).toBe('2020-01-01');
    expect(dateOf(1577836800000 + 86_399_999)).toBe('2020-01-01');
    expect(dateOf(null)).toBe('—');
  });
  it('whenText shortens an ISO time and keeps the date', () => {
    expect(whenText('2026-11-25T10:51:22Z')).toBe('2026-11-25 10:51');
    expect(whenText('')).toBe('—');
    expect(whenText(undefined)).toBe('—');
  });
});

describe('layoutText', () => {
  it('says nothing for auto, a missing layout (older runs) or an empty one', () => {
    for (const l of ['auto', undefined, null, '']) expect(layoutText(l)).toBe('');
  });
  it('names any other layout, ready to join onto a line', () => {
    expect(layoutText('stooq')).toBe(' · layout stooq');
  });
});
