import { describe, expect, it } from 'vitest';
import { formatRecurrence, parseRecurrence } from '@/core/dates';
import { describeDraft, DEFAULT_REPEAT, draftToRule, quickPicks, REPEAT_KINDS, ruleToDraft, toggleWeekday, type RepeatDraft } from './reminder';

const THURSDAY = '2026-10-15T09:00';
const draft = (d: Partial<RepeatDraft>): RepeatDraft => ({ ...DEFAULT_REPEAT, ...d });

describe('draftToRule', () => {
  it('maps every picker option to a canonical rule', () => {
    const texts = REPEAT_KINDS.map((kind) => {
      const r = draftToRule(draft({ kind, interval: 3, unit: 'day' }), THURSDAY);
      return r ? formatRecurrence(r, 'en') : null;
    });
    expect(texts).toEqual([
      null,
      'every day',
      'every weekday',
      'every week on Thursday',
      'every month',
      'every year',
      'every 3 days',
    ]);
  });

  it('shows Polish labels for the same rules', () => {
    const labels = REPEAT_KINDS.map((kind) => describeDraft(draft({ kind, interval: 2, unit: 'week' }), THURSDAY, 'pl')?.label ?? null);
    expect(labels).toEqual([
      null,
      'codziennie',
      'w dni robocze',
      'co tydzień w czwartek',
      'co miesiąc',
      'co rok',
      'co 2 tygodnie w czwartek',
    ]);
  });

  it('uses chosen weekdays in Monday-first order', () => {
    const r = draftToRule(draft({ kind: 'weekly', weekdays: [5, 1, 3] }), THURSDAY);
    expect(r).toEqual({ freq: 'week', interval: 1, weekdays: [1, 3, 5] });
    expect(formatRecurrence(r!, 'en')).toBe('every week on Monday, Wednesday, Friday');
    expect(formatRecurrence(r!, 'pl')).toBe('co tydzień w poniedziałek, środę, piątek');
  });

  it('handles Sunday and the "we" preposition for Tuesday', () => {
    const sun = draftToRule(draft({ kind: 'weekly', weekdays: [0] }), THURSDAY)!;
    expect(formatRecurrence(sun, 'en')).toBe('every week on Sunday');
    const tue = draftToRule(draft({ kind: 'weekly', weekdays: [2] }), THURSDAY)!;
    expect(formatRecurrence(tue, 'pl')).toBe('co tydzień we wtorek');
  });

  it('weekly without an anchor or days repeats weekly', () => {
    expect(formatRecurrence(draftToRule(draft({ kind: 'weekly' }), null)!, 'en')).toBe('every week');
  });

  it('custom intervals per unit, clamped to sane values', () => {
    const en = (d: Partial<RepeatDraft>) => formatRecurrence(draftToRule(draft({ kind: 'custom', ...d }), THURSDAY)!, 'en');
    expect(en({ interval: 2, unit: 'week', weekdays: [] })).toBe('every 2 weeks on Thursday');
    expect(en({ interval: 6, unit: 'month' })).toBe('every 6 months');
    expect(en({ interval: 5, unit: 'year' })).toBe('every 5 years');
    expect(en({ interval: 0, unit: 'day' })).toBe('every day');
    expect(en({ interval: Number.NaN, unit: 'day' })).toBe('every day');
    expect(en({ interval: 2.7, unit: 'day' })).toBe('every 2 days');
    expect(describeDraft(draft({ kind: 'custom', interval: 5, unit: 'month' }), THURSDAY, 'pl')?.label).toBe('co 5 miesięcy');
  });

  it('round-trips through the core parser', () => {
    for (const kind of REPEAT_KINDS) {
      const r = draftToRule(draft({ kind, weekdays: [1, 4], interval: 4, unit: 'week' }), THURSDAY);
      if (!r) continue;
      const en = formatRecurrence(r, 'en');
      expect(parseRecurrence(en)).toEqual(r);
      expect(parseRecurrence(formatRecurrence(r, 'pl'))).toEqual(r);
    }
  });
});

describe('ruleToDraft', () => {
  it('maps rule texts back to picker options', () => {
    expect(ruleToDraft(null).kind).toBe('none');
    expect(ruleToDraft('').kind).toBe('none');
    expect(ruleToDraft('every day').kind).toBe('daily');
    expect(ruleToDraft('codziennie').kind).toBe('daily');
    expect(ruleToDraft('every weekday').kind).toBe('weekdays');
    expect(ruleToDraft('co tydzień w poniedziałek, piątek')).toMatchObject({ kind: 'weekly', weekdays: [1, 5] });
    expect(ruleToDraft('every month').kind).toBe('monthly');
    expect(ruleToDraft('co rok').kind).toBe('yearly');
    expect(ruleToDraft('every 3 days')).toMatchObject({ kind: 'custom', interval: 3, unit: 'day' });
    expect(ruleToDraft('co 2 tygodnie we wtorek')).toMatchObject({ kind: 'custom', interval: 2, unit: 'week', weekdays: [2] });
  });

  it('keeps details the picker has no control for', () => {
    const d = ruleToDraft('every month on the 15th when done');
    expect(d).toMatchObject({ kind: 'monthly', monthDay: 15, whenDone: true });
    expect(formatRecurrence(draftToRule(d, THURSDAY)!, 'en')).toBe('every month on the 15th when done');
    const last = ruleToDraft('every 2 months on the last');
    expect(formatRecurrence(draftToRule(last, THURSDAY)!, 'en')).toBe('every 2 months on the last');
  });

  it('round-trips draft → rule → draft', () => {
    for (const text of ['every day', 'every weekday', 'every week on Tuesday, Saturday', 'every year', 'every 10 days']) {
      const d = ruleToDraft(text);
      expect(formatRecurrence(draftToRule(d, THURSDAY)!, 'en')).toBe(text);
    }
  });
});

describe('toggleWeekday', () => {
  it('adds and removes days keeping Monday-first order', () => {
    expect(toggleWeekday([1, 3], 0)).toEqual([1, 3, 0]);
    expect(toggleWeekday([1, 3, 0], 3)).toEqual([1, 0]);
    expect(toggleWeekday([], 6)).toEqual([6]);
  });
});

describe('quickPicks', () => {
  it('computes quick times on a Wednesday morning', () => {
    const picks = quickPicks(new Date(2026, 9, 14, 10, 30, 45));
    expect(Object.fromEntries(picks.map((p) => [p.id, [p.at, p.disabled]]))).toEqual({
      in10: ['2026-10-14T10:40', false],
      in60: ['2026-10-14T11:30', false],
      tonight: ['2026-10-14T19:00', false],
      tomorrowMorning: ['2026-10-15T08:00', false],
      monday: ['2026-10-19T09:00', false],
    });
  });

  it('disables "tonight" after 19:00 and crosses midnight', () => {
    const picks = quickPicks(new Date(2026, 9, 14, 23, 55));
    const byId = Object.fromEntries(picks.map((p) => [p.id, p]));
    expect(byId.tonight?.disabled).toBe(true);
    expect(byId.in10?.at).toBe('2026-10-15T00:05');
    expect(byId.tomorrowMorning?.at).toBe('2026-10-15T08:00');
  });

  it('"Monday" means next week on a Monday and tomorrow on a Sunday', () => {
    expect(quickPicks(new Date(2026, 9, 12, 8, 0)).find((p) => p.id === 'monday')?.at).toBe('2026-10-19T09:00');
    expect(quickPicks(new Date(2026, 9, 18, 8, 0)).find((p) => p.id === 'monday')?.at).toBe('2026-10-19T09:00');
  });
});
