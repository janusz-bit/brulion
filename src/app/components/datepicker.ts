/**
 * Pure helpers behind <DatePicker>: month grid, keyboard movement, names (date-fns locales),
 * time input parsing and the natural-language preview.
 */
import { format } from 'date-fns';
import { enUS } from 'date-fns/locale/en-US';
import { pl } from 'date-fns/locale/pl';
import {
  addDaysLocal,
  addMonthsLocal,
  compareLocal,
  endOfWeekLocal,
  formatHm,
  formatYmd,
  joinDateTime,
  monthShortName,
  parseLocal,
  parseNaturalDate,
  splitLocal,
  startOfMonthLocal,
  startOfWeekLocal,
  toLocalDate,
  weekdayName,
  weekdayOf,
  weekdayShortName,
} from '@/core/dates';
import type { LocalDate, LocalDateTime, Locale } from '@/core/types';

export type DayMark = 'journal' | 'task' | 'due' | 'overdue' | 'reminder' | 'event' | 'done';
export type DayMarks = Readonly<Record<LocalDate, DayMark | readonly DayMark[]>>;

const DF_LOCALES = { pl, en: enUS } as const;

export interface MonthView {
  year: number;
  /** 1-12 */
  month: number;
}

export function viewOf(d: LocalDate | LocalDateTime): MonthView {
  return { year: Number(d.slice(0, 4)), month: Number(d.slice(5, 7)) };
}

export function shiftView(v: MonthView, months: number): MonthView {
  const total = v.year * 12 + (v.month - 1) + months;
  return { year: Math.floor(total / 12), month: (((total % 12) + 12) % 12) + 1 };
}

export function sameView(a: MonthView, b: MonthView): boolean {
  return a.year === b.year && a.month === b.month;
}

/** Six weeks (6×7 dates) covering the month, starting on `weekStartsOn`. */
export function monthGrid(view: MonthView, weekStartsOn: 0 | 1 = 1): LocalDate[][] {
  let d = startOfWeekLocal(formatYmd(view.year, view.month, 1), weekStartsOn);
  const weeks: LocalDate[][] = [];
  for (let w = 0; w < 6; w++) {
    const row: LocalDate[] = [];
    for (let i = 0; i < 7; i++) {
      row.push(d);
      d = addDaysLocal(d, 1);
    }
    weeks.push(row);
  }
  return weeks;
}

/** Clamp a date into [min, max] (either bound optional). */
export function clampDate(d: LocalDate, min?: LocalDate, max?: LocalDate): LocalDate {
  if (min && compareLocal(d, min) < 0) return min;
  if (max && compareLocal(d, max) > 0) return max;
  return d;
}

export function inRange(d: LocalDate, min?: LocalDate, max?: LocalDate): boolean {
  return !(min && compareLocal(d, min) < 0) && !(max && compareLocal(d, max) > 0);
}

/**
 * Grid keyboard movement (WAI-ARIA date picker pattern): arrows ±1 day / ±1 week,
 * PageUp/PageDown ±1 month (with Shift ±1 year), Home/End start/end of the week.
 * Returns null for other keys.
 */
export function moveFocusDate(
  date: LocalDate,
  key: string,
  opts: { weekStartsOn?: 0 | 1; shiftKey?: boolean; min?: LocalDate; max?: LocalDate } = {},
): LocalDate | null {
  const ws = opts.weekStartsOn ?? 1;
  let next: LocalDate;
  switch (key) {
    case 'ArrowLeft':
      next = addDaysLocal(date, -1);
      break;
    case 'ArrowRight':
      next = addDaysLocal(date, 1);
      break;
    case 'ArrowUp':
      next = addDaysLocal(date, -7);
      break;
    case 'ArrowDown':
      next = addDaysLocal(date, 7);
      break;
    case 'PageUp':
      next = addMonthsLocal(date, opts.shiftKey ? -12 : -1);
      break;
    case 'PageDown':
      next = addMonthsLocal(date, opts.shiftKey ? 12 : 1);
      break;
    case 'Home':
      next = startOfWeekLocal(date, ws);
      break;
    case 'End':
      next = endOfWeekLocal(date, ws);
      break;
    default:
      return null;
  }
  return clampDate(next, opts.min, opts.max);
}

function capitalize(s: string): string {
  return s ? s[0]!.toLocaleUpperCase() + s.slice(1) : s;
}

/** "Październik 2026" / "October 2026". */
export function monthTitle(view: MonthView, locale: Locale): string {
  return capitalize(format(new Date(view.year, view.month - 1, 1), 'LLLL yyyy', { locale: DF_LOCALES[locale] }));
}

/**
 * Weekday column headers starting at `weekStartsOn`: short ("pn", "wt"…, the same names as
 * the reminder's weekday toggles and the rest of the app) and full names.
 */
export function weekdayHeaders(locale: Locale, weekStartsOn: 0 | 1 = 1): { short: string; long: string; day: number }[] {
  const out: { short: string; long: string; day: number }[] = [];
  for (let i = 0; i < 7; i++) {
    const day = (weekStartsOn + i) % 7;
    out.push({ day, short: weekdayShortName(day, locale), long: weekdayName(day, locale) });
  }
  return out;
}

/** Full accessible day name: "czwartek, 15 października 2026" / "Thursday, October 15, 2026". */
export function dayLabel(d: LocalDate, locale: Locale): string {
  const date = parseLocal(d);
  return locale === 'pl'
    ? format(date, 'EEEE, d MMMM yyyy', { locale: pl })
    : format(date, 'EEEE, MMMM d, yyyy', { locale: enUS });
}

/**
 * Compact unambiguous date(-time) for previews: "czw, 15 paź 09:00" / "Thu, Oct 15 09:00";
 * the year is added when it differs from `now`'s.
 */
export function formatPreview(value: LocalDate | LocalDateTime, locale: Locale, now: Date = new Date()): string {
  const sp = splitLocal(value);
  if (!sp) return value;
  const y = Number(sp.date.slice(0, 4));
  const m = Number(sp.date.slice(5, 7));
  const d = Number(sp.date.slice(8, 10));
  const wd = weekdayShortName(weekdayOf(sp.date), locale);
  const mon = monthShortName(m, locale);
  const showYear = y !== now.getFullYear();
  const date =
    locale === 'pl' ? `${wd}, ${d} ${mon}${showYear ? ` ${y}` : ''}` : `${wd}, ${mon} ${d}${showYear ? `, ${y}` : ''}`;
  return sp.time ? `${date} ${sp.time}` : date;
}

/**
 * Parse a typed time: "9" → 09:00, "9:05", "9.30", "18 30", "18h30", "930" / "0930" → 09:30,
 * "6pm" → 18:00, "6:30 am", "12am" → 00:00. Returns "HH:mm" or null.
 */
export function parseTimeInput(input: string): string | null {
  const s = input.trim().toLowerCase().replace(/\s+/g, ' ');
  if (!s) return null;
  const m = /^(\d{1,2})(?:[:.,h ](\d{2}))?\s*(am|pm|a\.m\.|p\.m\.)?$/.exec(s) ?? /^(\d{1,2})(\d{2})\s*(am|pm)?$/.exec(s);
  if (!m) return null;
  let h = Number(m[1]);
  const min = m[2] === undefined ? 0 : Number(m[2]);
  const mer = m[3]?.replace(/\./g, '');
  if (mer) {
    if (h < 1 || h > 12) return null;
    if (mer === 'am') h = h === 12 ? 0 : h;
    else h = h === 12 ? 12 : h + 12;
  }
  if (h > 23 || min > 59) return null;
  return formatHm(h, min);
}

/** Step a "HH:mm" time by `minutes` on a grid of that size (09:07 +15 → 09:15), wrapping at midnight. */
export function stepTime(time: string | undefined, minutes: number, fallback = '09:00'): string {
  const t = time && /^\d{2}:\d{2}$/.test(time) ? time : fallback;
  const total = Number(t.slice(0, 2)) * 60 + Number(t.slice(3, 5));
  const step = Math.abs(minutes) || 1;
  let next: number;
  if (total % step === 0) next = total + minutes;
  else next = minutes > 0 ? Math.ceil(total / step) * step : Math.floor(total / step) * step;
  next = ((next % 1440) + 1440) % 1440;
  return formatHm(Math.floor(next / 60), next % 60);
}

export interface NaturalPreview {
  /** What applying the input would set. */
  value: LocalDate | LocalDateTime;
  date: LocalDate;
  time?: string;
  /** "czw, 15 paź 09:00" */
  label: string;
  /** The recognised part of the input. */
  matched: string;
  /** True when parts of the input were ignored ("jutro coś tam"). */
  partial: boolean;
}

export interface NaturalPreviewOptions {
  locale?: Locale;
  weekStartsOn?: 0 | 1;
  /** Picker shows a time: a phrase without a time gets `defaultTime`. */
  withTime?: boolean;
  /** Time used when the phrase has none (only with `withTime`). Omit to keep a bare date. */
  defaultTime?: string;
}

/**
 * Interpret a natural-language date for the picker's live preview ("jutro o 9" →
 * "→ czw, 15 paź 09:00"). Without `withTime` a recognised time is dropped.
 */
export function naturalPreview(input: string, now: Date, opts: NaturalPreviewOptions = {}): NaturalPreview | null {
  const text = input.trim();
  if (!text) return null;
  const locale = opts.locale ?? 'pl';
  const r = parseNaturalDate(text, now, { locale, weekStartsOn: opts.weekStartsOn ?? 1 });
  if (!r) return null;
  let time: string | undefined;
  if (opts.withTime) time = r.time ?? opts.defaultTime;
  const value = joinDateTime(r.date, time);
  const sp = splitLocal(value);
  const leftover = (text.slice(0, r.index) + text.slice(r.index + r.matched.length)).replace(/[\s,.;:!?-]+/g, '');
  return {
    value,
    date: r.date,
    time: sp?.time,
    label: formatPreview(value, locale, now),
    matched: r.matched,
    partial: leftover.length > 0,
  };
}

/** Marks for a day as an array. */
export function marksFor(marks: DayMarks | undefined, d: LocalDate): readonly DayMark[] {
  const m = marks?.[d];
  if (!m) return [];
  return typeof m === 'string' ? [m] : m;
}

/** Today's date in local time (wrapper for tests). */
export function todayOf(now: Date): LocalDate {
  return toLocalDate(now);
}

/** First day of the month containing `d`. */
export function monthStart(d: LocalDate): LocalDate {
  return startOfMonthLocal(d);
}
