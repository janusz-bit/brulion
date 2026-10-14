/**
 * Human-friendly date formatting for the UI ("dziś 09:00", "jutro", "pt 17 paź").
 */
import type { LocalDate, LocalDateTime, Locale } from '../types';
import { diffDaysLocal, localParts, splitLocal, toLocalDate, weekdayOf } from './local';
import { monthShortName, own, weekdayShortName } from './names';

const RELATIVE: Record<Locale, Record<string, string>> = {
  pl: { [-1]: 'wczoraj', 0: 'dziś', 1: 'jutro' },
  en: { [-1]: 'yesterday', 0: 'today', 1: 'tomorrow' },
};

/**
 * Date without the relative words: "pt 17 paź" / "Fri, Oct 17"; with year when not `nowYear`.
 * A date-time's time is ignored; malformed input is returned unchanged.
 */
export function formatDateShort(date: LocalDate, locale: Locale, nowYear?: number): string {
  const p = localParts(date);
  if (!p) return date;
  const { year, month, day } = p;
  const mon = monthShortName(month, locale);
  const sameYear = nowYear === undefined || nowYear === year;
  if (locale === 'pl') {
    return sameYear ? `${weekdayShortName(weekdayOf(date), 'pl')} ${day} ${mon}` : `${day} ${mon} ${year}`;
  }
  return sameYear ? `${weekdayShortName(weekdayOf(date), 'en')}, ${mon} ${day}` : `${mon} ${day}, ${year}`;
}

/**
 * Format a date or date-time for display relative to `now`:
 *
 * * yesterday / today / tomorrow → "wczoraj" / "dziś" / "jutro" ("yesterday", "today", "tomorrow");
 * * other days in the current year → "pt 17 paź" ("Fri, Oct 17");
 * * other years → "17 paź 2027" ("Oct 17, 2027");
 * * date-times get the 24-hour time appended: "dziś 09:00", "pt 17 paź 18:30".
 *
 * Malformed input is returned unchanged.
 */
export function formatDateHuman(d: LocalDate | LocalDateTime, locale: Locale, now: Date = new Date()): string {
  const sp = splitLocal(d);
  if (!sp) return d;
  // Unknown locales (e.g. a malformed setting) fall back to English like the other formatters.
  const loc: Locale = locale === 'pl' ? 'pl' : 'en';
  const validNow = !Number.isNaN(now.getTime());
  const rel = validNow ? own(RELATIVE[loc], String(diffDaysLocal(toLocalDate(now), sp.date))) : undefined;
  // Without a valid "now" the year is always shown (nowYear -1 never matches).
  const datePart = rel ?? formatDateShort(sp.date, loc, validNow ? now.getFullYear() : -1);
  return sp.time ? `${datePart} ${sp.time}` : datePart;
}
