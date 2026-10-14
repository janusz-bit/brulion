import { describe, expect, it } from 'vitest';
import { foldText, foldWithMap, unfoldRange } from './fold';
import {
  MONTH_LOOKUP,
  WEEKDAY_LOOKUP,
  alternation,
  monthName,
  monthShortName,
  own,
  weekdayName,
  weekdayShortName,
} from './names';

describe('fold', () => {
  it('lower-cases and strips Polish diacritics', () => {
    expect(foldText('Zażółć gęślą jaźń')).toBe('zazolc gesla jazn');
    expect(foldText('ŁÓDŹ Ąę Ćń Śż')).toBe('lodz ae cn sz');
    expect(foldText('Środę')).toBe('srode');
  });
  it('handles decomposed (NFD) input and keeps other characters', () => {
    expect(foldText('środa')).toBe('sroda');
    expect(foldText('2026-10-15 18:30 🔁')).toBe('2026-10-15 18:30 🔁');
  });
  it('maps folded offsets back to the original string', () => {
    const input = 'Zadzwoń w śródę';
    const f = foldWithMap(input);
    expect(f.text).toBe('zadzwon w srode');
    const start = f.text.indexOf('srode');
    const r = unfoldRange(f, start, start + 5);
    expect(input.slice(r.start, r.end)).toBe('śródę');
    expect(f.map).toHaveLength(f.text.length + 1);
    expect(f.map[f.text.length]).toBe(input.length);
  });
  it('maps astral characters', () => {
    const input = '🔁 jutro';
    const f = foldWithMap(input);
    const start = f.text.indexOf('jutro');
    const r = unfoldRange(f, start, start + 5);
    expect(input.slice(r.start, r.end)).toBe('jutro');
  });
});

describe('name tables', () => {
  it('formats weekday and month names', () => {
    expect(weekdayName(3, 'pl')).toBe('środa');
    expect(weekdayName(3, 'en')).toBe('Wednesday');
    expect(weekdayName(-1, 'en')).toBe('Saturday');
    expect(weekdayShortName(5, 'pl')).toBe('pt');
    expect(weekdayShortName(0, 'en')).toBe('Sun');
    expect(monthShortName(10, 'pl')).toBe('paź');
    expect(monthShortName(10, 'en')).toBe('Oct');
    expect(monthShortName(13, 'en')).toBe('');
    expect(monthName(10, 'pl')).toBe('października');
    expect(monthName(10, 'pl', 'nominative')).toBe('październik');
    expect(monthName(5, 'en')).toBe('May');
  });
  it('looks up folded weekday forms', () => {
    expect(WEEKDAY_LOOKUP.get('srode')).toEqual({ day: 3, abbr: false, plural: false });
    expect(WEEKDAY_LOOKUP.get('niedziele')?.day).toBe(0);
    expect(WEEKDAY_LOOKUP.get('pt')).toEqual({ day: 5, abbr: true, plural: false });
    expect(WEEKDAY_LOOKUP.get('poniedzialki')).toEqual({ day: 1, abbr: false, plural: true });
    expect(WEEKDAY_LOOKUP.get('thurs')?.day).toBe(4);
    expect(WEEKDAY_LOOKUP.get('środę')).toBeUndefined(); // keys are folded
  });
  it('looks up folded month forms', () => {
    expect(MONTH_LOOKUP.get('pazdziernika')).toEqual({ month: 10, en: false, abbr: false });
    expect(MONTH_LOOKUP.get('paz')).toEqual({ month: 10, en: false, abbr: true });
    expect(MONTH_LOOKUP.get('oct')).toEqual({ month: 10, en: true, abbr: true });
    expect(MONTH_LOOKUP.get('maj')?.month).toBe(5);
    expect(MONTH_LOOKUP.get('may')?.month).toBe(5);
    expect(MONTH_LOOKUP.get('sept')?.month).toBe(9);
  });
  it('builds longest-first alternations', () => {
    expect(alternation(['a', 'abc', 'ab', 'a.b'])).toBe('a\\.b|abc|ab|a');
  });
  it('own() ignores inherited members', () => {
    const rec: Record<string, number> = { a: 1 };
    expect(own(rec, 'a')).toBe(1);
    expect(own(rec, 'constructor')).toBeUndefined();
    expect(own(rec, 'toString')).toBeUndefined();
  });
});
