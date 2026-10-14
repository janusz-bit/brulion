import { afterEach, describe, expect, it } from 'vitest';
import {
  addDaysLocal,
  addMinutesLocal,
  addMonthsLocal,
  addYearsLocal,
  compareLocal,
  datePart,
  daysInMonth,
  diffDaysLocal,
  endOfMonthLocal,
  endOfWeekLocal,
  isLeapYear,
  isLocalDate,
  isLocalDateTime,
  isValidTime,
  joinDateTime,
  localParts,
  normalizeLocalDateTime,
  normalizeTime,
  parseLocal,
  splitLocal,
  startOfMonthLocal,
  startOfWeekLocal,
  timePart,
  toFileDateTime,
  toLocalDate,
  toLocalDateTime,
  todayLocal,
  weekdayOf,
} from './index';

describe('toLocalDate / toLocalDateTime / todayLocal', () => {
  it('formats local wall-clock values', () => {
    const d = new Date(2026, 9, 14, 9, 5, 59);
    expect(toLocalDate(d)).toBe('2026-10-14');
    expect(toLocalDateTime(d)).toBe('2026-10-14T09:05');
    expect(todayLocal(d)).toBe('2026-10-14');
  });
  it('pads single-digit months and days', () => {
    expect(toLocalDate(new Date(2026, 0, 5))).toBe('2026-01-05');
    expect(toLocalDateTime(new Date(2026, 0, 5, 0, 0))).toBe('2026-01-05T00:00');
  });
  it('late evening stays on the same local day (no UTC shift)', () => {
    expect(toLocalDate(new Date(2026, 11, 31, 23, 59))).toBe('2026-12-31');
  });
  it('todayLocal defaults to the current date', () => {
    expect(isLocalDate(todayLocal())).toBe(true);
  });
  it('throws for an invalid Date', () => {
    expect(() => toLocalDate(new Date(Number.NaN))).toThrow(RangeError);
    expect(() => toLocalDateTime(new Date('nonsense'))).toThrow(RangeError);
  });
});

describe('parseLocal', () => {
  it('parses a date as local midnight', () => {
    const d = parseLocal('2026-10-14');
    expect([d.getFullYear(), d.getMonth(), d.getDate(), d.getHours(), d.getMinutes()]).toEqual([2026, 9, 14, 0, 0]);
  });
  it('parses date-times with T or space', () => {
    for (const s of ['2026-10-14T18:30', '2026-10-14 18:30']) {
      const d = parseLocal(s);
      expect([d.getFullYear(), d.getMonth(), d.getDate(), d.getHours(), d.getMinutes()]).toEqual([2026, 9, 14, 18, 30]);
    }
  });
  it('round-trips with toLocalDate / toLocalDateTime', () => {
    expect(toLocalDate(parseLocal('2028-02-29'))).toBe('2028-02-29');
    expect(toLocalDateTime(parseLocal('2026-12-31T23:59'))).toBe('2026-12-31T23:59');
  });
  it('keeps years below 100 literal', () => {
    expect(parseLocal('0099-03-01').getFullYear()).toBe(99);
  });
  it('returns an Invalid Date for malformed input', () => {
    for (const s of ['', '2026-02-30', '2026-13-01', '14.10.2026', '2026-10-14T25:00', 'jutro']) {
      expect(Number.isNaN(parseLocal(s).getTime())).toBe(true);
    }
  });
});

describe('isLocalDate / isLocalDateTime', () => {
  it('validates dates strictly', () => {
    expect(isLocalDate('2026-10-14')).toBe(true);
    expect(isLocalDate('2028-02-29')).toBe(true);
    expect(isLocalDate('2026-02-29')).toBe(false);
    expect(isLocalDate('2100-02-29')).toBe(false);
    expect(isLocalDate('2000-02-29')).toBe(true);
    expect(isLocalDate('2026-13-01')).toBe(false);
    expect(isLocalDate('2026-00-10')).toBe(false);
    expect(isLocalDate('2026-04-31')).toBe(false);
    expect(isLocalDate('2026-1-01')).toBe(false);
    expect(isLocalDate(' 2026-10-14')).toBe(false);
    expect(isLocalDate('2026-10-14T09:00')).toBe(false);
    expect(isLocalDate('')).toBe(false);
  });
  it('validates date-times strictly (T separator, HH:mm)', () => {
    expect(isLocalDateTime('2026-10-14T09:00')).toBe(true);
    expect(isLocalDateTime('2026-10-14T23:59')).toBe(true);
    expect(isLocalDateTime('2026-10-14 09:00')).toBe(false);
    expect(isLocalDateTime('2026-10-14T9:00')).toBe(false);
    expect(isLocalDateTime('2026-10-14T24:00')).toBe(false);
    expect(isLocalDateTime('2026-10-14T09:60')).toBe(false);
    expect(isLocalDateTime('2026-02-30T09:00')).toBe(false);
    expect(isLocalDateTime('2026-10-14')).toBe(false);
  });
});

describe('normalizeLocalDateTime', () => {
  it('accepts space, T, single-digit hours and seconds', () => {
    expect(normalizeLocalDateTime('2026-10-15 09:00')).toBe('2026-10-15T09:00');
    expect(normalizeLocalDateTime('2026-10-15T09:00')).toBe('2026-10-15T09:00');
    expect(normalizeLocalDateTime('2026-10-15 9:05')).toBe('2026-10-15T09:05');
    expect(normalizeLocalDateTime('2026-10-15T09:00:59')).toBe('2026-10-15T09:00');
    expect(normalizeLocalDateTime('  2026-10-15   18:30 ')).toBe('2026-10-15T18:30');
  });
  it('rejects bare dates and invalid values', () => {
    expect(normalizeLocalDateTime('2026-10-15')).toBeNull();
    expect(normalizeLocalDateTime('2026-10-15 24:00')).toBeNull();
    expect(normalizeLocalDateTime('2026-02-30 10:00')).toBeNull();
    expect(normalizeLocalDateTime('15.10.2026 10:00')).toBeNull();
    expect(normalizeLocalDateTime('')).toBeNull();
  });
});

describe('split / join helpers', () => {
  it('splits dates and date-times', () => {
    expect(splitLocal('2026-10-15')).toEqual({ date: '2026-10-15' });
    expect(splitLocal('2026-10-15 9:30')).toEqual({ date: '2026-10-15', time: '09:30' });
    expect(splitLocal('bad')).toBeNull();
    expect(datePart('2026-10-15T09:30')).toBe('2026-10-15');
    expect(datePart('x')).toBeNull();
    expect(timePart('2026-10-15T09:30')).toBe('09:30');
    expect(timePart('2026-10-15')).toBeUndefined();
    expect(localParts('2026-10-15T09:30')).toEqual({ year: 2026, month: 10, day: 15, hour: 9, minute: 30 });
  });
  it('joins date and time', () => {
    expect(joinDateTime('2026-10-15')).toBe('2026-10-15');
    expect(joinDateTime('2026-10-15', '9:30')).toBe('2026-10-15T09:30');
    expect(joinDateTime('2026-10-15', '25:00')).toBe('2026-10-15');
  });
  it('formats for files with a space', () => {
    expect(toFileDateTime('2026-10-15T09:30')).toBe('2026-10-15 09:30');
    expect(toFileDateTime('2026-10-15')).toBe('2026-10-15');
    expect(toFileDateTime('garbage')).toBe('garbage');
  });
  it('validates and normalises times', () => {
    expect(isValidTime('09:30')).toBe(true);
    expect(isValidTime('9:30')).toBe(false);
    expect(isValidTime('24:00')).toBe(false);
    expect(normalizeTime('9:30')).toBe('09:30');
    expect(normalizeTime('23:59')).toBe('23:59');
    expect(normalizeTime('23:60')).toBeNull();
    expect(normalizeTime('rano')).toBeNull();
  });
  it('knows month lengths and leap years', () => {
    expect(daysInMonth(2026, 2)).toBe(28);
    expect(daysInMonth(2028, 2)).toBe(29);
    expect(daysInMonth(2026, 4)).toBe(30);
    expect(daysInMonth(2026, 12)).toBe(31);
    expect(isLeapYear(1900)).toBe(false);
    expect(isLeapYear(2000)).toBe(true);
  });
});

describe('calendar arithmetic', () => {
  it('adds days across months and years', () => {
    expect(addDaysLocal('2026-10-14', 1)).toBe('2026-10-15');
    expect(addDaysLocal('2026-10-31', 1)).toBe('2026-11-01');
    expect(addDaysLocal('2026-12-31', 1)).toBe('2027-01-01');
    expect(addDaysLocal('2027-01-01', -1)).toBe('2026-12-31');
    expect(addDaysLocal('2028-02-28', 1)).toBe('2028-02-29');
    expect(addDaysLocal('2026-02-28', 1)).toBe('2026-03-01');
    expect(addDaysLocal('2026-10-14', 0)).toBe('2026-10-14');
    expect(addDaysLocal('2026-10-14', 365)).toBe('2027-10-14');
  });
  it('is not affected by DST transitions', () => {
    // EU DST changes on the last Sundays of March and October.
    expect(addDaysLocal('2026-03-28', 1)).toBe('2026-03-29');
    expect(addDaysLocal('2026-03-29', 1)).toBe('2026-03-30');
    expect(addDaysLocal('2026-10-25', 1)).toBe('2026-10-26');
  });
  it('keeps the time of date-times', () => {
    expect(addDaysLocal('2026-10-14T09:00', 2)).toBe('2026-10-16T09:00');
  });
  it('throws on malformed input', () => {
    expect(() => addDaysLocal('14.10.2026', 1)).toThrow(RangeError);
  });
  it('adds months with clamping', () => {
    expect(addMonthsLocal('2026-01-31', 1)).toBe('2026-02-28');
    expect(addMonthsLocal('2028-01-31', 1)).toBe('2028-02-29');
    expect(addMonthsLocal('2026-10-14', 3)).toBe('2027-01-14');
    expect(addMonthsLocal('2026-01-15', -1)).toBe('2025-12-15');
    expect(addMonthsLocal('2026-03-31', -1)).toBe('2026-02-28');
    expect(addMonthsLocal('2026-10-31T08:15', 1)).toBe('2026-11-30T08:15');
  });
  it('adds years with leap-day clamping', () => {
    expect(addYearsLocal('2028-02-29', 1)).toBe('2029-02-28');
    expect(addYearsLocal('2028-02-29', 4)).toBe('2032-02-29');
    expect(addYearsLocal('2026-10-14', -1)).toBe('2025-10-14');
  });
  it('adds wall-clock minutes', () => {
    expect(addMinutesLocal('2026-10-14T23:30', 60)).toBe('2026-10-15T00:30');
    expect(addMinutesLocal('2026-10-14T00:10', -20)).toBe('2026-10-13T23:50');
    expect(addMinutesLocal('2026-10-14', 90)).toBe('2026-10-14T01:30');
    expect(addMinutesLocal('2026-12-31 23:59', 1)).toBe('2027-01-01T00:00');
  });
  it('computes day differences', () => {
    expect(diffDaysLocal('2026-10-14', '2026-10-15')).toBe(1);
    expect(diffDaysLocal('2026-10-15', '2026-10-14')).toBe(-1);
    expect(diffDaysLocal('2026-01-01', '2027-01-01')).toBe(365);
    expect(diffDaysLocal('2026-10-14T23:59', '2026-10-15T00:01')).toBe(1);
  });
  it('computes weekdays (0 = Sunday)', () => {
    expect(weekdayOf('2026-10-14')).toBe(3);
    expect(weekdayOf('2026-10-18')).toBe(0);
    expect(weekdayOf('2026-10-17T12:00')).toBe(6);
    expect(weekdayOf('1970-01-01')).toBe(4);
    expect(weekdayOf('1969-12-31')).toBe(3);
    expect(weekdayOf('2000-02-29')).toBe(2);
  });
  it('finds week boundaries for both week starts', () => {
    expect(startOfWeekLocal('2026-10-14')).toBe('2026-10-12');
    expect(endOfWeekLocal('2026-10-14')).toBe('2026-10-18');
    expect(startOfWeekLocal('2026-10-18', 1)).toBe('2026-10-12');
    expect(startOfWeekLocal('2026-10-14', 0)).toBe('2026-10-11');
    expect(endOfWeekLocal('2026-10-14', 0)).toBe('2026-10-17');
    expect(startOfWeekLocal('2026-10-18', 0)).toBe('2026-10-18');
    expect(startOfWeekLocal('2026-10-12', 1)).toBe('2026-10-12');
  });
  it('finds month boundaries', () => {
    expect(startOfMonthLocal('2026-10-14T09:00')).toBe('2026-10-01');
    expect(endOfMonthLocal('2026-10-14')).toBe('2026-10-31');
    expect(endOfMonthLocal('2028-02-03')).toBe('2028-02-29');
    expect(endOfMonthLocal('2026-02-03')).toBe('2026-02-28');
  });
});

describe('compareLocal', () => {
  it('orders dates and date-times; a bare date comes first on its day', () => {
    expect(compareLocal('2026-10-14', '2026-10-15')).toBeLessThan(0);
    expect(compareLocal('2026-10-15', '2026-10-14')).toBeGreaterThan(0);
    expect(compareLocal('2026-10-14', '2026-10-14')).toBe(0);
    expect(compareLocal('2026-10-14', '2026-10-14T00:00')).toBeLessThan(0);
    expect(compareLocal('2026-10-14T00:00', '2026-10-14')).toBeGreaterThan(0);
    expect(compareLocal('2026-10-14T09:00', '2026-10-14T18:00')).toBeLessThan(0);
    expect(compareLocal('2026-10-14T23:59', '2026-10-15')).toBeLessThan(0);
  });
  it('treats space and T forms as equal', () => {
    expect(compareLocal('2026-10-14 09:00', '2026-10-14T09:00')).toBe(0);
  });
  it('sorts malformed values last', () => {
    const list = ['zzz', '2026-10-15', '2026-10-14T10:00', 'aaa', '2026-10-14'];
    expect([...list].sort(compareLocal)).toEqual(['2026-10-14', '2026-10-14T10:00', '2026-10-15', 'aaa', 'zzz']);
  });
});

describe('time zone independence', () => {
  const original = process.env.TZ;
  afterEach(() => {
    if (original === undefined) delete process.env.TZ;
    else process.env.TZ = original;
  });
  it('works in a zone whose DST starts at midnight', () => {
    // São Paulo skipped 00:00-01:00 on 2018-11-04.
    process.env.TZ = 'America/Sao_Paulo';
    expect(toLocalDate(parseLocal('2018-11-04'))).toBe('2018-11-04');
    expect(addDaysLocal('2018-11-03', 1)).toBe('2018-11-04');
    expect(weekdayOf('2018-11-04')).toBe(0);
  });
  it('works far east of UTC', () => {
    process.env.TZ = 'Pacific/Kiritimati';
    const d = new Date(2026, 9, 14, 0, 30);
    expect(toLocalDate(d)).toBe('2026-10-14');
    expect(toLocalDateTime(parseLocal('2026-10-14T00:30'))).toBe('2026-10-14T00:30');
  });
});

describe('range safety (review regressions)', () => {
  it('rejects non-finite offsets instead of returning "0NaN-NaN-NaN"', () => {
    expect(() => addDaysLocal('2026-10-14', Number.NaN)).toThrow(RangeError);
    expect(() => addDaysLocal('2026-10-14', Number.POSITIVE_INFINITY)).toThrow(RangeError);
    expect(() => addMonthsLocal('2026-10-14', Number.NaN)).toThrow(RangeError);
    expect(() => addMinutesLocal('2026-10-14T09:00', Number.NaN)).toThrow(RangeError);
  });
  it('rejects results outside years 0000-9999', () => {
    expect(() => addDaysLocal('9999-12-31', 1)).toThrow(RangeError);
    expect(() => addDaysLocal('0000-01-01', -1)).toThrow(RangeError);
    expect(() => addDaysLocal('2026-10-14', 1e20)).toThrow(RangeError);
    expect(() => addYearsLocal('2026-10-14', 9999)).toThrow(RangeError);
    expect(() => addMonthsLocal('0000-01-15', -1)).toThrow(RangeError);
    expect(() => addMinutesLocal('9999-12-31T23:59', 1)).toThrow(RangeError);
  });
  it('still reaches the edges of the supported range', () => {
    expect(addDaysLocal('9999-12-30', 1)).toBe('9999-12-31');
    expect(addDaysLocal('0000-01-02', -1)).toBe('0000-01-01');
    expect(addYearsLocal('2026-10-14', 7973)).toBe('9999-10-14');
    expect(addMinutesLocal('9999-12-31T23:58', 1)).toBe('9999-12-31T23:59');
  });
  it('treats a malformed weekStartsOn (e.g. from a synced config) as Monday', () => {
    const bad = [Number.NaN, 7, -1, 2] as unknown as (0 | 1)[];
    for (const w of bad) {
      expect(startOfWeekLocal('2026-10-14', w)).toBe('2026-10-12');
      expect(endOfWeekLocal('2026-10-14', w)).toBe('2026-10-18');
    }
    expect(startOfWeekLocal('2026-10-14', 0)).toBe('2026-10-11');
  });
});
