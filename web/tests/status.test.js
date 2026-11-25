import { describe, it, expect } from 'vitest';
import { isActive, isFinished } from '../src/lib/status.js';

describe('status', () => {
  it('queued and running are active, nothing else is', () => {
    expect(isActive('queued')).toBe(true);
    expect(isActive('running')).toBe(true);
    for (const s of ['done', 'failed', 'cancelled', '', undefined]) expect(isActive(s)).toBe(false);
  });
  it('done, failed and cancelled are finished, nothing else is', () => {
    for (const s of ['done', 'failed', 'cancelled']) expect(isFinished(s)).toBe(true);
    for (const s of ['queued', 'running', '', undefined]) expect(isFinished(s)).toBe(false);
  });
});
