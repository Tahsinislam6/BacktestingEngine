// Small helpers that turn a result's arrays into chart data.

// How far below its running peak the equity is at each bar, as a fraction: 0 at a new high, -0.5 at half the peak.
export function drawdownSeries(equity) {
  let peak = -Infinity;
  return equity.map((value) => {
    if (value > peak) peak = value;
    return peak > 0 ? value / peak - 1 : 0;
  });
}

// [[x, y], ...] from two lists; stops at the shorter one.
export function toPoints(xs, ys) {
  const n = Math.min(xs.length, ys.length);
  const points = new Array(n);
  for (let i = 0; i < n; i++) points[i] = [xs[i], ys[i]];
  return points;
}

// The largest number in the list, ignoring nulls; null if there is none.
export function maxOf(values) {
  let best = null;
  for (const v of values) {
    if (typeof v === 'number' && (best === null || v > best)) best = v;
  }
  return best;
}
