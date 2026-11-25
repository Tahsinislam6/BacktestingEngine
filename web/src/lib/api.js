// The calls to the C++ server. Every function returns the parsed JSON or throws an Error whose message can be shown as is.

async function request(path, init) {
  let response;
  try {
    response = await fetch(path, init);
  } catch {
    throw new Error('Cannot reach the server. Is it running?');
  }
  let body;
  try {
    body = await response.json();
  } catch {
    throw new Error(`Server error (${response.status})`);
  }
  if (!response.ok) throw new Error(body?.error ?? `Server error (${response.status})`);
  return body;
}

export const getStrategies = () => request('/api/strategies');
export const getDatasets = () => request('/api/datasets');
export const getRuns = () => request('/api/runs');
export const getRun = (id) => request(`/api/runs/${id}`);

// Asks the server to cancel a run: a queued run is cancelled at once, a running sweep at its next step. Returns the server's
// answer ({id, status}); throws with the server's reason if the run cannot be cancelled.
export const postCancel = (id) => request(`/api/runs/${id}/cancel`, { method: 'POST' });

// Queues a run and returns its id.
export async function postRun(spec) {
  const body = await request('/api/runs', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(spec),
  });
  return body.id;
}

// Queues a density sweep and returns its id.
export async function postSweep(spec) {
  const body = await request('/api/sweeps', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(spec),
  });
  return body.id;
}

// Queues a portfolio and returns its id.
export async function postPortfolio(spec) {
  const body = await request('/api/portfolios', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(spec),
  });
  return body.id;
}
