/**
 * Date formatting with date-fns tokens (journal file names, template variables).
 *
 * Obsidian stores Moment.js formats (`YYYY-MM-DD`, `dddd, D MMMM`); such formats are
 * recognised and converted to date-fns tokens, so imported settings and templates keep
 * working. A format is treated as Moment.js when it contains `Y`, `D` or `[literal]`
 * (date-fns rejects `YYYY`/`DD` without extra options and uses `'literal'` quoting), or —
 * when there is no date-fns `y` year — `ddd` / `dddd` (Moment weekday names; date-fns has no
 * such tokens) or an `A` AM/PM marker (not a date-fns token).
 */
import { format, isValid, parse } from 'date-fns';
import { enUS } from 'date-fns/locale/en-US';
import { pl } from 'date-fns/locale/pl';
import type { Locale } from '../types';

/** Moment token -> date-fns token. */
const MOMENT_TOKENS: Readonly<Record<string, string>> = {
  YYYY: 'yyyy',
  YY: 'yy',
  Y: 'y',
  MMMM: 'MMMM',
  MMM: 'MMM',
  MM: 'MM',
  M: 'M',
  Mo: 'Mo',
  DDDD: 'DDD',
  DDD: 'D',
  DD: 'dd',
  D: 'd',
  Do: 'do',
  dddd: 'EEEE',
  ddd: 'EEE',
  dd: 'EEEEEE',
  d: 'e',
  E: 'i',
  HH: 'HH',
  H: 'H',
  hh: 'hh',
  h: 'h',
  kk: 'kk',
  k: 'k',
  mm: 'mm',
  m: 'm',
  ss: 'ss',
  s: 's',
  SSS: 'SSS',
  SS: 'SS',
  S: 'S',
  A: 'a',
  a: 'aaa',
  Q: 'Q',
  Qo: 'Qo',
  ww: 'ww',
  w: 'w',
  WW: 'II',
  W: 'I',
  GGGG: 'RRRR',
  gggg: 'RRRR',
  X: 't',
  x: 'T',
  Z: 'xxx',
  ZZ: 'xx',
};

/** Heuristic: does `fmt` look like a Moment.js format? */
export function isMomentFormat(fmt: string): boolean {
  const unquoted = fmt.replace(/'[^']*'/g, '');
  if (/[YD[]/.test(unquoted)) return true;
  // date-fns years ("yyyy") mean a date-fns format, even with odd tokens.
  if (unquoted.includes('y')) return false;
  return /(?:^|[^d])d{3,4}(?!d)/.test(unquoted) || unquoted.includes('A');
}

function quoteLiteral(s: string): string {
  return s ? `'${s.replace(/'/g, "''")}'` : '';
}

/** Convert a Moment.js format to date-fns tokens (unknown letters become quoted literals). */
export function momentToDateFns(fmt: string): string {
  let out = '';
  let i = 0;
  while (i < fmt.length) {
    const c = fmt[i]!;
    if (c === '[') {
      const close = fmt.indexOf(']', i + 1);
      if (close !== -1) {
        out += quoteLiteral(fmt.slice(i + 1, close));
        i = close + 1;
        continue;
      }
    }
    if (/[A-Za-z]/.test(c)) {
      let j = i;
      while (j < fmt.length && fmt[j] === c) j++;
      let run = fmt.slice(i, j);
      // Ordinal suffix tokens: "Do", "Mo", "Qo".
      if (fmt[j] === 'o' && run.length === 1 && MOMENT_TOKENS[run + 'o']) {
        run += 'o';
        j++;
      }
      const mapped = MOMENT_TOKENS[run];
      out += mapped ?? quoteLiteral(run);
      i = j;
      continue;
    }
    out += c === "'" ? "''" : c;
    i++;
  }
  return out;
}

/** The date-fns format for a configured format string (Moment.js formats converted). */
export function toDateFnsFormat(fmt: string): string {
  return isMomentFormat(fmt) ? momentToDateFns(fmt) : fmt;
}

function dfLocale(locale: Locale | undefined): typeof enUS {
  return locale === 'pl' ? pl : enUS;
}

/** Format `date` with a date-fns (or Moment.js) format; null for invalid formats or dates. */
export function formatDate(date: Date, fmt: string, locale?: Locale): string | null {
  if (Number.isNaN(date.getTime())) return null;
  try {
    return format(date, toDateFnsFormat(fmt), { locale: dfLocale(locale) });
  } catch {
    return null;
  }
}

/** date-fns `parse`, null instead of throwing / Invalid Date. */
function tryParse(text: string, fmt: string, opts: { locale: typeof enUS }): Date | null {
  try {
    const d = parse(text, fmt, new Date(2000, 0, 1, 0, 0, 0, 0), opts);
    return isValid(d) ? d : null;
  } catch {
    return null;
  }
}

/** date-fns `format`, null instead of throwing. */
function tryFormat(d: Date, fmt: string, opts: { locale: typeof enUS }): string | null {
  try {
    return format(d, fmt, opts);
  } catch {
    return null;
  }
}

/** Split a date-fns format on `/` outside `'quoted'` literals. */
export function splitFormatPath(fmt: string): string[] {
  const parts: string[] = [];
  let quoted = false;
  let start = 0;
  for (let i = 0; i < fmt.length; i++) {
    const c = fmt[i];
    if (c === "'") quoted = !quoted;
    else if (c === '/' && !quoted) {
      parts.push(fmt.slice(start, i));
      start = i + 1;
    }
  }
  parts.push(fmt.slice(start));
  return parts;
}

/**
 * Parse `text` with a date-fns (or Moment.js) format, strictly: the parsed date must format
 * back to exactly `text`. Returns a local Date or null.
 *
 * date-fns refuses formats that repeat a unit, which nested journal formats always do
 * (`yyyy/MM/yyyy-MM-dd`); such texts are parsed path part by path part (last part first)
 * and accepted when the candidate date formats back to the whole text.
 */
export function parseDateStrict(text: string, fmt: string, locale?: Locale): Date | null {
  const f = toDateFnsFormat(fmt);
  const opts = { locale: dfLocale(locale) };
  const d = tryParse(text, f, opts);
  if (d && tryFormat(d, f, opts) === text) return d;
  const fParts = splitFormatPath(f);
  const tParts = text.split('/');
  if (fParts.length < 2 || fParts.length !== tParts.length) return null;
  for (let k = fParts.length - 1; k >= 0; k--) {
    const candidate = tryParse(tParts[k]!, fParts[k]!, opts);
    if (candidate && tryFormat(candidate, f, opts) === text) return candidate;
  }
  return null;
}
