/**
 * Local (time-zone-less) date helpers.
 *
 * `LocalDate` is "YYYY-MM-DD" and `LocalDateTime` is "YYYY-MM-DDTHH:mm" (wall-clock time).
 * Files may also contain the space form "YYYY-MM-DD HH:mm"; {@link normalizeLocalDateTime}
 * and {@link parseLocal} accept it.
 *
 * Pure calendar arithmetic (adding days/months, weekdays) is done on proleptic Gregorian
 * date parts via UTC math, so daylight-saving transitions can never shift a date. ISO
 * strings are never handed to `new Date(string)` (that would be parsed as UTC).
 */
import type { LocalDate, LocalDateTime } from '../types';

/** Date parts of a LocalDate / LocalDateTime. Month is 1-12. */
export interface LocalParts {
  year: number;
  month: number;
  day: number;
  /** Present only for date-times. */
  hour?: number;
  minute?: number;
}

/** `date` + optional `time` ("HH:mm") split of a local date or date-time. */
export interface SplitLocal {
  date: LocalDate;
  time?: string;
}

const DATE_RE = /^(\d{4})-(\d{2})-(\d{2})$/;
const DATETIME_RE = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})$/;
/** Lenient date-time: "T" or space separator, 1-2 digit hour, optional seconds. */
const LENIENT_DATETIME_RE = /^(\d{4})-(\d{2})-(\d{2})(?:[T ]+|\s+)(\d{1,2}):(\d{2})(?::\d{2}(?:\.\d+)?)?$/;
const TIME_RE = /^(\d{1,2}):(\d{2})$/;

const MS_PER_DAY = 86_400_000;

/** Left-pad a non-negative integer with zeros. */
export function pad(n: number, width = 2): string {
  return String(n).padStart(width, '0');
}

/** True for Gregorian leap years. */
export function isLeapYear(year: number): boolean {
  return (year % 4 === 0 && year % 100 !== 0) || year % 400 === 0;
}

/** Number of days in `month` (1-12) of `year`. */
export function daysInMonth(year: number, month: number): number {
  if (month === 2) return isLeapYear(year) ? 29 : 28;
  return month === 4 || month === 6 || month === 9 || month === 11 ? 30 : 31;
}

/** True when year/month/day form an existing calendar date (years 0-9999). */
export function isValidYmd(year: number, month: number, day: number): boolean {
  return (
    Number.isInteger(year) &&
    Number.isInteger(month) &&
    Number.isInteger(day) &&
    year >= 0 &&
    year <= 9999 &&
    month >= 1 &&
    month <= 12 &&
    day >= 1 &&
    day <= daysInMonth(year, month)
  );
}

/** True when hour/minute form a valid wall-clock time (00:00-23:59). */
export function isValidHm(hour: number, minute: number): boolean {
  return Number.isInteger(hour) && Number.isInteger(minute) && hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59;
}

/** Format date parts as "YYYY-MM-DD". Does not validate. */
export function formatYmd(year: number, month: number, day: number): LocalDate {
  return `${pad(year, 4)}-${pad(month)}-${pad(day)}`;
}

/** Format a time as "HH:mm". Does not validate. */
export function formatHm(hour: number, minute: number): string {
  return `${pad(hour)}:${pad(minute)}`;
}

/** True for a valid "HH:mm" (two-digit hour) time string. */
export function isValidTime(s: string): boolean {
  if (!/^\d{2}:\d{2}$/.test(s)) return false;
  return isValidHm(Number(s.slice(0, 2)), Number(s.slice(3, 5)));
}

/** Normalise "9:05" / "09:05" to "09:05"; null when invalid. */
export function normalizeTime(s: string): string | null {
  const m = TIME_RE.exec(s.trim());
  if (!m) return null;
  const h = Number(m[1]);
  const min = Number(m[2]);
  return isValidHm(h, min) ? formatHm(h, min) : null;
}

/** True for a valid "YYYY-MM-DD" (strict format, existing calendar day). */
export function isLocalDate(s: string): boolean {
  const m = DATE_RE.exec(s);
  return !!m && isValidYmd(Number(m[1]), Number(m[2]), Number(m[3]));
}

/** True for a valid "YYYY-MM-DDTHH:mm" (strict: `T` separator, two-digit hour). */
export function isLocalDateTime(s: string): boolean {
  const m = DATETIME_RE.exec(s);
  return (
    !!m && isValidYmd(Number(m[1]), Number(m[2]), Number(m[3])) && isValidHm(Number(m[4]), Number(m[5]))
  );
}

/**
 * Normalise a date-time written as "YYYY-MM-DD HH:mm", "YYYY-MM-DDTHH:mm", "YYYY-MM-DD H:mm"
 * or with seconds ("…T09:00:00", truncated) to the canonical "YYYY-MM-DDTHH:mm".
 * Returns null for anything else (including bare dates).
 */
export function normalizeLocalDateTime(s: string): LocalDateTime | null {
  const m = LENIENT_DATETIME_RE.exec(s.trim());
  if (!m) return null;
  const [y, mo, d, h, min] = [Number(m[1]), Number(m[2]), Number(m[3]), Number(m[4]), Number(m[5])];
  if (!isValidYmd(y, mo, d) || !isValidHm(h, min)) return null;
  return `${formatYmd(y, mo, d)}T${formatHm(h, min)}`;
}

/**
 * Parse a LocalDate or LocalDateTime (either separator) into parts; null when malformed
 * or not an existing date/time.
 */
export function localParts(s: string): LocalParts | null {
  const t = s.trim();
  const dm = DATE_RE.exec(t);
  if (dm) {
    const [y, mo, d] = [Number(dm[1]), Number(dm[2]), Number(dm[3])];
    return isValidYmd(y, mo, d) ? { year: y, month: mo, day: d } : null;
  }
  const norm = normalizeLocalDateTime(t);
  if (!norm) return null;
  return {
    year: Number(norm.slice(0, 4)),
    month: Number(norm.slice(5, 7)),
    day: Number(norm.slice(8, 10)),
    hour: Number(norm.slice(11, 13)),
    minute: Number(norm.slice(14, 16)),
  };
}

/** Split a local date or date-time into `{ date, time? }`; null when malformed. */
export function splitLocal(s: string): SplitLocal | null {
  const p = localParts(s);
  if (!p) return null;
  const date = formatYmd(p.year, p.month, p.day);
  return p.hour === undefined ? { date } : { date, time: formatHm(p.hour, p.minute ?? 0) };
}

/** Date part of a local date or date-time ("2026-10-15T09:00" -> "2026-10-15"); null if malformed. */
export function datePart(s: string): LocalDate | null {
  return splitLocal(s)?.date ?? null;
}

/** Time part ("HH:mm") of a local date-time; undefined for bare dates or malformed input. */
export function timePart(s: string): string | undefined {
  return splitLocal(s)?.time;
}

/**
 * Combine a date with an optional "HH:mm" (or "H:mm") time. Without a (valid) time the bare
 * date is returned.
 */
export function joinDateTime(date: LocalDate, time?: string): LocalDate | LocalDateTime {
  if (time === undefined) return date;
  const t = normalizeTime(time);
  return t ? `${date}T${t}` : date;
}

/** "YYYY-MM-DDTHH:mm" -> "YYYY-MM-DD HH:mm" (the form written into note files); dates unchanged. */
export function toFileDateTime(s: LocalDate | LocalDateTime): string {
  const sp = splitLocal(s);
  if (!sp) return s;
  return sp.time ? `${sp.date} ${sp.time}` : sp.date;
}

function assertValidDate(d: Date): void {
  if (Number.isNaN(d.getTime())) throw new RangeError('Invalid Date');
}

/** Local calendar date of a Date ("YYYY-MM-DD"). Throws RangeError for an invalid Date. */
export function toLocalDate(d: Date): LocalDate {
  assertValidDate(d);
  return formatYmd(d.getFullYear(), d.getMonth() + 1, d.getDate());
}

/** Local wall-clock date-time of a Date ("YYYY-MM-DDTHH:mm", seconds dropped). Throws for invalid Dates. */
export function toLocalDateTime(d: Date): LocalDateTime {
  assertValidDate(d);
  return `${toLocalDate(d)}T${formatHm(d.getHours(), d.getMinutes())}`;
}

/** Today's local date (defaults to the current time). */
export function todayLocal(now: Date = new Date()): LocalDate {
  return toLocalDate(now);
}

/**
 * Parse a LocalDate ("YYYY-MM-DD", local midnight) or LocalDateTime ("YYYY-MM-DDTHH:mm" or
 * "YYYY-MM-DD HH:mm") into a Date in the local time zone. Malformed input yields an
 * Invalid Date (check with `Number.isNaN(d.getTime())`).
 */
export function parseLocal(s: LocalDate | LocalDateTime): Date {
  const p = localParts(s);
  if (!p) return new Date(Number.NaN);
  const d = new Date(2000, 0, 1, p.hour ?? 0, p.minute ?? 0, 0, 0);
  // setFullYear keeps years < 100 literal (the Date constructor maps them to 19xx).
  d.setFullYear(p.year, p.month - 1, p.day);
  return d;
}

// ---------------------------------------------------------------------------
// Calendar arithmetic (DST-proof: computed on UTC day numbers)
// ---------------------------------------------------------------------------

/** Day number (days since 1970-01-01) of a calendar date. */
export function dayNumber(year: number, month: number, day: number): number {
  const d = new Date(0);
  d.setUTCFullYear(year, month - 1, day);
  d.setUTCHours(0, 0, 0, 0);
  return Math.round(d.getTime() / MS_PER_DAY);
}

/** Day number of 0000-01-01, the first supported date. */
const MIN_DAY = dayNumber(0, 1, 1);
/** Day number of 9999-12-31, the last supported date. */
const MAX_DAY = dayNumber(9999, 12, 31);

/**
 * Calendar date for a day number (inverse of {@link dayNumber}). Throws RangeError for
 * non-integers and for dates outside 0000-01-01 … 9999-12-31 (a LocalDate has 4 year digits).
 */
export function fromDayNumber(n: number): LocalDate {
  if (!Number.isInteger(n) || n < MIN_DAY || n > MAX_DAY) throw new RangeError(`Date out of range (day ${n})`);
  const d = new Date(n * MS_PER_DAY);
  return formatYmd(d.getUTCFullYear(), d.getUTCMonth() + 1, d.getUTCDate());
}

/** Integer part of a finite offset; throws RangeError for NaN / ±Infinity. */
function finiteOffset(n: number): number {
  if (!Number.isFinite(n)) throw new RangeError(`Invalid offset: ${n}`);
  return Math.trunc(n);
}

/**
 * Coerce a possibly malformed week-start setting (e.g. read from a synced config file) to
 * 0 (Sunday) or 1 (Monday, the default for anything that is not 0).
 */
export function normalizeWeekStart(weekStartsOn: unknown): 0 | 1 {
  return weekStartsOn === 0 || weekStartsOn === '0' ? 0 : 1;
}

function partsOrThrow(s: string): LocalParts {
  const p = localParts(s);
  if (!p) throw new RangeError(`Invalid local date: "${s}"`);
  return p;
}

function withSameTime(original: LocalParts, date: LocalDate): LocalDate | LocalDateTime {
  return original.hour === undefined ? date : `${date}T${formatHm(original.hour, original.minute ?? 0)}`;
}

/**
 * Add `n` calendar days (may be negative). Accepts a date or date-time; a date-time keeps
 * its time ("2026-10-14T09:00" + 1 -> "2026-10-15T09:00"). Throws RangeError on malformed
 * input, a non-finite `n`, or a result outside years 0000-9999.
 */
export function addDaysLocal(d: LocalDate | LocalDateTime, n: number): LocalDate | LocalDateTime {
  const p = partsOrThrow(d);
  return withSameTime(p, fromDayNumber(dayNumber(p.year, p.month, p.day) + finiteOffset(n)));
}

/**
 * Add `n` months, clamping the day to the target month's length
 * ("2026-01-31" + 1 -> "2026-02-28"). Keeps the time of a date-time. Throws RangeError like
 * {@link addDaysLocal}.
 */
export function addMonthsLocal(d: LocalDate | LocalDateTime, n: number): LocalDate | LocalDateTime {
  const p = partsOrThrow(d);
  const total = p.year * 12 + (p.month - 1) + finiteOffset(n);
  const year = Math.floor(total / 12);
  if (year < 0 || year > 9999) throw new RangeError(`Date out of range (year ${year})`);
  const month = total - year * 12 + 1;
  const day = Math.min(p.day, daysInMonth(year, month));
  return withSameTime(p, formatYmd(year, month, day));
}

/** Add `n` years ("2028-02-29" + 1 -> "2029-02-28"). Keeps the time of a date-time. Throws like {@link addMonthsLocal}. */
export function addYearsLocal(d: LocalDate | LocalDateTime, n: number): LocalDate | LocalDateTime {
  return addMonthsLocal(d, finiteOffset(n) * 12);
}

/**
 * Add `n` minutes of wall-clock time to a date-time (no DST adjustment: 23:30 + 60 min is
 * always 00:30 the next day). A bare date is treated as 00:00. Returns a LocalDateTime.
 * Throws RangeError like {@link addDaysLocal}.
 */
export function addMinutesLocal(d: LocalDate | LocalDateTime, n: number): LocalDateTime {
  const p = partsOrThrow(d);
  const total = dayNumber(p.year, p.month, p.day) * 1440 + (p.hour ?? 0) * 60 + (p.minute ?? 0) + finiteOffset(n);
  const day = Math.floor(total / 1440);
  const rest = total - day * 1440;
  return `${fromDayNumber(day)}T${formatHm(Math.floor(rest / 60), rest % 60)}`;
}

/** Calendar days from `a` to `b` (positive when b is later). Time parts are ignored. */
export function diffDaysLocal(a: LocalDate | LocalDateTime, b: LocalDate | LocalDateTime): number {
  const pa = partsOrThrow(a);
  const pb = partsOrThrow(b);
  return dayNumber(pb.year, pb.month, pb.day) - dayNumber(pa.year, pa.month, pa.day);
}

/** Weekday of a local date: 0 = Sunday … 6 = Saturday (same as `Date#getDay`). */
export function weekdayOf(d: LocalDate | LocalDateTime): number {
  const p = partsOrThrow(d);
  // 1970-01-01 was a Thursday (4).
  return (((dayNumber(p.year, p.month, p.day) + 4) % 7) + 7) % 7;
}

/**
 * First day of the week containing `d` (weekStartsOn: 0 = Sunday, 1 = Monday; any other
 * value is treated as Monday, see {@link normalizeWeekStart}).
 */
export function startOfWeekLocal(d: LocalDate | LocalDateTime, weekStartsOn: 0 | 1 = 1): LocalDate {
  const date = partsToDate(partsOrThrow(d));
  const offset = (weekdayOf(date) - normalizeWeekStart(weekStartsOn) + 7) % 7;
  return addDaysLocal(date, -offset);
}

/** Last day of the week containing `d`. */
export function endOfWeekLocal(d: LocalDate | LocalDateTime, weekStartsOn: 0 | 1 = 1): LocalDate {
  return addDaysLocal(startOfWeekLocal(d, weekStartsOn), 6);
}

/** First day of the month containing `d`. */
export function startOfMonthLocal(d: LocalDate | LocalDateTime): LocalDate {
  const p = partsOrThrow(d);
  return formatYmd(p.year, p.month, 1);
}

/** Last day of the month containing `d`. */
export function endOfMonthLocal(d: LocalDate | LocalDateTime): LocalDate {
  const p = partsOrThrow(d);
  return formatYmd(p.year, p.month, daysInMonth(p.year, p.month));
}

function partsToDate(p: LocalParts): LocalDate {
  return formatYmd(p.year, p.month, p.day);
}

/**
 * Compare two local dates / date-times chronologically (for sorting): negative when `a` is
 * earlier. Either separator is accepted. A bare date sorts **before** any date-time on the
 * same day. Malformed strings sort after all valid ones (then by plain string order).
 */
export function compareLocal(a: string, b: string): number {
  const pa = splitLocal(a);
  const pb = splitLocal(b);
  if (!pa || !pb) {
    if (pa) return -1;
    if (pb) return 1;
    return a < b ? -1 : a > b ? 1 : 0;
  }
  if (pa.date !== pb.date) return pa.date < pb.date ? -1 : 1;
  if (pa.time === pb.time) return 0;
  if (pa.time === undefined) return -1;
  if (pb.time === undefined) return 1;
  return pa.time < pb.time ? -1 : 1;
}
