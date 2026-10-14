/**
 * Display formatting of evaluation results (numbers, money, percentages, units, booleans,
 * complex numbers, matrices, functions) and translation of error messages.
 */
import type { Complex, Unit } from 'mathjs';
import type { Locale } from '../types';
import { getEngine } from './engine';
import { calcMessage, CalcError } from './messages';
import type { ValueMeta } from './model';
import { demangleText } from './names';
import { formatMoney, formatNumber, type NumberFormatOptions } from './numbers';

/** Longest value string returned; longer output is cut with an ellipsis. */
export const MAX_DISPLAY_LENGTH = 1000;

function truncate(s: string, max = MAX_DISPLAY_LENGTH): string {
  return s.length > max ? `${s.slice(0, max - 1)}…` : s;
}

/** mathjs unit names → the short forms people write. */
function displayUnits(units: string): string {
  return units
    .replace(/\bminute\b/g, 'min')
    .replace(/\bsecond\b/g, 's')
    .replace(/\bhour\b/g, 'h')
    .replace(/\bdeg([CF])\b/g, '°$1')
    .replace(/^deg$/, '°');
}

function toPlainNumber(v: unknown): number | null {
  const math = getEngine().math;
  if (typeof v === 'number') return v;
  if (typeof v === 'bigint') return Number(v);
  if (math.isBigNumber(v) || math.isFraction(v)) return Number(v.valueOf());
  return null;
}

function formatComplex(c: Complex, locale: Locale, opts: NumberFormatOptions = {}): string {
  const { re, im } = c;
  if (im === 0) return formatNumber(re, locale, opts);
  const imAbs = Math.abs(im) === 1 ? '' : formatNumber(Math.abs(im), locale, opts);
  if (re === 0) return `${im < 0 ? '-' : ''}${imAbs}i`;
  return `${formatNumber(re, locale, opts)} ${im < 0 ? '-' : '+'} ${imAbs}i`;
}

/** Simplified form of a unit value (`2 kg·m/s²` → `N`) without mathjs' automatic prefixes. */
function simplifiedUnit(u: Unit): Unit {
  const skip = (u as unknown as { skipAutomaticSimplification?: boolean }).skipAutomaticSimplification;
  return skip ? u.clone() : u.simplify();
}

/** Magnitude of a unit value in its displayed unit, or null when it has no number. */
export function unitMagnitude(u: Unit): number | null {
  if ((u as unknown as { value: unknown }).value === null) return null;
  const v: unknown = simplifiedUnit(u).toNumeric();
  return toPlainNumber(v);
}

function formatUnit(u: Unit, meta: ValueMeta, locale: Locale, opts: NumberFormatOptions = {}): string {
  const math = getEngine().math;
  if ((u as unknown as { value: unknown }).value === null) return displayUnits(u.formatUnits());
  const simp = simplifiedUnit(u);
  const units = displayUnits(simp.formatUnits());
  const raw: unknown = simp.toNumeric();
  if (math.isComplex(raw)) return `(${formatComplex(raw, locale, opts)}) ${units}`;
  const n = toPlainNumber(raw) ?? NaN;
  // Money per unit: 12 zł/kg
  const perUnit = /^([A-Za-z]+)\^-1$/.exec(units);
  if (meta.currency && perUnit) return `${formatMoney(n, meta.currency, locale, opts)}/${perUnit[1]}`;
  const num = formatNumber(n, locale, opts);
  if (!units) return num;
  return units.startsWith('°') ? `${num}${units}` : `${num} ${units}`;
}

/**
 * `[a; b]` (Polish) / `[a, b]` (English). English numbers in lists are not grouped (`1,000`
 * would be ambiguous next to the `,` separator). Stops early once the display limit is passed.
 */
function formatArray(items: unknown[], meta: ValueMeta, locale: Locale, opts: NumberFormatOptions = {}): string {
  const sep = locale === 'pl' ? '; ' : ', ';
  const inner: NumberFormatOptions = { ...opts, grouping: locale !== 'en' && (opts.grouping ?? true) };
  let out = '[';
  for (let i = 0; i < items.length; i++) {
    const v = items[i];
    if (i > 0) out += sep;
    out += Array.isArray(v) ? formatArray(v, meta, locale, inner) : (formatValue(v, meta, locale, inner) ?? '');
    if (out.length > MAX_DISPLAY_LENGTH) return out;
  }
  return `${out}]`;
}

/**
 * Human-readable result for a mathjs value; undefined for "no value" (null/undefined, empty
 * result sets).
 */
export function formatValue(value: unknown, meta: ValueMeta, locale: Locale, opts: NumberFormatOptions = {}): string | undefined {
  const math = getEngine().math;
  if (value === undefined || value === null) return undefined;
  if (math.isResultSet(value)) {
    const entries = value.entries;
    return entries.length ? formatValue(entries[entries.length - 1], meta, locale, opts) : undefined;
  }
  const plain = toPlainNumber(value);
  if (plain !== null) {
    if (meta.percent) return `${formatNumber(plain * 100, locale, opts)}%`;
    if (meta.currency && Number.isFinite(plain)) return formatMoney(plain, meta.currency, locale, opts);
    return formatNumber(plain, locale, opts);
  }
  if (typeof value === 'boolean') return calcMessage(locale, value ? 'calc.value.true' : 'calc.value.false');
  if (typeof value === 'string') return truncate(value);
  if (math.isUnit(value)) return truncate(formatUnit(value, meta, locale, opts));
  if (math.isComplex(value)) return formatComplex(value, locale, opts);
  if (math.isMatrix(value)) return truncate(formatArray(value.toArray() as unknown[], meta, locale, opts));
  if (Array.isArray(value)) return truncate(formatArray(value, meta, locale, opts));
  if (typeof value === 'function') {
    const syntax = (value as { syntax?: unknown }).syntax;
    return typeof syntax === 'string' ? demangleText(syntax) : 'ƒ';
  }
  try {
    return truncate(demangleText(math.format(value, { precision: 10 })));
  } catch {
    return truncate(String(value));
  }
}

/** Numeric magnitude of a value (numbers, big numbers, fractions, unit magnitudes). */
export function numericValue(value: unknown): number | undefined {
  const math = getEngine().math;
  const plain = toPlainNumber(value);
  if (plain !== null) return plain;
  if (math.isUnit(value)) return unitMagnitude(value) ?? undefined;
  return undefined;
}

const ERROR_PATTERNS: [RegExp, (m: RegExpExecArray) => CalcError][] = [
  [/^Undefined symbol (.+)$/, (m) => new CalcError('calc.error.undefinedSymbol', { name: m[1]! })],
  [/^Undefined function (.+)$/, (m) => new CalcError('calc.error.undefinedFunction', { name: m[1]! })],
  [/^'(.+)' is not a function/, (m) => new CalcError('calc.error.notAFunction', { name: m[1]! })],
  [/^Units do not match/, () => new CalcError('calc.error.unitsMismatch')],
  [/^Unexpected end of expression/, () => new CalcError('calc.error.unexpectedEnd')],
  [/^Parenthesis (\S) expected/, (m) => new CalcError('calc.error.parenExpected', { paren: m[1]! })],
  [/^Value expected/, () => new CalcError('calc.error.valueExpected')],
  [/^Unexpected operator (.+)$/, (m) => new CalcError('calc.error.unexpectedOperator', { op: m[1]! })],
  [/^Unexpected part "(.+)"$/, (m) => new CalcError('calc.error.unexpectedPart', { part: m[1]! })],
  [/^Cannot convert "(.*)" to a number/, (m) => new CalcError('calc.error.notANumber', { text: m[1]! })],
  [/^Unexpected type of argument in function (\S+)/, (m) => new CalcError('calc.error.wrongType', { name: m[1]! })],
  [/^Too many arguments in function (\S+)/, (m) => new CalcError('calc.error.tooManyArgs', { name: m[1]! })],
  [/^Too few arguments in function (\S+)/, (m) => new CalcError('calc.error.tooFewArgs', { name: m[1]! })],
  [/Maximum call stack size exceeded|too much recursion/i, () => new CalcError('calc.error.recursion')],
  [/^Cannot calculate .* of an empty array/, () => new CalcError('calc.error.emptyArray')],
];

/**
 * Short, human-readable message for an evaluation error (Polish or English); internal names
 * are shown as the user wrote them and mathjs' character positions are dropped (they refer to
 * the rewritten expression).
 */
export function describeError(err: unknown, locale: Locale): string {
  try {
    return describeErrorText(err, locale);
  } catch {
    // Describing must never fail (callers promise not to throw).
    return calcMessage(locale, 'calc.error.generic');
  }
}

function describeErrorText(err: unknown, locale: Locale): string {
  if (err instanceof CalcError) return err.describe(locale);
  const raw = err instanceof Error ? err.message : String(err);
  const embedded = CalcError.fromMessage(raw);
  if (embedded) return embedded.describe(locale);
  const msg = demangleText(raw.split('\n')[0] ?? '')
    .replace(/^Function \S+ cannot apply callback arguments (?:to function \S+: )?/, '')
    .replace(/\s*\(char \d+\)/g, '')
    .replace(/\s*\(expected: .*$/, '')
    .trim();
  for (const [re, make] of ERROR_PATTERNS) {
    const m = re.exec(msg);
    if (m) return make(m).describe(locale);
  }
  return msg ? truncate(msg, 160) : calcMessage(locale, 'calc.error.generic');
}
