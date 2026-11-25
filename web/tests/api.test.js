import { describe, it, expect, vi, afterEach } from 'vitest';
import { getStrategies, getDatasets, postRun, postSweep, postPortfolio, getRun, getRuns } from '../src/lib/api.js';

function reply(status, body) {
  return { ok: status >= 200 && status < 300, status, json: async () => body };
}

afterEach(() => vi.unstubAllGlobals());

describe('api', () => {
  it('GETs the right paths and returns the parsed JSON', async () => {
    const fetchMock = vi.fn(async () => reply(200, ['x']));
    vi.stubGlobal('fetch', fetchMock);
    expect(await getStrategies()).toEqual(['x']);
    expect(await getDatasets()).toEqual(['x']);
    expect(await getRuns()).toEqual(['x']);
    expect(await getRun('000001')).toEqual(['x']);
    expect(fetchMock.mock.calls.map((c) => c[0])).toEqual([
      '/api/strategies', '/api/datasets', '/api/runs', '/api/runs/000001',
    ]);
  });
  it('POSTs a run as JSON and returns its id', async () => {
    const fetchMock = vi.fn(async () => reply(202, { id: '000007' }));
    vi.stubGlobal('fetch', fetchMock);
    const id = await postRun({ strategy: 'a', dataset: 'b' });
    expect(id).toBe('000007');
    const [path, init] = fetchMock.mock.calls[0];
    expect(path).toBe('/api/runs');
    expect(init.method).toBe('POST');
    expect(JSON.parse(init.body)).toEqual({ strategy: 'a', dataset: 'b' });
    expect(init.headers['Content-Type']).toBe('application/json');
  });
  it('POSTs a sweep to its own path and returns its id', async () => {
    const fetchMock = vi.fn(async () => reply(202, { id: '000009' }));
    vi.stubGlobal('fetch', fetchMock);
    expect(await postSweep({ strategy: 'a', note: 'n' })).toBe('000009');
    const [path, init] = fetchMock.mock.calls[0];
    expect(path).toBe('/api/sweeps');
    expect(init.method).toBe('POST');
    expect(JSON.parse(init.body)).toEqual({ strategy: 'a', note: 'n' });
  });
  it('POSTs a portfolio to its own path and returns its id', async () => {
    const fetchMock = vi.fn(async () => reply(202, { id: '000011' }));
    vi.stubGlobal('fetch', fetchMock);
    expect(await postPortfolio({ sleeves: [] })).toBe('000011');
    const [path, init] = fetchMock.mock.calls[0];
    expect(path).toBe('/api/portfolios');
    expect(init.method).toBe('POST');
    expect(JSON.parse(init.body)).toEqual({ sleeves: [] });
  });
  it('shows the server reason a portfolio was refused', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => reply(400, { error: 'two sleeves are named BTC' })));
    await expect(postPortfolio({})).rejects.toThrow('two sleeves are named BTC');
  });
  it('shows the server reason a sweep was refused', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => reply(400, { error: 'the note is required: write what you expect' })));
    await expect(postSweep({})).rejects.toThrow('the note is required');
  });
  it("throws the server's own error text", async () => {
    vi.stubGlobal('fetch', vi.fn(async () => reply(400, { error: 'unknown strategy: nope' })));
    await expect(postRun({})).rejects.toThrow('unknown strategy: nope');
  });
  it('throws a readable error when the server cannot be reached', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => { throw new TypeError('Failed to fetch'); }));
    await expect(getStrategies()).rejects.toThrow(/server/i);
  });
  it('throws with the status when the reply is not JSON', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => ({ ok: false, status: 502, json: async () => { throw new Error('bad'); } })));
    await expect(getRuns()).rejects.toThrow('502');
  });
});
