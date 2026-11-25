// Turns numbers from the API into text. A value that is missing or not finite (the server sends null for those) is
// always an em dash, never "NaN" or "null".

const DASH = '—';

const formatters = new Map();
function formatter(digits) {
  if (!formatters.has(digits)) {
    formatters.set(
      digits,
      new Intl.NumberFormat('en-US', { minimumFractionDigits: digits, maximumFractionDigits: digits }),
    );
  }
  return formatters.get(digits);
}

function isNumber(x) {
  return typeof x === 'number' && Number.isFinite(x);
}

// A number with thousands separators; a negative that rounds to zero is shown as plain zero.
export function num(x, digits = 2) {
  if (!isNumber(x)) return DASH;
  const text = formatter(digits).format(x);
  return /^-0(\.0+)?$/.test(text) ? text.slice(1) : text;
}

// A fraction as a percentage: 0.331 -> "33.1%".
export function pct(x, digits = 1) {
  if (!isNumber(x)) return DASH;
  return num(x * 100, digits) + '%';
}

// A duration in milliseconds as days.
export function days(ms) {
  if (!isNumber(ms)) return DASH;
  return num(ms / 86_400_000, 1) + ' d';
}

// The UTC calendar date of a millisecond timestamp: "2020-01-01".
export function dateOf(ms) {
  if (!isNumber(ms)) return DASH;
  return new Date(ms).toISOString().slice(0, 10);
}

// "2026-11-25T10:51:22Z" -> "2026-11-25 10:51".
export function whenText(iso) {
  if (typeof iso !== 'string' || iso.length < 16) return DASH;
  return iso.slice(0, 16).replace('T', ' ');
}
