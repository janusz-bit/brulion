import { describe, expect, it } from 'vitest';
import { resolveDateExpr } from './index';

/** Wednesday, 14 October 2026, 10:00. */
const NOW = new Date(2026, 9, 14, 10, 0);

const day = (d: string) => ({ from: d, to: d });

describe('resolveDateExpr', () => {
  it.each([
    ['today', day('2026-10-14')],
    ['tomorrow', day('2026-10-15')],
    ['yesterday', day('2026-10-13')],
    ['dziś', day('2026-10-14')],
    ['dzis', day('2026-10-14')],
    ['dzisiaj', day('2026-10-14')],
    ['DZIŚ', day('2026-10-14')],
    ['jutro', day('2026-10-15')],
    ['wczoraj', day('2026-10-13')],
    ['pojutrze', day('2026-10-16')],
    ['2026-10-20', day('2026-10-20')],
    ['2026-10-20T09:00', day('2026-10-20')],
    ['2026-10-20 09:00', day('2026-10-20')],
    ['+7d', day('2026-10-21')],
    ['-2d', day('2026-10-12')],
    ['7d', day('2026-10-21')],
    ['+7', day('2026-10-21')],
    ['-2w', day('2026-09-30')],
    ['+2t', day('2026-10-28')],
    ['+1m', day('2026-11-14')],
    ['+1y', day('2027-10-14')],
    ['+1r', day('2027-10-14')],
    ['+0d', day('2026-10-14')],
    ['+3 dni', day('2026-10-17')],
    ['this-week', { from: '2026-10-12', to: '2026-10-18' }],
    ['next-week', { from: '2026-10-19', to: '2026-10-25' }],
    ['last-week', { from: '2026-10-05', to: '2026-10-11' }],
    ['this-month', { from: '2026-10-01', to: '2026-10-31' }],
    ['next-month', { from: '2026-11-01', to: '2026-11-30' }],
    ['last-month', { from: '2026-09-01', to: '2026-09-30' }],
    ['this-year', { from: '2026-01-01', to: '2026-12-31' }],
    ['next-year', { from: '2027-01-01', to: '2027-12-31' }],
    ['last-year', { from: '2025-01-01', to: '2025-12-31' }],
    ['week', { from: '2026-10-12', to: '2026-10-18' }],
    ['this week', { from: '2026-10-12', to: '2026-10-18' }],
    ['THIS_WEEK', { from: '2026-10-12', to: '2026-10-18' }],
    ['ten-tydzień', { from: '2026-10-12', to: '2026-10-18' }],
    ['ten tydzien', { from: '2026-10-12', to: '2026-10-18' }],
    ['następny-tydzień', { from: '2026-10-19', to: '2026-10-25' }],
    ['przyszły-tydzień', { from: '2026-10-19', to: '2026-10-25' }],
    ['zeszły-tydzień', { from: '2026-10-05', to: '2026-10-11' }],
    ['ten-miesiąc', { from: '2026-10-01', to: '2026-10-31' }],
    ['następny-miesiąc', { from: '2026-11-01', to: '2026-11-30' }],
    ['ten-rok', { from: '2026-01-01', to: '2026-12-31' }],
    ['today..+7d', { from: '2026-10-14', to: '2026-10-21' }],
    ['today .. next-week', { from: '2026-10-14', to: '2026-10-25' }],
    ['+7d..today', { from: '2026-10-14', to: '2026-10-21' }],
    ['2026-10-01..2026-10-31', { from: '2026-10-01', to: '2026-10-31' }],
  ])('%s', (expr, expected) => {
    expect(resolveDateExpr(expr, NOW)).toEqual(expected);
  });

  it.each([
    ['overdue', 'overdue'],
    ['zaległe', 'overdue'],
    ['zalegle', 'overdue'],
    ['none', 'none'],
    ['brak', 'none'],
    ['any', 'any'],
    ['dowolna', 'any'],
    ['ANY', 'any'],
  ])('keyword %s', (expr, expected) => {
    expect(resolveDateExpr(expr, NOW)).toBe(expected);
  });

  it('uses weekStartsOn for weeks', () => {
    expect(resolveDateExpr('this-week', NOW, 0)).toEqual({ from: '2026-10-11', to: '2026-10-17' });
    expect(resolveDateExpr('next-week', NOW, 0)).toEqual({ from: '2026-10-18', to: '2026-10-24' });
  });

  it('handles month and year boundaries', () => {
    const jan31 = new Date(2026, 0, 31, 12, 0);
    expect(resolveDateExpr('next-month', jan31)).toEqual({ from: '2026-02-01', to: '2026-02-28' });
    expect(resolveDateExpr('last-month', jan31)).toEqual({ from: '2025-12-01', to: '2025-12-31' });
    expect(resolveDateExpr('+1m', jan31)).toEqual(day('2026-02-28'));
    const sunday = new Date(2026, 9, 18, 23, 59);
    expect(resolveDateExpr('this-week', sunday)).toEqual({ from: '2026-10-12', to: '2026-10-18' });
    expect(resolveDateExpr('this-week', sunday, 0)).toEqual({ from: '2026-10-18', to: '2026-10-24' });
  });

  it.each([
    '',
    '   ',
    'someday',
    '7',
    '+7q',
    '2026-13-01',
    '2026-02-30',
    'this-decade',
    'next-next-week',
    'constructor',
    'toString',
    'a..b..c',
    'today..',
    '..today',
  ])('returns null for %j', (expr) => {
    expect(resolveDateExpr(expr, NOW)).toBeNull();
  });
});

describe('resolveDateExpr — review regressions', () => {
  it('returns null instead of invalid dates for out-of-range offsets', () => {
    expect(resolveDateExpr('+9999y', NOW)).toBeNull();
    expect(resolveDateExpr('+99999999999999999999d', NOW)).toBeNull();
    expect(resolveDateExpr('-3000y', NOW)).toBeNull();
    expect(resolveDateExpr('today..+9999y', NOW)).toBeNull();
    expect(resolveDateExpr('+7973y', NOW)).toEqual(day('9999-10-14'));
  });
  it('does not throw for "now" at the end of the supported range', () => {
    const lastDay = new Date(9999, 11, 31, 12, 0);
    expect(resolveDateExpr('tomorrow', lastDay)).toBeNull();
    expect(resolveDateExpr('next-week', lastDay)).toBeNull();
    expect(resolveDateExpr('next-year', lastDay)).toBeNull();
    expect(resolveDateExpr('today', lastDay)).toEqual(day('9999-12-31'));
    expect(resolveDateExpr('this-year', lastDay)).toEqual({ from: '9999-01-01', to: '9999-12-31' });
  });
  it('is linear on long whitespace (no ReDoS in the range split)', () => {
    const t0 = performance.now();
    expect(resolveDateExpr(`a${' '.repeat(40_000)}b`, NOW)).toBeNull();
    expect(resolveDateExpr(`today${' '.repeat(40_000)}..${' '.repeat(40_000)}+7d`, NOW)).toEqual({
      from: '2026-10-14',
      to: '2026-10-21',
    });
    expect(performance.now() - t0).toBeLessThan(2000);
  }, 60_000);
  it('treats a malformed weekStartsOn as Monday', () => {
    const bad = Number.NaN as unknown as 0 | 1;
    expect(resolveDateExpr('this-week', NOW, bad)).toEqual({ from: '2026-10-12', to: '2026-10-18' });
    expect(resolveDateExpr('next-week', NOW, 7 as unknown as 0 | 1)).toEqual({ from: '2026-10-19', to: '2026-10-25' });
  });
});
