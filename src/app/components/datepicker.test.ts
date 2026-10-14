import { describe, expect, it } from 'vitest';
import {
  clampDate,
  dayLabel,
  formatPreview,
  marksFor,
  monthGrid,
  monthTitle,
  moveFocusDate,
  naturalPreview,
  parseTimeInput,
  shiftView,
  stepTime,
  viewOf,
  weekdayHeaders,
} from './datepicker';

// Wednesday, 14 October 2026, 10:30 local time.
const NOW = new Date(2026, 9, 14, 10, 30);

describe('monthGrid', () => {
  it('builds 6 Monday-first weeks covering the month', () => {
    const g = monthGrid({ year: 2026, month: 10 }, 1);
    expect(g).toHaveLength(6);
    expect(g.every((w) => w.length === 7)).toBe(true);
    // 1 Oct 2026 is a Thursday → the grid starts on Monday 28 Sep.
    expect(g[0]![0]).toBe('2026-09-28');
    expect(g[0]![3]).toBe('2026-10-01');
    expect(g[5]![6]).toBe('2026-11-08');
  });

  it('starts on Sunday when asked', () => {
    const g = monthGrid({ year: 2026, month: 10 }, 0);
    expect(g[0]![0]).toBe('2026-09-27');
    expect(g[0]![4]).toBe('2026-10-01');
  });

  it('handles February of a leap year and year boundaries', () => {
    const feb = monthGrid({ year: 2028, month: 2 }, 1).flat();
    expect(feb).toContain('2028-02-29');
    const jan = monthGrid({ year: 2027, month: 1 }, 1);
    expect(jan[0]![0]).toBe('2026-12-28');
  });
});

describe('views', () => {
  it('shifts across years', () => {
    expect(shiftView({ year: 2026, month: 12 }, 1)).toEqual({ year: 2027, month: 1 });
    expect(shiftView({ year: 2026, month: 1 }, -1)).toEqual({ year: 2025, month: 12 });
    expect(shiftView({ year: 2026, month: 10 }, -22)).toEqual({ year: 2024, month: 12 });
    expect(viewOf('2026-10-14T09:00')).toEqual({ year: 2026, month: 10 });
  });
});

describe('moveFocusDate', () => {
  it('moves by days and weeks with arrows', () => {
    expect(moveFocusDate('2026-10-14', 'ArrowLeft')).toBe('2026-10-13');
    expect(moveFocusDate('2026-10-14', 'ArrowRight')).toBe('2026-10-15');
    expect(moveFocusDate('2026-10-14', 'ArrowUp')).toBe('2026-10-07');
    expect(moveFocusDate('2026-10-31', 'ArrowDown')).toBe('2026-11-07');
  });

  it('moves by months (PageUp/PageDown) and years with Shift, clamping the day', () => {
    expect(moveFocusDate('2026-10-14', 'PageDown')).toBe('2026-11-14');
    expect(moveFocusDate('2026-03-31', 'PageUp')).toBe('2026-02-28');
    expect(moveFocusDate('2028-02-29', 'PageDown', { shiftKey: true })).toBe('2029-02-28');
  });

  it('jumps to the start/end of the week (Home/End) per weekStartsOn', () => {
    expect(moveFocusDate('2026-10-14', 'Home')).toBe('2026-10-12');
    expect(moveFocusDate('2026-10-14', 'End')).toBe('2026-10-18');
    expect(moveFocusDate('2026-10-14', 'Home', { weekStartsOn: 0 })).toBe('2026-10-11');
    expect(moveFocusDate('2026-10-14', 'End', { weekStartsOn: 0 })).toBe('2026-10-17');
  });

  it('respects min/max and ignores other keys', () => {
    expect(moveFocusDate('2026-10-14', 'PageDown', { max: '2026-10-20' })).toBe('2026-10-20');
    expect(moveFocusDate('2026-10-14', 'ArrowUp', { min: '2026-10-10' })).toBe('2026-10-10');
    expect(moveFocusDate('2026-10-14', 'Enter')).toBeNull();
    expect(clampDate('2026-01-01', '2026-02-01')).toBe('2026-02-01');
  });
});

describe('names', () => {
  it('uses date-fns locales for headers and titles', () => {
    expect(weekdayHeaders('pl', 1).map((h) => h.short)).toEqual(['pn', 'wt', 'śr', 'czw', 'pt', 'sob', 'nd']);
    expect(weekdayHeaders('pl', 1)[0]!.long).toBe('poniedziałek');
    expect(weekdayHeaders('en', 0).map((h) => h.short)).toEqual(['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat']);
    expect(monthTitle({ year: 2026, month: 10 }, 'pl')).toBe('Październik 2026');
    expect(monthTitle({ year: 2026, month: 2 }, 'pl')).toBe('Luty 2026');
    expect(monthTitle({ year: 2026, month: 10 }, 'en')).toBe('October 2026');
  });

  it('labels days for screen readers', () => {
    expect(dayLabel('2026-10-15', 'pl')).toBe('czwartek, 15 października 2026');
    expect(dayLabel('2026-10-15', 'en')).toBe('Thursday, October 15, 2026');
  });
});

describe('formatPreview', () => {
  it('formats compact Polish and English previews', () => {
    expect(formatPreview('2026-10-15T09:00', 'pl', NOW)).toBe('czw, 15 paź 09:00');
    expect(formatPreview('2026-10-15', 'pl', NOW)).toBe('czw, 15 paź');
    expect(formatPreview('2027-01-04', 'pl', NOW)).toBe('pn, 4 sty 2027');
    expect(formatPreview('2026-10-15T09:00', 'en', NOW)).toBe('Thu, Oct 15 09:00');
    expect(formatPreview('2027-01-04', 'en', NOW)).toBe('Mon, Jan 4, 2027');
  });
});

describe('naturalPreview', () => {
  it('interprets "jutro o 9" with time', () => {
    const p = naturalPreview('jutro o 9', NOW, { locale: 'pl', withTime: true });
    expect(p?.value).toBe('2026-10-15T09:00');
    expect(p?.label).toBe('czw, 15 paź 09:00');
    expect(p?.partial).toBe(false);
  });

  it('interprets "w piątek 18:00" as the coming Friday', () => {
    const p = naturalPreview('w piątek 18:00', NOW, { locale: 'pl', withTime: true });
    expect(p?.value).toBe('2026-10-16T18:00');
    expect(p?.label).toBe('pt, 16 paź 18:00');
  });

  it('interprets "15.10" and fills the default time when the picker has a time field', () => {
    expect(naturalPreview('15.10', NOW, { withTime: true, defaultTime: '09:00' })?.value).toBe('2026-10-15T09:00');
    expect(naturalPreview('15.10', NOW, { withTime: true })?.value).toBe('2026-10-15');
    expect(naturalPreview('15.10', NOW)?.label).toBe('czw, 15 paź');
  });

  it('drops a typed time when the picker has no time field', () => {
    expect(naturalPreview('jutro o 9', NOW)?.value).toBe('2026-10-15');
  });

  it('understands text typed without diacritics and English phrases', () => {
    expect(naturalPreview('w srode', NOW)?.value).toBe('2026-10-21');
    const en = naturalPreview('tomorrow at 6pm', NOW, { locale: 'en', withTime: true });
    expect(en?.value).toBe('2026-10-15T18:00');
    expect(en?.label).toBe('Thu, Oct 15 18:00');
  });

  it('flags partial matches and rejects nonsense', () => {
    const p = naturalPreview('jutro kupić mleko', NOW);
    expect(p?.value).toBe('2026-10-15');
    expect(p?.matched).toBe('jutro');
    expect(p?.partial).toBe(true);
    expect(naturalPreview('kiedyś tam', NOW)).toBeNull();
    expect(naturalPreview('   ', NOW)).toBeNull();
  });
});

describe('parseTimeInput', () => {
  it.each([
    ['9', '09:00'],
    ['09', '09:00'],
    ['9:05', '09:05'],
    ['9.30', '09:30'],
    ['9,30', '09:30'],
    ['18 30', '18:30'],
    ['18h30', '18:30'],
    ['930', '09:30'],
    ['0930', '09:30'],
    ['2359', '23:59'],
    ['0', '00:00'],
    ['6pm', '18:00'],
    ['6:30 am', '06:30'],
    ['12am', '00:00'],
    ['12 pm', '12:00'],
  ])('%s → %s', (input, out) => {
    expect(parseTimeInput(input)).toBe(out);
  });

  it.each(['', '24', '9:5', '25:00', '12:60', 'abc', '13pm', '1:2:3'])('rejects %j', (input) => {
    expect(parseTimeInput(input)).toBeNull();
  });
});

describe('stepTime', () => {
  it('steps on a grid and wraps around midnight', () => {
    expect(stepTime('09:07', 15)).toBe('09:15');
    expect(stepTime('09:07', -15)).toBe('09:00');
    expect(stepTime('09:15', 15)).toBe('09:30');
    expect(stepTime('23:45', 15)).toBe('00:00');
    expect(stepTime('00:00', -60)).toBe('23:00');
    expect(stepTime(undefined, 15, '08:00')).toBe('08:15');
  });
});

describe('marksFor', () => {
  it('normalises single marks and arrays', () => {
    const marks = { '2026-10-14': 'journal', '2026-10-15': ['task', 'reminder'] } as const;
    expect(marksFor(marks, '2026-10-14')).toEqual(['journal']);
    expect(marksFor(marks, '2026-10-15')).toEqual(['task', 'reminder']);
    expect(marksFor(marks, '2026-10-16')).toEqual([]);
    expect(marksFor(undefined, '2026-10-16')).toEqual([]);
  });
});
