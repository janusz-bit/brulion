/**
 * Locale-aware number formatting and parsing, plus currency recognition.
 *
 * Formatting is implemented by hand (not `Intl.NumberFormat`) so results are identical on every
 * platform (desktop WebKitGTK, Android WebView, browsers, Node) regardless of ICU data.
 */
import type { Locale } from '../types';

/** Non-breaking space used as the Polish thousands separator. */
export const NBSP = '\u00a0';

/** Maximum significant digits shown for fractional results. */
export const SIGNIFICANT_DIGITS = 10;

/** A currency a number was written with; `symbol` is shown as typed (`zł`, `PLN`, `€`, ...). */
export interface CurrencyInfo {
  /** ISO code: PLN, EUR, USD, GBP, CHF. */
  code: string;
  /** Symbol or code as written by the user. */
  symbol: string;
  /** Written before the number (`$12`, `€5`). */
  prefix: boolean;
}

/** Currency symbols / codes recognised next to numbers → ISO code. */
export const CURRENCY_CODES: ReadonlyMap<string, string> = new Map([
  ['zł', 'PLN'],
  ['PLN', 'PLN'],
  ['€', 'EUR'],
  ['EUR', 'EUR'],
  ['$', 'USD'],
  ['USD', 'USD'],
  ['£', 'GBP'],
  ['GBP', 'GBP'],
  ['CHF', 'CHF'],
]);

/** Currency symbols that are single non-letter characters (lexed separately from words). */
export const CURRENCY_SYMBOL_CHARS = new Set(['€', '$', '£']);

function decimalSeparator(locale: Locale): string {
  return locale === 'pl' ? ',' : '.';
}

/** Options of the number formatters. */
export interface NumberFormatOptions {
  /** Group thousands (default true); off inside English lists, where `,` separates items. */
  grouping?: boolean;
}

/** Group the digits of a non-negative integer string according to the locale. */
function groupInteger(digits: string, locale: Locale, grouping = true): string {
  // Polish groups only from 5 digits up (10 000), English from 4 (1,000).
  const minDigits = locale === 'pl' ? 5 : 4;
  if (!grouping || digits.length < minDigits) return digits;
  const sep = locale === 'pl' ? NBSP : ',';
  let out = '';
  for (let i = 0; i < digits.length; i++) {
    if (i > 0 && (digits.length - i) % 3 === 0) out += sep;
    out += digits[i];
  }
  return out;
}

/** Localise a plain JS decimal string ("-1234.5") → "-1234,5" / "-1,234.5". */
function localiseDecimal(plain: string, locale: Locale, grouping = true): string {
  const negative = plain.startsWith('-');
  const body = negative ? plain.slice(1) : plain;
  const [int = '0', frac] = body.split('.');
  const grouped = groupInteger(int, locale, grouping);
  return (negative ? '-' : '') + grouped + (frac ? decimalSeparator(locale) + frac : '');
}

function trimFraction(s: string): string {
  return s.includes('.') ? s.replace(/\.?0+$/, '') : s;
}

function formatExponential(n: number, locale: Locale): string {
  const [mantissa = '0', exp = '0'] = n.toExponential(SIGNIFICANT_DIGITS - 1).split('e');
  const m = trimFraction(mantissa).replace('.', decimalSeparator(locale));
  return `${m}e${exp.replace('+', '')}`;
}

/**
 * Format a number for display: up to 10 significant digits (integers are never rounded below
 * 1e15), trailing zeros trimmed, Polish `12 345,5` (NBSP grouping from 10 000) or English
 * `12,345.5`. Very large / small magnitudes use exponent notation (`1,5e21`).
 */
export function formatNumber(n: number, locale: Locale, options: NumberFormatOptions = {}): string {
  if (Number.isNaN(n)) return 'NaN';
  if (n === Infinity) return '∞';
  if (n === -Infinity) return '-∞';
  if (n === 0) return '0';
  const abs = Math.abs(n);
  if (abs >= 1e15 || abs < 1e-6) {
    const rounded = Number(n.toPrecision(SIGNIFICANT_DIGITS));
    if (Math.abs(rounded) < 1e15 && Math.abs(rounded) >= 1e-6) return formatNumber(rounded, locale, options);
    return formatExponential(n, locale);
  }
  let plain: string;
  if (abs >= 1) {
    const intDigits = Math.floor(Math.log10(abs)) + 1;
    plain = trimFraction(n.toFixed(Math.max(0, SIGNIFICANT_DIGITS - intDigits)));
  } else {
    const leadingZeros = -Math.floor(Math.log10(abs)) - 1;
    plain = trimFraction(n.toFixed(Math.min(100, leadingZeros + SIGNIFICANT_DIGITS)));
  }
  if (plain === '-0' || /^-?0$/.test(plain)) return '0';
  return localiseDecimal(plain, locale, options.grouping ?? true);
}

/**
 * Format with a fixed number of fraction digits (used for money: `12,50`). Falls back to
 * `formatNumber` for non-finite or huge values.
 */
export function formatFixed(n: number, fractionDigits: number, locale: Locale, options: NumberFormatOptions = {}): string {
  if (!Number.isFinite(n) || Math.abs(n) >= 1e15) return formatNumber(n, locale, options);
  let plain = n.toFixed(fractionDigits);
  if (/^-0(\.0*)?$/.test(plain)) plain = plain.slice(1);
  return localiseDecimal(plain, locale, options.grouping ?? true);
}

/** Format an amount of money: integers without decimals, otherwise exactly two. */
export function formatMoney(n: number, currency: CurrencyInfo, locale: Locale, options: NumberFormatOptions = {}): string {
  const rounded = Math.round(n * 100) / 100;
  const body = Number.isInteger(rounded) ? formatNumber(rounded, locale, options) : formatFixed(rounded, 2, locale, options);
  if (currency.prefix) {
    return body.startsWith('-') ? `-${currency.symbol}${body.slice(1)}` : `${currency.symbol}${body}`;
  }
  return `${body} ${currency.symbol}`;
}

/** Result of `parseLocaleNumber`. */
export interface ParsedNumber {
  value: number;
  currency?: CurrencyInfo;
  /** Written as a percentage; `value` is already the fraction (`23%` → 0.23). */
  percent?: boolean;
}

const SPACE_CHARS = /\s/g;
const PREFIX_CURRENCY_RE = /^(zł|PLN|EUR|USD|GBP|CHF|€|\$|£)\s*/u;
const SUFFIX_CURRENCY_RE = /\s*(zł|PLN|EUR|USD|GBP|CHF|€|\$|£)$/u;

/** Remove light Markdown emphasis around a cell value (`**12**`, `_5_`, `` `7` ``, `~~3~~`). */
export function stripInlineMarkup(text: string): string {
  let s = text.trim();
  for (;;) {
    const m = /^(\*\*|__|~~|\*|_|`)(.+)\1$/su.exec(s);
    if (!m) return s;
    s = m[2]!.trim();
  }
}

/** Interpret the separators of an unsigned digit string with `.`, `,` and spaces. */
function normaliseDigits(body: string, locale: Locale): string | null {
  // Spaces are only valid as thousands separators between groups of three digits.
  if (/\s/.test(body)) {
    if (!/^\d{1,3}(?:\s\d{3})+(?:[.,]\d+)?$/u.test(body)) return null;
    body = body.replace(SPACE_CHARS, '');
  }
  const dots = (body.match(/\./g) ?? []).length;
  const commas = (body.match(/,/g) ?? []).length;
  if (dots && commas) {
    // The last separator is the decimal one, the other is grouping: 1.234,56 / 1,234.56
    const decimal = body.lastIndexOf(',') > body.lastIndexOf('.') ? ',' : '.';
    const group = decimal === ',' ? '.' : ',';
    if ((decimal === ',' ? commas : dots) !== 1) return null;
    const [int = '', frac = ''] = body.split(decimal);
    if (!new RegExp(`^\\d{1,3}(?:\\${group}\\d{3})+$`).test(int)) return null;
    return `${int.split(group).join('')}.${frac}`;
  }
  if (commas) {
    if (commas === 1 && !(locale === 'en' && /^\d{1,3},\d{3}$/.test(body))) return body.replace(',', '.');
    return /^\d{1,3}(?:,\d{3})+$/.test(body) ? body.replace(/,/g, '') : null;
  }
  if (dots > 1) return /^\d{1,3}(?:\.\d{3})+$/.test(body) ? body.replace(/\./g, '') : null;
  return body;
}

/**
 * Parse a number as people write it in notes and table cells: `12,5`, `1 234,50 zł`, `€12.50`,
 * `-3`, `23%`, `1.234,56`, `1,234.56` (English), `1e3`. Returns null for anything else.
 */
export function parseLocaleNumber(text: string, locale: Locale = 'pl'): ParsedNumber | null {
  let s = stripInlineMarkup(text.normalize('NFC'));
  if (!s) return null;
  let currency: CurrencyInfo | undefined;
  let sign = 1;
  const signMatch = /^([-−+])\s*/u.exec(s);
  if (signMatch) {
    if (signMatch[1] !== '+') sign = -1;
    s = s.slice(signMatch[0].length);
  }
  const pre = PREFIX_CURRENCY_RE.exec(s);
  if (pre) {
    currency = { code: CURRENCY_CODES.get(pre[1]!)!, symbol: pre[1]!, prefix: true };
    s = s.slice(pre[0].length);
  } else {
    const suf = SUFFIX_CURRENCY_RE.exec(s);
    if (suf) {
      currency = { code: CURRENCY_CODES.get(suf[1]!)!, symbol: suf[1]!, prefix: false };
      s = s.slice(0, s.length - suf[0].length);
    }
  }
  if (!currency && sign === 1) {
    const late = /^([-−])\s*/u.exec(s);
    if (late) {
      sign = -1;
      s = s.slice(late[0].length);
    }
  }
  let percent = false;
  if (!currency && /\s*%$/.test(s)) {
    percent = true;
    s = s.replace(/\s*%$/, '');
  }
  const exp = /[eE][+-]?\d+$/.exec(s);
  const mantissa = exp ? s.slice(0, exp.index) : s;
  if (!/^\d[\d\s.,]*$|^[.,]\d+$/u.test(mantissa)) return null;
  const digits = normaliseDigits(mantissa.startsWith(',') ? `0${mantissa}` : mantissa, locale);
  if (digits === null || !/^\d*\.?\d+$|^\d+\.?$/.test(digits)) return null;
  let value = sign * Number(digits + (exp ? exp[0] : ''));
  if (!Number.isFinite(value)) return null;
  if (percent) value /= 100;
  const out: ParsedNumber = { value };
  if (currency) out.currency = currency;
  if (percent) out.percent = true;
  return out;
}
