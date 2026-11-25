// Strategy parameters in the form. The inputs hold text; the API wants numbers.

// The default of every parameter, as text for the inputs. `strategy` is one entry of /api/strategies.
export function defaultParams(strategy) {
  const out = {};
  for (const p of strategy?.params ?? []) out[p.name] = String(p.default);
  return out;
}

// What is wrong with the text typed for a parameter, or null if it is fine (the server checks again).
export function paramError(spec, text) {
  const trimmed = String(text ?? '').trim();
  const value = Number(trimmed);
  if (trimmed === '' || !Number.isFinite(value)) return 'Enter a number';
  if (value < spec.min || value > spec.max) return `Must be between ${spec.min} and ${spec.max}`;
  if (spec.integer && !Number.isInteger(value)) return 'Must be a whole number';
  return null;
}

// The body of POST /api/runs from the form's values.
export function buildRequest({ strategy, dataset, params, cash, periodsPerYear }) {
  const numbers = {};
  for (const [name, text] of Object.entries(params)) numbers[name] = Number(text);
  return {
    strategy,
    dataset,
    params: numbers,
    cash: Number(cash),
    periodsPerYear: Number(periodsPerYear),
  };
}
