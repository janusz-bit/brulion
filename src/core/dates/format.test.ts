import { describe, expect, it } from 'vitest';
import { formatDateHuman, formatDateShort, todayLocal } from './index';

/** Wednesday, 14 October 2026, 10:00. */
const NOW = new Date(2026, 9, 14, 10, 0);

describe('formatDateHuman — Polish', () => {
  it.each([
    ['2026-10-14', 'dziś'],
    ['2026-10-15', 'jutro'],
    ['2026-10-13', 'wczoraj'],
    ['2026-10-14T09:00', 'dziś 09:00'],
    ['2026-10-15 18:30', 'jutro 18:30'],
    ['2026-10-13T23:59', 'wczoraj 23:59'],
    ['2026-10-16', 'pt 16 paź'],
    ['2026-10-17', 'sob 17 paź'],
    ['2026-10-18', 'nd 18 paź'],
    ['2026-10-19', 'pn 19 paź'],
    ['2026-10-20', 'wt 20 paź'],
    ['2026-10-21', 'śr 21 paź'],
    ['2026-10-22', 'czw 22 paź'],
    ['2026-10-16T09:00', 'pt 16 paź 09:00'],
    ['2026-01-05', 'pn 5 sty'],
    ['2026-05-01', 'pt 1 maj'],
    ['2027-10-17', '17 paź 2027'],
    ['2025-12-31', '31 gru 2025'],
    ['2027-01-01T00:30', '1 sty 2027 00:30'],
  ])('%s -> %s', (input, expected) => {
    expect(formatDateHuman(input, 'pl', NOW)).toBe(expected);
  });
});

describe('formatDateHuman — English', () => {
  it.each([
    ['2026-10-14', 'today'],
    ['2026-10-15', 'tomorrow'],
    ['2026-10-13', 'yesterday'],
    ['2026-10-14T09:00', 'today 09:00'],
    ['2026-10-16', 'Fri, Oct 16'],
    ['2026-10-17T18:30', 'Sat, Oct 17 18:30'],
    ['2027-10-17', 'Oct 17, 2027'],
    ['2026-09-01', 'Tue, Sep 1'],
  ])('%s -> %s', (input, expected) => {
    expect(formatDateHuman(input, 'en', NOW)).toBe(expected);
  });
});

describe('formatDateHuman — edge cases', () => {
  it('returns malformed input unchanged', () => {
    expect(formatDateHuman('jutro', 'pl', NOW)).toBe('jutro');
    expect(formatDateHuman('2026-02-30', 'en', NOW)).toBe('2026-02-30');
    expect(formatDateHuman('', 'pl', NOW)).toBe('');
  });
  it('relative words work across year boundaries', () => {
    const newYearsEve = new Date(2026, 11, 31, 22, 0);
    expect(formatDateHuman('2027-01-01', 'pl', newYearsEve)).toBe('jutro');
    expect(formatDateHuman('2027-01-02', 'pl', newYearsEve)).toBe('2 sty 2027');
  });
  it('defaults "now" to the current time', () => {
    expect(formatDateHuman(todayLocal(), 'pl')).toBe('dziś');
    expect(formatDateHuman(todayLocal(), 'en')).toBe('today');
  });
  it('formatDateShort ignores relative words', () => {
    expect(formatDateShort('2026-10-14', 'pl', 2026)).toBe('śr 14 paź');
    expect(formatDateShort('2026-10-14', 'en')).toBe('Wed, Oct 14');
    expect(formatDateShort('2026-10-14', 'en', 2025)).toBe('Oct 14, 2026');
  });
});

describe('formatDateHuman — review regressions', () => {
  it('falls back to English for an unknown locale instead of throwing', () => {
    const de = 'de' as unknown as 'en';
    expect(formatDateHuman('2026-10-14', de, NOW)).toBe('today');
    expect(formatDateHuman('2026-10-16T09:00', de, NOW)).toBe('Fri, Oct 16 09:00');
    expect(formatDateShort('2026-10-16', de, 2026)).toBe('Fri, Oct 16');
  });
  it('formats without relative words when "now" is invalid', () => {
    const bad = new Date(Number.NaN);
    expect(formatDateHuman('2026-10-14', 'pl', bad)).toBe('14 paź 2026');
    expect(formatDateHuman('2026-10-16T09:00', 'en', bad)).toBe('Oct 16, 2026 09:00');
  });
});

describe('formatDateShort — second review regressions', () => {
  it('returns malformed input unchanged instead of "0  NaN"', () => {
    expect(formatDateShort('bad', 'pl', 2026)).toBe('bad');
    expect(formatDateShort('', 'en')).toBe('');
    expect(formatDateShort('2026-02-30', 'pl', 2026)).toBe('2026-02-30');
  });
  it('accepts a date-time and formats its date (Polish diacritics kept)', () => {
    expect(formatDateShort('2026-10-14T09:00', 'pl', 2026)).toBe('śr 14 paź');
    expect(formatDateShort('2026-10-18 09:00', 'pl', 2026)).toBe('nd 18 paź');
  });
});
