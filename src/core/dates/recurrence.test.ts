import { describe, expect, it } from 'vitest';
import {
  addDaysLocal,
  canonicalRecurrence,
  dayNumber,
  formatRecurrence,
  fromDayNumber,
  nextOccurrence,
  normalizeRule,
  occurrencesBetween,
  ordinalEn,
  parseRecurrence,
  parseWeekdayList,
  repeaterToRule,
  ruleToRepeater,
  weekdayOf,
  type RecurrenceRule,
} from './index';

describe('parseRecurrence — canonical English (SYNTAX §7)', () => {
  it.each<[string, RecurrenceRule]>([
    ['every day', { freq: 'day', interval: 1 }],
    ['every 3 days', { freq: 'day', interval: 3 }],
    ['every weekday', { freq: 'day', interval: 1, weekdaysOnly: true }],
    ['every week', { freq: 'week', interval: 1 }],
    ['every 2 weeks', { freq: 'week', interval: 2 }],
    ['every week on Monday, Friday', { freq: 'week', interval: 1, weekdays: [1, 5] }],
    ['every month', { freq: 'month', interval: 1 }],
    ['every month on the 15th', { freq: 'month', interval: 1, monthDay: 15 }],
    ['every month on the last', { freq: 'month', interval: 1, monthDay: 'last' }],
    ['every year', { freq: 'year', interval: 1 }],
    ['every 2 years', { freq: 'year', interval: 2 }],
    ['every day when done', { freq: 'day', interval: 1, whenDone: true }],
    ['every week on Monday, Friday when done', { freq: 'week', interval: 1, weekdays: [1, 5], whenDone: true }],
  ])('%s', (text, rule) => {
    expect(parseRecurrence(text)).toEqual(rule);
  });
});

describe('parseRecurrence — Polish (SYNTAX §7)', () => {
  it.each<[string, RecurrenceRule]>([
    ['codziennie', { freq: 'day', interval: 1 }],
    ['co 3 dni', { freq: 'day', interval: 3 }],
    ['w dni robocze', { freq: 'day', interval: 1, weekdaysOnly: true }],
    ['co tydzień', { freq: 'week', interval: 1 }],
    ['co 2 tygodnie', { freq: 'week', interval: 2 }],
    ['co tydzień w poniedziałek, piątek', { freq: 'week', interval: 1, weekdays: [1, 5] }],
    ['co miesiąc', { freq: 'month', interval: 1 }],
    ['co miesiąc 15.', { freq: 'month', interval: 1, monthDay: 15 }],
    ['co miesiąc ostatniego', { freq: 'month', interval: 1, monthDay: 'last' }],
    ['co rok', { freq: 'year', interval: 1 }],
    ['co 2 lata', { freq: 'year', interval: 2 }],
    ['codziennie po wykonaniu', { freq: 'day', interval: 1, whenDone: true }],
  ])('%s', (text, rule) => {
    expect(parseRecurrence(text)).toEqual(rule);
  });
});

describe('parseRecurrence — variants', () => {
  it.each<[string, RecurrenceRule]>([
    ['co tydzien w poniedzialek, piatek', { freq: 'week', interval: 1, weekdays: [1, 5] }],
    ['CO TYDZIEŃ', { freq: 'week', interval: 1 }],
    ['🔁 co tydzień', { freq: 'week', interval: 1 }],
    ['🔁 every week', { freq: 'week', interval: 1 }],
    ['  every   2   weeks  ', { freq: 'week', interval: 2 }],
    ['Every Week On Monday', { freq: 'week', interval: 1, weekdays: [1] }],
    ['co 5 lat', { freq: 'year', interval: 5 }],
    ['co roku', { freq: 'year', interval: 1 }],
    ['corocznie', { freq: 'year', interval: 1 }],
    ['co 5 tygodni', { freq: 'week', interval: 5 }],
    ['co 2 miesiące', { freq: 'month', interval: 2 }],
    ['co 6 miesięcy', { freq: 'month', interval: 6 }],
    ['co dzień', { freq: 'day', interval: 1 }],
    ['każdego dnia', { freq: 'day', interval: 1 }],
    ['co drugi dzień', { freq: 'day', interval: 2 }],
    ['co dwa tygodnie', { freq: 'week', interval: 2 }],
    ['co trzeci miesiąc', { freq: 'month', interval: 3 }],
    ['co poniedziałek', { freq: 'week', interval: 1, weekdays: [1] }],
    ['co środę', { freq: 'week', interval: 1, weekdays: [3] }],
    ['w każdy piątek', { freq: 'week', interval: 1, weekdays: [5] }],
    ['w każdą sobotę i niedzielę', { freq: 'week', interval: 1, weekdays: [6, 0] }],
    ['w poniedziałki i czwartki', { freq: 'week', interval: 1, weekdays: [1, 4] }],
    ['we wtorek i czwartek', { freq: 'week', interval: 1, weekdays: [2, 4] }],
    ['co tydzień we wtorek oraz czwartek', { freq: 'week', interval: 1, weekdays: [2, 4] }],
    ['co 2 tygodnie w pn, pt', { freq: 'week', interval: 2, weekdays: [1, 5] }],
    ['co miesiąc ostatniego dnia', { freq: 'month', interval: 1, monthDay: 'last' }],
    ['co miesiąc w ostatni dzień', { freq: 'month', interval: 1, monthDay: 'last' }],
    ['co miesiąc 1-go', { freq: 'month', interval: 1, monthDay: 1 }],
    ['co miesiąc dnia 15', { freq: 'month', interval: 1, monthDay: 15 }],
    ['co miesiac 15', { freq: 'month', interval: 1, monthDay: 15 }],
    ['w dni powszednie', { freq: 'day', interval: 1, weekdaysOnly: true }],
    ['od poniedziałku do piątku', { freq: 'day', interval: 1, weekdaysOnly: true }],
    ['co 2 dni robocze', { freq: 'day', interval: 2, weekdaysOnly: true }],
    ['w dni robocze, po wykonaniu', { freq: 'day', interval: 1, weekdaysOnly: true, whenDone: true }],
    ['daily', { freq: 'day', interval: 1 }],
    ['weekly', { freq: 'week', interval: 1 }],
    ['weekly on tuesday', { freq: 'week', interval: 1, weekdays: [2] }],
    ['monthly', { freq: 'month', interval: 1 }],
    ['yearly', { freq: 'year', interval: 1 }],
    ['annually', { freq: 'year', interval: 1 }],
    ['fortnightly', { freq: 'week', interval: 2 }],
    ['every other week', { freq: 'week', interval: 2 }],
    ['every two days', { freq: 'day', interval: 2 }],
    ['every monday', { freq: 'week', interval: 1, weekdays: [1] }],
    ['every mon, fri', { freq: 'week', interval: 1, weekdays: [1, 5] }],
    ['every friday and monday', { freq: 'week', interval: 1, weekdays: [1, 5] }],
    ['every 2 weeks on tuesday and thursday', { freq: 'week', interval: 2, weekdays: [2, 4] }],
    ['every week on sunday, monday', { freq: 'week', interval: 1, weekdays: [1, 0] }],
    ['every month on the 1st', { freq: 'month', interval: 1, monthDay: 1 }],
    ['every month on the 31st', { freq: 'month', interval: 1, monthDay: 31 }],
    ['every month on the last day', { freq: 'month', interval: 1, monthDay: 'last' }],
    ['every 3 months on the 2nd', { freq: 'month', interval: 3, monthDay: 2 }],
    ['every weekday when done', { freq: 'day', interval: 1, weekdaysOnly: true, whenDone: true }],
    ['every 3 days, when done', { freq: 'day', interval: 3, whenDone: true }],
    ['every business day', { freq: 'day', interval: 1, weekdaysOnly: true }],
    ['pn, pt', { freq: 'week', interval: 1, weekdays: [1, 5] }],
  ])('%s', (text, rule) => {
    expect(parseRecurrence(text)).toEqual(rule);
  });

  it.each([
    '',
    '   ',
    '🔁',
    'every',
    'co',
    'every 0 days',
    'co 0 dni',
    'every month on the 32nd',
    'every month on the 0th',
    'every month on the 2nd Tuesday',
    'every year on October 15',
    'every day on monday',
    'every week on funday',
    'hello world',
    'every blah',
    'constructor',
    'toString',
    'when done',
    'every week on',
    'jutro',
  ])('rejects %j', (text) => {
    expect(parseRecurrence(text)).toBeNull();
  });
});

describe('formatRecurrence', () => {
  it.each<[RecurrenceRule, string, string]>([
    [{ freq: 'day', interval: 1 }, 'every day', 'codziennie'],
    [{ freq: 'day', interval: 3 }, 'every 3 days', 'co 3 dni'],
    [{ freq: 'day', interval: 1, weekdaysOnly: true }, 'every weekday', 'w dni robocze'],
    [{ freq: 'day', interval: 2, weekdaysOnly: true }, 'every 2 weekdays', 'co 2 dni robocze'],
    [{ freq: 'week', interval: 1 }, 'every week', 'co tydzień'],
    [{ freq: 'week', interval: 2 }, 'every 2 weeks', 'co 2 tygodnie'],
    [{ freq: 'week', interval: 5 }, 'every 5 weeks', 'co 5 tygodni'],
    [{ freq: 'week', interval: 12 }, 'every 12 weeks', 'co 12 tygodni'],
    [{ freq: 'week', interval: 22 }, 'every 22 weeks', 'co 22 tygodnie'],
    [{ freq: 'week', interval: 1, weekdays: [1, 5] }, 'every week on Monday, Friday', 'co tydzień w poniedziałek, piątek'],
    [{ freq: 'week', interval: 1, weekdays: [5, 1] }, 'every week on Monday, Friday', 'co tydzień w poniedziałek, piątek'],
    [{ freq: 'week', interval: 1, weekdays: [2, 4] }, 'every week on Tuesday, Thursday', 'co tydzień we wtorek, czwartek'],
    [{ freq: 'week', interval: 2, weekdays: [3, 6, 0] }, 'every 2 weeks on Wednesday, Saturday, Sunday', 'co 2 tygodnie w środę, sobotę, niedzielę'],
    [{ freq: 'month', interval: 1 }, 'every month', 'co miesiąc'],
    [{ freq: 'month', interval: 2 }, 'every 2 months', 'co 2 miesiące'],
    [{ freq: 'month', interval: 5 }, 'every 5 months', 'co 5 miesięcy'],
    [{ freq: 'month', interval: 1, monthDay: 15 }, 'every month on the 15th', 'co miesiąc 15.'],
    [{ freq: 'month', interval: 1, monthDay: 1 }, 'every month on the 1st', 'co miesiąc 1.'],
    [{ freq: 'month', interval: 1, monthDay: 22 }, 'every month on the 22nd', 'co miesiąc 22.'],
    [{ freq: 'month', interval: 1, monthDay: 'last' }, 'every month on the last', 'co miesiąc ostatniego'],
    [{ freq: 'year', interval: 1 }, 'every year', 'co rok'],
    [{ freq: 'year', interval: 2 }, 'every 2 years', 'co 2 lata'],
    [{ freq: 'year', interval: 5 }, 'every 5 years', 'co 5 lat'],
    [{ freq: 'year', interval: 22 }, 'every 22 years', 'co 22 lata'],
    [{ freq: 'day', interval: 1, whenDone: true }, 'every day when done', 'codziennie po wykonaniu'],
  ])('%j', (rule, en, pl) => {
    expect(formatRecurrence(rule, 'en')).toBe(en);
    expect(formatRecurrence(rule, 'pl')).toBe(pl);
  });

  it('sanitises invalid fields', () => {
    expect(formatRecurrence({ freq: 'day', interval: 0 }, 'en')).toBe('every day');
    expect(formatRecurrence({ freq: 'day', interval: Number.NaN }, 'en')).toBe('every day');
    expect(formatRecurrence({ freq: 'week', interval: 1, weekdays: [] }, 'en')).toBe('every week');
    expect(formatRecurrence({ freq: 'week', interval: 1, weekdays: [9, 1] }, 'en')).toBe('every week on Monday');
    expect(formatRecurrence({ freq: 'month', interval: 1, monthDay: 40 }, 'en')).toBe('every month');
    expect(formatRecurrence({ freq: 'day', interval: 1, monthDay: 3 }, 'en')).toBe('every day');
    expect(formatRecurrence({ freq: 'year', interval: 1, monthDay: 3 }, 'en')).toBe('every year');
    expect(formatRecurrence({ freq: 'week', interval: 1, weekdaysOnly: true }, 'en')).toBe(
      'every week on Monday, Tuesday, Wednesday, Thursday, Friday',
    );
  });

  it('round-trips through parseRecurrence in both languages', () => {
    const rules: RecurrenceRule[] = [
      { freq: 'day', interval: 1 },
      { freq: 'day', interval: 7, whenDone: true },
      { freq: 'day', interval: 1, weekdaysOnly: true },
      { freq: 'day', interval: 3, weekdaysOnly: true, whenDone: true },
      { freq: 'week', interval: 1 },
      { freq: 'week', interval: 3 },
      { freq: 'week', interval: 1, weekdays: [1, 2, 3, 4, 5, 6, 0] },
      { freq: 'week', interval: 2, weekdays: [2], whenDone: true },
      { freq: 'month', interval: 1 },
      { freq: 'month', interval: 12, monthDay: 31 },
      { freq: 'month', interval: 1, monthDay: 'last', whenDone: true },
      { freq: 'year', interval: 1 },
      { freq: 'year', interval: 25 },
    ];
    for (const rule of rules) {
      expect(parseRecurrence(formatRecurrence(rule, 'en'))).toEqual(rule);
      expect(parseRecurrence(formatRecurrence(rule, 'pl'))).toEqual(rule);
    }
  });
});

describe('canonicalRecurrence', () => {
  it('normalises Polish and loose English to canonical text', () => {
    expect(canonicalRecurrence('co tydzień w poniedziałek, piątek')).toBe('every week on Monday, Friday');
    expect(canonicalRecurrence('w dni robocze po wykonaniu')).toBe('every weekday when done');
    expect(canonicalRecurrence('co miesiąc ostatniego')).toBe('every month on the last');
    expect(canonicalRecurrence('🔁 co 2 lata')).toBe('every 2 years');
    expect(canonicalRecurrence('every friday and monday')).toBe('every week on Monday, Friday');
    expect(canonicalRecurrence('Every Day')).toBe('every day');
  });
  it('returns null for unsupported text', () => {
    expect(canonicalRecurrence('sometimes')).toBeNull();
  });
});

describe('helpers', () => {
  it('ordinalEn', () => {
    expect([1, 2, 3, 4, 11, 12, 13, 21, 22, 23, 31, 101, 111].map(ordinalEn)).toEqual([
      '1st', '2nd', '3rd', '4th', '11th', '12th', '13th', '21st', '22nd', '23rd', '31st', '101st', '111th',
    ]);
  });
  it('parseWeekdayList', () => {
    expect(parseWeekdayList('poniedzialek i piatek')).toEqual([1, 5]);
    expect(parseWeekdayList('w srode, sobote')).toEqual([3, 6]);
    expect(parseWeekdayList('monday, monday')).toEqual([1]);
    expect(parseWeekdayList('monday, ')).toBeNull();
    expect(parseWeekdayList('')).toBeNull();
  });
  it('normalizeRule drops irrelevant fields', () => {
    expect(normalizeRule({ freq: 'day', interval: 2.7, weekdays: [1], monthDay: 3 })).toEqual({ freq: 'day', interval: 2 });
    expect(normalizeRule({ freq: 'year', interval: 1, monthDay: 'last' })).toEqual({ freq: 'year', interval: 1 });
    expect(normalizeRule({ freq: 'week', interval: 1, weekdays: [1], weekdaysOnly: true })).toEqual({
      freq: 'week',
      interval: 1,
      weekdays: [1],
    });
  });
});

describe('Logseq repeaters', () => {
  it.each<[string, RecurrenceRule | null]>([
    ['+1w', { freq: 'week', interval: 1 }],
    ['++2d', { freq: 'day', interval: 2 }],
    ['.+1m', { freq: 'month', interval: 1, whenDone: true }],
    ['+1y', { freq: 'year', interval: 1 }],
    ['+1W', { freq: 'week', interval: 1 }],
    [' +3d ', { freq: 'day', interval: 3 }],
    ['+3h', null],
    ['+0d', null],
    ['+1x', null],
    ['1w', null],
    ['', null],
    ['+-1d', null],
  ])('%j', (repeater, rule) => {
    expect(repeaterToRule(repeater)).toEqual(rule);
  });
  it('converts simple rules back', () => {
    expect(ruleToRepeater({ freq: 'week', interval: 1 })).toBe('+1w');
    expect(ruleToRepeater({ freq: 'month', interval: 2, whenDone: true })).toBe('.+2m');
    expect(ruleToRepeater({ freq: 'year', interval: 1 })).toBe('+1y');
    expect(ruleToRepeater({ freq: 'day', interval: 3 })).toBe('+3d');
    expect(ruleToRepeater({ freq: 'week', interval: 1, weekdays: [1] })).toBeNull();
    expect(ruleToRepeater({ freq: 'month', interval: 1, monthDay: 'last' })).toBeNull();
    expect(ruleToRepeater({ freq: 'day', interval: 1, weekdaysOnly: true })).toBeNull();
  });
});

const r = (text: string): RecurrenceRule => {
  const rule = parseRecurrence(text);
  if (!rule) throw new Error(`cannot parse ${text}`);
  return rule;
};

describe('nextOccurrence', () => {
  it.each<[string, string, string]>([
    ['every day', '2026-10-14', '2026-10-15'],
    ['every 3 days', '2026-10-14', '2026-10-17'],
    ['every day', '2026-12-31', '2027-01-01'],
    ['every weekday', '2026-10-14', '2026-10-15'],
    ['every weekday', '2026-10-16', '2026-10-19'],
    ['every weekday', '2026-10-17', '2026-10-19'],
    ['every weekday', '2026-10-18', '2026-10-19'],
    ['co 2 dni robocze', '2026-10-15', '2026-10-19'],
    ['co 5 dni robocze', '2026-10-12', '2026-10-19'],
    ['co 6 dni robocze', '2026-10-17', '2026-10-26'],
    ['every week', '2026-10-14', '2026-10-21'],
    ['every 2 weeks', '2026-10-14', '2026-10-28'],
    ['every week on Monday, Friday', '2026-10-14', '2026-10-16'],
    ['every week on Monday, Friday', '2026-10-16', '2026-10-19'],
    ['every week on Monday, Friday', '2026-10-19', '2026-10-23'],
    ['every week on Wednesday', '2026-10-14', '2026-10-21'],
    ['every week on Sunday', '2026-10-14', '2026-10-18'],
    ['every week on Sunday', '2026-10-18', '2026-10-25'],
    ['every 2 weeks on Monday, Friday', '2026-10-14', '2026-10-16'],
    ['every 2 weeks on Monday, Friday', '2026-10-16', '2026-10-26'],
    ['every month', '2026-10-14', '2026-11-14'],
    ['every month', '2026-01-31', '2026-02-28'],
    ['every month', '2028-01-31', '2028-02-29'],
    ['every month', '2026-12-15', '2027-01-15'],
    ['every 3 months', '2026-11-30', '2027-02-28'],
    ['every month on the 15th', '2026-10-14', '2026-10-15'],
    ['every month on the 15th', '2026-10-15', '2026-11-15'],
    ['every month on the 15th', '2026-10-20', '2026-11-15'],
    ['every 2 months on the 15th', '2026-10-20', '2026-12-15'],
    ['every month on the 31st', '2026-10-31', '2026-11-30'],
    ['every month on the 31st', '2026-11-30', '2026-12-31'],
    ['every month on the last', '2026-10-14', '2026-10-31'],
    ['every month on the last', '2026-10-31', '2026-11-30'],
    ['every month on the last', '2027-01-31', '2027-02-28'],
    ['every 2 months on the last', '2026-10-31', '2026-12-31'],
    ['every year', '2026-10-14', '2027-10-14'],
    ['every year', '2028-02-29', '2029-02-28'],
    ['every 2 years', '2026-10-14', '2028-10-14'],
    ['every day', '2026-10-14T09:00', '2026-10-15T09:00'],
    ['every day', '2026-10-14 09:00', '2026-10-15T09:00'],
    ['every week on Friday', '2026-10-14T18:30', '2026-10-16T18:30'],
  ])('%s from %s -> %s', (rule, from, expected) => {
    expect(nextOccurrence(r(rule), from)).toBe(expected);
  });

  it('computes "when done" rules from the completion date', () => {
    expect(nextOccurrence(r('every week when done'), '2026-10-01', '2026-10-14')).toBe('2026-10-21');
    expect(nextOccurrence(r('every day when done'), '2026-10-01T08:00', '2026-10-14')).toBe('2026-10-15T08:00');
    expect(nextOccurrence(r('every month on the 15th when done'), '2026-09-15', '2026-10-20')).toBe('2026-11-15');
    // without completedOn, or for normal rules, the original date is used
    expect(nextOccurrence(r('every week when done'), '2026-10-01')).toBe('2026-10-08');
    expect(nextOccurrence(r('every week'), '2026-10-01', '2026-10-14')).toBe('2026-10-08');
  });

  it('returns malformed input unchanged and sanitises intervals', () => {
    expect(nextOccurrence(r('every day'), 'kiedyś')).toBe('kiedyś');
    expect(nextOccurrence({ freq: 'day', interval: 0 }, '2026-10-14')).toBe('2026-10-15');
    expect(nextOccurrence({ freq: 'week', interval: 1, weekdays: [] }, '2026-10-14')).toBe('2026-10-21');
  });

  it('is always strictly later', () => {
    const rules = ['every day', 'every weekday', 'every week on Monday, Friday', 'every month on the last', 'every year'];
    let d = '2026-01-01';
    for (let i = 0; i < 400; i++) {
      for (const rule of rules) expect(nextOccurrence(r(rule), d) > d).toBe(true);
      d = addDaysLocal(d, 1);
    }
  });
});

describe('occurrencesBetween', () => {
  it('lists daily occurrences in range (anchor included)', () => {
    expect(occurrencesBetween(r('every day'), '2026-10-14', '2026-10-14', '2026-10-17')).toEqual([
      '2026-10-14', '2026-10-15', '2026-10-16', '2026-10-17',
    ]);
  });
  it('starts later when the anchor is before the range', () => {
    expect(occurrencesBetween(r('every 3 days'), '2026-10-01', '2026-10-14', '2026-10-20')).toEqual([
      '2026-10-16', '2026-10-19',
    ]);
  });
  it('returns [] for empty ranges or late anchors', () => {
    expect(occurrencesBetween(r('every day'), '2026-11-01', '2026-10-01', '2026-10-31')).toEqual([]);
    expect(occurrencesBetween(r('every day'), '2026-10-01', '2026-10-31', '2026-10-01')).toEqual([]);
    expect(occurrencesBetween(r('every day'), 'bad', '2026-10-01', '2026-10-31')).toEqual([]);
    expect(occurrencesBetween(r('every day'), '2026-10-01', '2026-10-01', '2026-10-31', 0)).toEqual([]);
  });
  it('counts the anchor even if it is not one of the weekdays', () => {
    expect(occurrencesBetween(r('every week on Monday, Friday'), '2026-10-14', '2026-10-01', '2026-10-31')).toEqual([
      '2026-10-14', '2026-10-16', '2026-10-19', '2026-10-23', '2026-10-26', '2026-10-30',
    ]);
  });
  it('remembers the anchor day of month', () => {
    expect(occurrencesBetween(r('every month'), '2026-01-31', '2026-01-01', '2026-06-30')).toEqual([
      '2026-01-31', '2026-02-28', '2026-03-31', '2026-04-30', '2026-05-31', '2026-06-30',
    ]);
  });
  it('handles leap days yearly', () => {
    expect(occurrencesBetween(r('every year'), '2028-02-29', '2028-01-01', '2032-12-31')).toEqual([
      '2028-02-29', '2029-02-28', '2030-02-28', '2031-02-28', '2032-02-29',
    ]);
  });
  it('keeps the anchor time', () => {
    expect(occurrencesBetween(r('every week'), '2026-10-14T07:30', '2026-10-15', '2026-10-31')).toEqual([
      '2026-10-21T07:30', '2026-10-28T07:30',
    ]);
  });
  it('lists business days', () => {
    expect(occurrencesBetween(r('every weekday'), '2026-10-16', '2026-10-16', '2026-10-23')).toEqual([
      '2026-10-16', '2026-10-19', '2026-10-20', '2026-10-21', '2026-10-22', '2026-10-23',
    ]);
  });
  it('caps the number of results', () => {
    expect(occurrencesBetween(r('every day'), '2026-01-01', '2026-01-01', '2026-12-31', 10)).toHaveLength(10);
    expect(occurrencesBetween(r('every day'), '2020-01-01', '2020-01-01', '2030-12-31')).toHaveLength(500);
  });
  it('fast-forwards old anchors without changing results', () => {
    const cases: [string, string][] = [
      ['every day', '1990-01-01'],
      ['every 3 days', '2001-02-03'],
      ['every 2 weeks', '2015-06-07'],
      ['every 2 weeks on Monday, Thursday', '2019-03-06'],
      ['every week on Sunday', '2010-01-01'],
      ['every month', '2004-01-31'],
      ['every 5 months on the last', '2003-08-12'],
      ['every month on the 30th', '2012-02-10'],
      ['every year', '2000-02-29'],
      ['every 3 years', '1999-12-31'],
      ['every weekday', '2024-05-05'],
    ];
    for (const [text, anchor] of cases) {
      const rule = r(text);
      const fast = occurrencesBetween(rule, anchor, '2026-10-01', '2027-03-31');
      const slow = occurrencesBetween(rule, anchor, anchor, '2027-03-31', 1_000_000).filter((d) => d >= '2026-10-01');
      expect(fast, text).toEqual(slow);
      expect(fast.length, text).toBeGreaterThan(0);
    }
  });
});

describe('review regressions', () => {
  it('parses rules quickly even with long runs of separators (no ReDoS)', () => {
    const t0 = performance.now();
    expect(parseRecurrence(`${' ,'.repeat(20_000)}x when done`)).toBeNull();
    expect(parseRecurrence(`every week on monday${', '.repeat(20_000)}x`)).toBeNull();
    expect(parseWeekdayList(`monday${' '.repeat(40_000)}x`)).toBeNull();
    expect(parseWeekdayList(`monday${' '.repeat(40_000)}, friday`)).toEqual([1, 5]);
    expect(performance.now() - t0).toBeLessThan(3000);
  }, 60_000);
  it('tolerates a trailing comma or semicolon', () => {
    expect(canonicalRecurrence('every week on Monday, Friday,')).toBe('every week on Monday, Friday');
    expect(canonicalRecurrence('co tydzień w poniedziałek;')).toBe('every week on Monday');
    expect(canonicalRecurrence('every day, when done,')).toBe('every day when done');
  });
  it('rejects absurd intervals so they cannot produce invalid dates', () => {
    expect(parseRecurrence('every 99999999999999999999 days')).toBeNull();
    expect(parseRecurrence('every 100000 years')).toBeNull();
    expect(repeaterToRule('+99999999999999999999d')).toBeNull();
    expect(parseRecurrence('every 9999 days')).toEqual({ freq: 'day', interval: 9999 });
    expect(normalizeRule({ freq: 'day', interval: 1e21 }).interval).toBeLessThanOrEqual(99_999);
    expect(formatRecurrence({ freq: 'day', interval: 1e21 }, 'en')).not.toContain('e+');
  });
  it('nextOccurrence never returns a date beyond year 9999 (returns "from" unchanged)', () => {
    expect(nextOccurrence(r('every year'), '9999-06-01')).toBe('9999-06-01');
    expect(nextOccurrence(r('every 10 days'), '9999-12-25T09:00')).toBe('9999-12-25T09:00');
    expect(nextOccurrence({ freq: 'year', interval: 1e21 }, '2026-10-14')).toBe('2026-10-14');
    expect(nextOccurrence(r('every month'), '9999-11-30')).toBe('9999-12-30');
  });
  it('occurrencesBetween stops cleanly at the end of the supported range', () => {
    expect(occurrencesBetween(r('every year'), '9997-03-01', '9990-01-01', '9999-12-31')).toEqual([
      '9997-03-01',
      '9998-03-01',
      '9999-03-01',
    ]);
    // 1e21 is clamped to MAX_INTERVAL years: only the anchor fits before 9999-12-31.
    expect(occurrencesBetween({ freq: 'year', interval: 1e21 }, '2026-10-14', '2026-10-01', '9999-12-31')).toEqual([
      '2026-10-14',
    ]);
    // Clamped days still step normally (99 999 days ≈ 274 years) and stop before year 10000.
    const huge = occurrencesBetween({ freq: 'day', interval: 1e21 }, '2026-10-14', '2026-10-01', '9999-12-31');
    expect(huge.slice(0, 2)).toEqual(['2026-10-14', '2300-07-29']);
    expect(huge.every((d) => d <= '9999-12-31' && /^\d{4}-\d{2}-\d{2}$/.test(d))).toBe(true);
  });
  it('fast-forwards business-day rules with very old anchors', () => {
    /** Independent reference: every n-th business day counted from a weekday anchor. */
    function reference(n: number, anchor: string, from: string, to: string): string[] {
      const num = (d: string) => dayNumber(Number(d.slice(0, 4)), Number(d.slice(5, 7)), Number(d.slice(8, 10)));
      const out: string[] = [];
      const [start, lo, hi] = [num(anchor), num(from), num(to)];
      let count = 0;
      for (let d = start; d <= hi; d++) {
        const dow = (((d + 4) % 7) + 7) % 7; // 1970-01-01 was a Thursday
        if (dow === 0 || dow === 6) continue;
        if (count % n === 0 && d >= lo) out.push(fromDayNumber(d));
        count += 1;
      }
      return out;
    }
    for (const [n, anchor] of [
      [1, '1400-03-03'],
      [3, '1500-01-01'],
      [7, '1800-06-30'],
    ] as const) {
      expect(weekdayOf(anchor), anchor).not.toBe(0);
      expect(weekdayOf(anchor), anchor).not.toBe(6);
      const rule: RecurrenceRule = { freq: 'day', interval: n, weekdaysOnly: true };
      const got = occurrencesBetween(rule, anchor, '2026-10-01', '2026-11-15');
      expect(got.length, `${n} ${anchor}`).toBeGreaterThan(0);
      expect(got, `${n} ${anchor}`).toEqual(reference(n, anchor, '2026-10-01', '2026-11-15'));
    }
  });
  it('fast-forward of business days with a weekend anchor matches plain iteration', () => {
    const rule: RecurrenceRule = { freq: 'day', interval: 4, weekdaysOnly: true };
    const anchor = '2001-06-02'; // Saturday
    const fast = occurrencesBetween(rule, anchor, '2026-10-01', '2026-12-31');
    const slow = occurrencesBetween(rule, anchor, anchor, '2026-12-31', 1_000_000).filter((d) => d >= '2026-10-01');
    expect(fast).toEqual(slow);
    expect(fast.length).toBeGreaterThan(0);
  });
  it.each([
    ['co kwartał', 'every 3 months'],
    ['co kwartal', 'every 3 months'],
    ['kwartalnie', 'every 3 months'],
    ['every quarter', 'every 3 months'],
    ['quarterly', 'every 3 months'],
    ['co pół roku', 'every 6 months'],
    ['co pol roku po wykonaniu', 'every 6 months when done'],
    ['every half year', 'every 6 months'],
    ['półrocznie', 'every 6 months'],
    ['every second week', 'every 2 weeks'],
    ['every 2nd week', 'every 2 weeks'],
    ['every third day', 'every 3 days'],
    ['every 3rd month on the 15th', 'every 3 months on the 15th'],
  ])('also understands %j', (input, expected) => {
    expect(canonicalRecurrence(input)).toBe(expected);
  });
});

describe('second review regressions', () => {
  const daily = { freq: 'day', interval: 1 } as const;

  it('occurrencesBetween treats a NaN max as the default and floors fractional ones', () => {
    expect(occurrencesBetween(daily, '2026-10-01', '2026-10-01', '2026-10-05', Number.NaN)).toHaveLength(5);
    expect(occurrencesBetween(daily, '2026-10-01', '2026-10-01', '2026-10-31', 2.5)).toEqual(['2026-10-01', '2026-10-02']);
    expect(occurrencesBetween(daily, '2026-10-01', '2026-10-01', '2026-10-31', 0.5)).toEqual([]);
    expect(occurrencesBetween(daily, '2026-10-01', '2026-10-01', '2026-10-03', Number.POSITIVE_INFINITY)).toHaveLength(3);
  });

  describe('rules with an unknown frequency (e.g. from corrupted JSON)', () => {
    const bad = { freq: 'hour', interval: 1 } as unknown as RecurrenceRule;
    it('formatRecurrence returns an empty string, never undefined', () => {
      expect(formatRecurrence(bad, 'en')).toBe('');
      expect(formatRecurrence({ ...bad, whenDone: true }, 'pl')).toBe('');
    });
    it('nextOccurrence returns "from" unchanged (no next occurrence)', () => {
      expect(nextOccurrence(bad, '2026-10-14')).toBe('2026-10-14');
      expect(nextOccurrence(bad, '2026-10-14T09:00')).toBe('2026-10-14T09:00');
    });
    it('occurrencesBetween returns no occurrences', () => {
      expect(occurrencesBetween(bad, '2026-10-14', '2026-10-01', '2026-10-31')).toEqual([]);
    });
    it('ruleToRepeater returns null', () => {
      expect(ruleToRepeater(bad)).toBeNull();
    });
  });
});
