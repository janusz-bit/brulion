/**
 * Date expressions used by queries (docs/SYNTAX.md §10): `due:today`, `due:<=+7d`,
 * `done:this-week`, `due:overdue`, … The comparison operator is handled by the caller; this
 * module only resolves the expression to a date range.
 */
import type { LocalDate } from '../types';
import { foldText } from './fold';
import { own } from './names';
import {
  addDaysLocal,
  addMonthsLocal,
  addYearsLocal,
  endOfMonthLocal,
  endOfWeekLocal,
  isLocalDate,
  normalizeWeekStart,
  startOfMonthLocal,
  startOfWeekLocal,
  toLocalDate,
} from './local';

/** Inclusive range of local dates. */
export type DateRange = { from: LocalDate; to: LocalDate };

/** Result of {@link resolveDateExpr}: a range, a special keyword, or null (not understood). */
export type DateExprResult = DateRange | 'overdue' | 'none' | 'any' | null;

type Period = 'week' | 'month' | 'year';
type Which = 'this' | 'next' | 'last';

const KEYWORDS: Record<string, 'overdue' | 'none' | 'any'> = {
  overdue: 'overdue', zalegle: 'overdue', zalegly: 'overdue', przeterminowane: 'overdue',
  none: 'none', brak: 'none', bez: 'none',
  any: 'any', dowolna: 'any', dowolny: 'any', dowolne: 'any', jakakolwiek: 'any', kazda: 'any',
};

const DAY_WORDS: Record<string, number> = {
  today: 0, dzis: 0, dzisiaj: 0,
  tomorrow: 1, jutro: 1,
  yesterday: -1, wczoraj: -1,
  pojutrze: 2, przedwczoraj: -2,
};

const WHICH_WORDS: Record<string, Which> = {
  this: 'this', current: 'this', ten: 'this', ta: 'this', biezacy: 'this', biezaca: 'this', obecny: 'this',
  next: 'next', nastepny: 'next', nastepna: 'next', przyszly: 'next', przyszla: 'next', kolejny: 'next',
  last: 'last', previous: 'last', prev: 'last', zeszly: 'last', zeszla: 'last', poprzedni: 'last', poprzednia: 'last', ubiegly: 'last', ostatni: 'last',
};

const PERIOD_WORDS: Record<string, Period> = {
  week: 'week', tydzien: 'week', tygodnia: 'week', tyg: 'week',
  month: 'month', miesiac: 'month', miesiaca: 'month', mies: 'month',
  year: 'year', rok: 'year', roku: 'year',
};

const OFFSET_UNITS: Record<string, 'd' | 'w' | 'm' | 'y'> = {
  d: 'd', day: 'd', days: 'd', dzien: 'd', dni: 'd',
  w: 'w', t: 'w', week: 'w', weeks: 'w', tyg: 'w', tydzien: 'w', tygodnie: 'w', tygodni: 'w',
  m: 'm', month: 'm', months: 'm', mies: 'm', miesiac: 'm', miesiace: 'm', miesiecy: 'm',
  y: 'y', r: 'y', year: 'y', years: 'y', rok: 'y', lata: 'y', lat: 'y',
};

function single(d: LocalDate): DateRange {
  return { from: d, to: d };
}

/** Range of the week/month/year containing `today`, shifted by `shift` periods. */
function periodRange(period: Period, shift: number, today: LocalDate, weekStartsOn: 0 | 1): DateRange {
  if (period === 'week') {
    const start = addDaysLocal(startOfWeekLocal(today, weekStartsOn), 7 * shift);
    return { from: start, to: endOfWeekLocal(start, weekStartsOn) };
  }
  if (period === 'month') {
    const start = addMonthsLocal(startOfMonthLocal(today), shift);
    return { from: start, to: endOfMonthLocal(start) };
  }
  const year = Number(addYearsLocal(today, shift).slice(0, 4));
  const y = String(year).padStart(4, '0');
  return { from: `${y}-01-01`, to: `${y}-12-31` };
}

/** "+7d", "-2w", "+1m", "3d" (sign optional), "+2 tygodnie". */
function parseOffset(s: string, today: LocalDate): DateRange | null {
  const m = /^([+-]?)(\d+)-?([a-z]*)$/.exec(s);
  if (!m) return null;
  const unit = m[3] ? own(OFFSET_UNITS, m[3]) : 'd';
  if (!unit || (!m[1] && !m[3])) return null; // a bare number is not a date
  const n = Number(m[2]) * (m[1] === '-' ? -1 : 1);
  switch (unit) {
    case 'd':
      return single(addDaysLocal(today, n));
    case 'w':
      return single(addDaysLocal(today, 7 * n));
    case 'm':
      return single(addMonthsLocal(today, n));
    case 'y':
      return single(addYearsLocal(today, n));
  }
}

/** "this-week", "next-month", "ten-tydzien", "przyszly-miesiac", "week" (= this week). */
function parsePeriod(s: string, today: LocalDate, weekStartsOn: 0 | 1): DateRange | null {
  const parts = s.split('-').filter(Boolean);
  if (parts.length === 1) {
    const period = own(PERIOD_WORDS, parts[0] ?? '');
    return period ? periodRange(period, 0, today, weekStartsOn) : null;
  }
  if (parts.length !== 2) return null;
  const [a = '', b = ''] = parts;
  // Both word orders: "this-week" and Polish "tydzien-ten" / "tydzien-nastepny".
  const which = own(WHICH_WORDS, a) ?? own(WHICH_WORDS, b);
  const period = own(PERIOD_WORDS, b) ?? own(PERIOD_WORDS, a);
  if (!which || !period) return null;
  const shift = which === 'this' ? 0 : which === 'next' ? 1 : -1;
  return periodRange(period, shift, today, weekStartsOn);
}

function resolveSingle(s: string, today: LocalDate, weekStartsOn: 0 | 1): DateRange | null {
  const dayOffset = own(DAY_WORDS, s);
  if (dayOffset !== undefined) return single(addDaysLocal(today, dayOffset));
  // "2026-10-15", also a date-time ("2026-10-15t09:00", or "-" where a space was) -> its date.
  const iso = /^(\d{4}-\d{2}-\d{2})(?:[t-]\d{1,2}:\d{2})?$/.exec(s);
  if (iso) return isLocalDate(iso[1] ?? '') ? single(iso[1] ?? '') : null;
  return parseOffset(s, today) ?? parsePeriod(s, today, weekStartsOn);
}

/** Lower-case, fold diacritics, and use "-" for spaces/underscores ("This week" -> "this-week"). */
function normalizeExpr(s: string): string {
  return s.trim().replace(/[\s_]+/g, '-');
}

/**
 * Resolve a query date expression relative to `now`:
 *
 * * `today` / `tomorrow` / `yesterday` (also `dziś`, `dzisiaj`, `jutro`, `wczoraj`,
 *   `pojutrze`) → that single day;
 * * `YYYY-MM-DD` (or a date-time, whose date is used) → that day;
 * * `+7d`, `-2w`, `+1m`, `+1y` (also Polish `t`/`r` units) → the single day at that offset;
 * * `this-week`, `next-week`, `last-week`, `this-month`, `next-month`, `last-month`,
 *   `this-year`, `next-year`, `last-year` and Polish `ten-tydzień`, `następny-tydzień`,
 *   `przyszły-tydzień`, `zeszły-tydzień`, `ten-miesiąc`, … → the whole period
 *   (weeks per `weekStartsOn`);
 * * `A..B` → from the start of A to the end of B (e.g. `today..+7d`);
 * * `overdue`/`zaległe`, `none`/`brak`, `any`/`dowolna` → the keyword.
 *
 * Case/diacritic-insensitive; `_` or spaces may replace `-`. Returns null if not understood,
 * if `now` is an invalid Date, or if the range would leave years 0000-9999 (`+9999y`).
 * A malformed `weekStartsOn` is treated as Monday.
 */
export function resolveDateExpr(expr: string, now: Date, weekStartsOn: 0 | 1 = 1): DateExprResult {
  const folded = foldText(expr).trim();
  if (!folded) return null;
  // Plain split + trim: a regex like /\s*\.\.\s*/ backtracks quadratically on long whitespace.
  const sides = folded.split('..').map((side) => side.trim());
  if (sides.length > 2) return null;
  const keyword = sides.length === 1 ? own(KEYWORDS, normalizeExpr(folded)) : undefined;
  if (keyword) return keyword;
  try {
    return resolveSides(sides, toLocalDate(now), normalizeWeekStart(weekStartsOn));
  } catch (e) {
    // Out-of-range arithmetic (or an invalid `now`) means the expression has no valid range.
    if (e instanceof RangeError) return null;
    throw e;
  }
}

/** Resolve one expression or the two sides of "A..B" (inclusive span of both). */
function resolveSides(sides: string[], today: LocalDate, weekStartsOn: 0 | 1): DateRange | null {
  const ranges = sides.map((side) => (side ? resolveSingle(normalizeExpr(side), today, weekStartsOn) : null));
  const [a, b] = ranges;
  if (!a) return null;
  if (sides.length === 1) return a;
  if (!b) return null;
  return a.from <= b.to ? { from: a.from, to: b.to } : { from: b.from, to: a.to };
}
