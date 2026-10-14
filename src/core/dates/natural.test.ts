import { describe, expect, it } from 'vitest';
import { findNaturalDates, parseNaturalDate, stripNaturalDate, type NaturalDateOptions } from './index';

/** Wednesday, 14 October 2026, 10:00. */
const NOW = new Date(2026, 9, 14, 10, 0);

/** "date" or "date time" for compact assertions; null when nothing was found. */
function when(input: string, opts?: NaturalDateOptions, now: Date = NOW): string | null {
  const r = parseNaturalDate(input, now, opts);
  if (!r) return null;
  return r.time ? `${r.date} ${r.time}` : r.date;
}

function matched(input: string, opts?: NaturalDateOptions): string | null {
  return parseNaturalDate(input, NOW, opts)?.matched ?? null;
}

describe('parseNaturalDate — the documented example', () => {
  it('finds "jutro o 18" inside a sentence', () => {
    expect(parseNaturalDate('Zadzwoń do mamy jutro o 18', NOW)).toEqual({
      date: '2026-10-15',
      time: '18:00',
      matched: 'jutro o 18',
      index: 16,
    });
  });
});

describe('relative day words', () => {
  it.each([
    ['dziś', '2026-10-14'],
    ['dzisiaj', '2026-10-14'],
    ['dzis', '2026-10-14'],
    ['DZIŚ', '2026-10-14'],
    ['jutro', '2026-10-15'],
    ['Jutro', '2026-10-15'],
    ['pojutrze', '2026-10-16'],
    ['wczoraj', '2026-10-13'],
    ['przedwczoraj', '2026-10-12'],
    ['today', '2026-10-14'],
    ['tomorrow', '2026-10-15'],
    ['yesterday', '2026-10-13'],
    ['the day after tomorrow', '2026-10-16'],
    ['day before yesterday', '2026-10-12'],
    ['do jutra', '2026-10-15'],
    ['od jutra', '2026-10-15'],
    ['na dziś', '2026-10-14'],
  ])('%s -> %s', (input, expected) => {
    expect(when(input)).toBe(expected);
  });
  it('requires a preposition for the genitive "jutra"', () => {
    expect(when('jutra nie będzie')).toBeNull();
  });
  it('includes leading prepositions in the match', () => {
    expect(parseNaturalDate('Kupić mleko na jutro', NOW)).toMatchObject({ matched: 'na jutro', index: 12 });
  });
  it('does not match inside words', () => {
    expect(when('jutrzejszy')).toBeNull();
    expect(when('todays')).toBeNull();
    expect(when('dzisiejszy')).toBeNull();
  });
});

describe('weekdays (next such day strictly after today)', () => {
  it.each([
    ['poniedziałek', '2026-10-19'],
    ['w poniedziałek', '2026-10-19'],
    ['wtorek', '2026-10-20'],
    ['we wtorek', '2026-10-20'],
    ['środa', '2026-10-21'],
    ['w środę', '2026-10-21'],
    ['w srode', '2026-10-21'],
    ['czwartek', '2026-10-15'],
    ['w czwartek', '2026-10-15'],
    ['piątek', '2026-10-16'],
    ['w piątek', '2026-10-16'],
    ['W PIĄTEK', '2026-10-16'],
    ['w piatek', '2026-10-16'],
    ['sobota', '2026-10-17'],
    ['w sobotę', '2026-10-17'],
    ['niedziela', '2026-10-18'],
    ['w niedzielę', '2026-10-18'],
    ['do piątku', '2026-10-16'],
    ['monday', '2026-10-19'],
    ['on Friday', '2026-10-16'],
    ['wednesday', '2026-10-21'],
  ])('%s -> %s', (input, expected) => {
    expect(when(input)).toBe(expected);
  });

  it.each([
    ['pn', '2026-10-19'],
    ['pon', '2026-10-19'],
    ['wt', '2026-10-20'],
    ['śr', '2026-10-21'],
    ['czw', '2026-10-15'],
    ['pt', '2026-10-16'],
    ['pt.', '2026-10-16'],
    ['sob', '2026-10-17'],
    ['nd', '2026-10-18'],
    ['ndz', '2026-10-18'],
    ['fri', '2026-10-16'],
    ['w pt', '2026-10-16'],
  ])('abbreviation %s alone -> %s', (input, expected) => {
    expect(when(input)).toBe(expected);
  });

  it('ignores abbreviations in prose unless next to a time', () => {
    expect(when('Pt. Pan Tadeusz')).toBeNull();
    expect(when('I sat down')).toBeNull();
    expect(when('Wed nothing')).toBeNull();
    expect(when('pt 18:00')).toBe('2026-10-16 18:00');
    expect(when('sat 10am')).toBe('2026-10-17 10:00');
  });

  it('"przyszły/następny/next" means the occurrence in next week', () => {
    expect(when('w przyszły poniedziałek')).toBe('2026-10-19');
    expect(when('w przyszły piątek')).toBe('2026-10-23');
    expect(when('w przyszłą środę')).toBe('2026-10-21');
    expect(when('w następną sobotę')).toBe('2026-10-24');
    expect(when('w przyszłą niedzielę')).toBe('2026-10-25');
    expect(when('przyszły czwartek')).toBe('2026-10-22');
    expect(when('następny piątek')).toBe('2026-10-23');
    expect(when('do przyszłego piątku')).toBe('2026-10-23');
    expect(when('next friday')).toBe('2026-10-23');
    expect(when('next wednesday')).toBe('2026-10-21');
    expect(when('next sunday')).toBe('2026-10-25');
  });

  it('respects weekStartsOn for "next"', () => {
    expect(when('next sunday', { weekStartsOn: 0 })).toBe('2026-10-18');
    expect(when('next saturday', { weekStartsOn: 0 })).toBe('2026-10-24');
    expect(when('w przyszły poniedziałek', { weekStartsOn: 0 })).toBe('2026-10-19');
  });

  it('"ten/this" includes today, "zeszły/last" looks back', () => {
    expect(when('ten piątek')).toBe('2026-10-16');
    expect(when('w tę środę')).toBe('2026-10-14');
    expect(when('this wednesday')).toBe('2026-10-14');
    expect(when('w najbliższy wtorek')).toBe('2026-10-20');
    expect(when('w zeszły piątek')).toBe('2026-10-09');
    expect(when('w ubiegłą środę')).toBe('2026-10-07');
    expect(when('last tuesday')).toBe('2026-10-13');
  });
});

describe('relative offsets', () => {
  it.each([
    ['za 1 dzień', '2026-10-15'],
    ['za dzień', '2026-10-15'],
    ['za 5 dni', '2026-10-19'],
    ['za dwa dni', '2026-10-16'],
    ['za tydzień', '2026-10-21'],
    ['za tydzien', '2026-10-21'],
    ['za 2 tygodnie', '2026-10-28'],
    ['za trzy tygodnie', '2026-11-04'],
    ['za 5 tygodni', '2026-11-18'],
    ['za miesiąc', '2026-11-14'],
    ['za 2 miesiące', '2026-12-14'],
    ['za 5 miesięcy', '2027-03-14'],
    ['za rok', '2027-10-14'],
    ['za 2 lata', '2028-10-14'],
    ['za 5 lat', '2031-10-14'],
    ['za pół roku', '2027-04-14'],
    ['w ciągu 3 dni', '2026-10-17'],
    ['in 2 days', '2026-10-16'],
    ['in two days', '2026-10-16'],
    ['in a week', '2026-10-21'],
    ['in 2 weeks', '2026-10-28'],
    ['in 3 months', '2027-01-14'],
    ['in 1 year', '2027-10-14'],
    ['2 dni temu', '2026-10-12'],
    ['tydzień temu', '2026-10-07'],
    ['3 weeks ago', '2026-09-23'],
  ])('%s -> %s', (input, expected) => {
    expect(when(input)).toBe(expected);
  });

  it.each([
    ['za godzinę', '2026-10-14 11:00'],
    ['za 3 godziny', '2026-10-14 13:00'],
    ['za 5 godzin', '2026-10-14 15:00'],
    ['za 15 godzin', '2026-10-15 01:00'],
    ['za 2h', '2026-10-14 12:00'],
    ['za minutę', '2026-10-14 10:01'],
    ['za 2 minuty', '2026-10-14 10:02'],
    ['za 30 minut', '2026-10-14 10:30'],
    ['za 90 min', '2026-10-14 11:30'],
    ['za pół godziny', '2026-10-14 10:30'],
    ['za półtorej godziny', '2026-10-14 11:30'],
    ['za kwadrans', '2026-10-14 10:15'],
    ['za trzy kwadranse', '2026-10-14 10:45'],
    ['in 3 hours', '2026-10-14 13:00'],
    ['in an hour', '2026-10-14 11:00'],
    ['in 45 minutes', '2026-10-14 10:45'],
    ['in half an hour', '2026-10-14 10:30'],
    ['2 hours ago', '2026-10-14 08:00'],
  ])('%s -> %s (exact date-time)', (input, expected) => {
    expect(when(input)).toBe(expected);
  });

  it('crosses midnight and ignores seconds', () => {
    expect(when('za 3 godziny', undefined, new Date(2026, 11, 31, 22, 30, 45))).toBe('2027-01-01 01:30');
  });
  it('rejects results outside the supported year range', () => {
    expect(when('za 9999 lat')).toBeNull();
    expect(when('za 100 lat')).toBe('2126-10-14');
  });
  it('rejects incomplete phrases', () => {
    expect(when('za dni')).toBeNull();
    expect(when('in week')).toBeNull();
    expect(when('dni temu')).toBeNull();
  });
  it('does not merge exact date-times with other tokens', () => {
    expect(parseNaturalDate('za 3 godziny jutro', NOW)).toMatchObject({ matched: 'za 3 godziny', time: '13:00' });
  });
});

describe('explicit dates', () => {
  it.each([
    ['15 października', '2026-10-15'],
    ['15 października 2027', '2027-10-15'],
    ['15 października 2027 r.', '2027-10-15'],
    ['15 paź', '2026-10-15'],
    ['15 paz', '2026-10-15'],
    ['15 październik', '2026-10-15'],
    ['14 października', '2026-10-14'],
    ['13 października', '2027-10-13'],
    ['31 grudnia', '2026-12-31'],
    ['3 maja', '2027-05-03'],
    ['1 stycznia', '2027-01-01'],
    ['29 lutego', '2028-02-29'],
    ['15-go października', '2026-10-15'],
    ['1 sty', '2027-01-01'],
    ['1 lut', '2027-02-01'],
    ['1 mar', '2027-03-01'],
    ['1 kwi', '2027-04-01'],
    ['1 maj', '2027-05-01'],
    ['1 cze', '2027-06-01'],
    ['1 lip', '2027-07-01'],
    ['1 sie', '2027-08-01'],
    ['1 wrz', '2027-09-01'],
    ['1 paź', '2027-10-01'],
    ['1 lis', '2026-11-01'],
    ['1 gru', '2026-12-01'],
    ['15.10', '2026-10-15'],
    ['15.10.2026', '2026-10-15'],
    ['15.10.2026r.', '2026-10-15'],
    ['15.10.27', '2027-10-15'],
    ['1.10', '2027-10-01'],
    ['01.11', '2026-11-01'],
    ['15/10', '2026-10-15'],
    ['15/10/2026', '2026-10-15'],
    ['15-10-2026', '2026-10-15'],
    ['2026-10-15', '2026-10-15'],
    ['2026/10/15', '2026-10-15'],
    ['do 20.10', '2026-10-20'],
    ['Oct 15', '2026-10-15'],
    ['15 Oct', '2026-10-15'],
    ['October 15th', '2026-10-15'],
    ['October 15, 2027', '2027-10-15'],
    ['1st of November', '2026-11-01'],
    ['May 3', '2027-05-03'],
    ['sept 1', '2027-09-01'],
    ['the 20th', '2026-10-20'],
    ['on the 1st', '2026-11-01'],
    ['20-go', '2026-10-20'],
  ])('%s -> %s', (input, expected) => {
    expect(when(input)).toBe(expected);
  });

  it('rejects impossible dates', () => {
    expect(when('31 lutego')).toBeNull();
    expect(when('31.04')).toBeNull();
    expect(when('32.10')).toBeNull();
    expect(when('15.13.2026')).toBeNull();
    expect(when('2026-02-30')).toBeNull();
    expect(when('29.02.2027')).toBeNull();
  });

  it('resolves ambiguous slash dates by locale', () => {
    expect(when('10/11')).toBe('2026-11-10');
    expect(when('10/11', { locale: 'en' })).toBe('2027-10-11'); // Oct 11 already passed
    expect(when('11/10', { locale: 'en' })).toBe('2026-11-10');
    expect(when('10/15')).toBe('2026-10-15');
    expect(when('15/10', { locale: 'en' })).toBe('2026-10-15');
  });

  it('finds dates inside sentences', () => {
    expect(parseNaturalDate('Spotkanie 15.10 w biurze', NOW)).toMatchObject({ matched: '15.10', index: 10 });
    expect(parseNaturalDate('Urodziny Ani 3 maja!', NOW)).toMatchObject({ date: '2027-05-03', matched: '3 maja' });
  });
});

describe('false positives', () => {
  it.each([
    'majonez',
    'Maj był ciepły',
    'wersja 1.2.3',
    'IP 192.168.1.10',
    'pi to 3.14',
    'kosztuje 12.5 zł',
    'cena 12.05 zł',
    'rabat 10.10 %',
    'koszt $15.10',
    '1/2 szklanki',
    'ocena 8/10',
    'podwyżka o 5 zł',
    'zwiększyć o 10%',
    'przesunąć o 2 dni',
    'rozmowa o 5 rzeczach',
    'zapisało się 15 osób',
    'Dobry wieczór',
    'Good morning',
    'pokój 12',
    'o 25',
    '13pm',
    '',
  ])('%j has no date', (input) => {
    expect(when(input)).toBeNull();
  });
});

describe('times', () => {
  it.each([
    ['o 9', '2026-10-15 09:00'],
    ['o 11', '2026-10-14 11:00'],
    ['o 9:30', '2026-10-15 09:30'],
    ['o 18.30', '2026-10-14 18:30'],
    ['o godz. 18', '2026-10-14 18:00'],
    ['o godzinie 7', '2026-10-15 07:00'],
    ['godz. 18', '2026-10-14 18:00'],
    ['o 18h', '2026-10-14 18:00'],
    ['9:30', '2026-10-15 09:30'],
    ['18:30', '2026-10-14 18:30'],
    ['10:00', '2026-10-15 10:00'],
    ['10:01', '2026-10-14 10:01'],
    ['6pm', '2026-10-14 18:00'],
    ['6 pm', '2026-10-14 18:00'],
    ['6:30pm', '2026-10-14 18:30'],
    ['6:30 p.m.', '2026-10-14 18:30'],
    ['11am', '2026-10-14 11:00'],
    ['12am', '2026-10-15 00:00'],
    ['12pm', '2026-10-14 12:00'],
    ['at 9', '2026-10-15 09:00'],
    ['at 9:15', '2026-10-15 09:15'],
    ['5 o’clock', '2026-10-15 05:00'],
    ['noon', '2026-10-14 12:00'],
    ['at noon', '2026-10-14 12:00'],
    ['midnight', '2026-10-15 00:00'],
    ['rano', '2026-10-15 08:00'],
    ['przed południem', '2026-10-15 10:00'],
    ['w południe', '2026-10-14 12:00'],
    ['po południu', '2026-10-14 15:00'],
    ['po poludniu', '2026-10-14 15:00'],
    ['wieczorem', '2026-10-14 19:00'],
    ['w nocy', '2026-10-14 22:00'],
    ['o północy', '2026-10-15 00:00'],
    ['in the evening', '2026-10-14 19:00'],
    ['tonight', '2026-10-14 19:00'],
    ['this morning', '2026-10-14 08:00'],
    ['o 7 rano', '2026-10-15 07:00'],
    ['o 8 wieczorem', '2026-10-14 20:00'],
    ['o 3 po południu', '2026-10-14 15:00'],
    ['o 10 w nocy', '2026-10-14 22:00'],
    ['o 2 w nocy', '2026-10-15 02:00'],
    ['o 12 w nocy', '2026-10-15 00:00'],
    ['rano o 7', '2026-10-15 07:00'],
  ])('%s -> %s', (input, expected) => {
    expect(when(input)).toBe(expected);
  });

  it('accepts a bare "o H" only in a time-ish context', () => {
    expect(parseNaturalDate('Spotkanie o 9 w biurze', NOW)).toMatchObject({ date: '2026-10-15', time: '09:00', matched: 'o 9' });
    expect(when('Zadzwoń o 18.')).toBe('2026-10-14 18:00');
    expect(when('o 18, potem kino')).toBe('2026-10-14 18:00');
    expect(when('o 18 zadzwonić do Ani')).toBe('2026-10-14 18:00');
    expect(when('o 15 się spotkamy')).toBe('2026-10-14 15:00');
    expect(when('meet at 7 with Bob')).toBe('2026-10-15 07:00');
  });
});

describe('date + time combinations', () => {
  it.each([
    ['jutro o 18', '2026-10-15 18:00'],
    ['jutro o 7:30', '2026-10-15 07:30'],
    ['o 18 jutro', '2026-10-15 18:00'],
    ['dziś o 9', '2026-10-14 09:00'],
    ['jutro rano', '2026-10-15 08:00'],
    ['dzisiaj wieczorem', '2026-10-14 19:00'],
    ['jutro wieczorem o 8', '2026-10-15 20:00'],
    ['w piątek wieczorem', '2026-10-16 19:00'],
    ['w piątek o 10', '2026-10-16 10:00'],
    ['o 10 w piątek', '2026-10-16 10:00'],
    ['15.10 o 18:30', '2026-10-15 18:30'],
    ['15.10.2026 18:30', '2026-10-15 18:30'],
    ['15 października o 9', '2026-10-15 09:00'],
    ['2026-10-15 18:30', '2026-10-15 18:30'],
    ['2026-10-15T18:30', '2026-10-15 18:30'],
    ['jutro, o 18', '2026-10-15 18:00'],
    ['tomorrow at 6pm', '2026-10-15 18:00'],
    ['tomorrow morning', '2026-10-15 08:00'],
    ['tonight at 9', '2026-10-14 21:00'],
    ['Oct 15 6pm', '2026-10-15 18:00'],
    ['next friday at 5pm', '2026-10-23 17:00'],
    ['za 2 dni o 9', '2026-10-16 09:00'],
    ['w piątek 16.10 o 18', '2026-10-16 18:00'],
  ])('%s -> %s', (input, expected) => {
    expect(when(input)).toBe(expected);
    expect(matched(input)).toBe(input);
  });

  it('keeps the explicit date even when the time already passed', () => {
    expect(when('dziś o 8')).toBe('2026-10-14 08:00');
  });
  it('does not merge two different dates', () => {
    expect(parseNaturalDate('jutro 20.10', NOW)).toMatchObject({ date: '2026-10-15', matched: 'jutro' });
  });
});

describe('periods', () => {
  it.each([
    ['koniec miesiąca', '2026-10-31'],
    ['na koniec miesiąca', '2026-10-31'],
    ['do końca miesiąca', '2026-10-31'],
    ['koniec tygodnia', '2026-10-18'],
    ['do końca przyszłego tygodnia', '2026-10-25'],
    ['koniec roku', '2026-12-31'],
    ['end of month', '2026-10-31'],
    ['by the end of the month', '2026-10-31'],
    ['end of next month', '2026-11-30'],
    ['w przyszłym tygodniu', '2026-10-19'],
    ['w przyszłym miesiącu', '2026-11-01'],
    ['w przyszłym roku', '2027-01-01'],
    ['next week', '2026-10-19'],
    ['next month', '2026-11-01'],
    ['next year', '2027-01-01'],
    ['przyszły tydzień', '2026-10-19'],
    ['w weekend', '2026-10-17'],
    ['na weekend', '2026-10-17'],
    ['this weekend', '2026-10-17'],
    ['w przyszły weekend', '2026-10-24'],
  ])('%s -> %s', (input, expected) => {
    expect(when(input)).toBe(expected);
  });

  it('uses weekStartsOn for weeks', () => {
    expect(when('w przyszłym tygodniu', { weekStartsOn: 0 })).toBe('2026-10-18');
    expect(when('koniec tygodnia', { weekStartsOn: 0 })).toBe('2026-10-17');
  });
  it('"w weekend" on a weekend day is today', () => {
    expect(when('w weekend', undefined, new Date(2026, 9, 18, 9, 0))).toBe('2026-10-18');
  });
  it('end of February in a leap year', () => {
    expect(when('koniec miesiąca', undefined, new Date(2028, 1, 10))).toBe('2028-02-29');
  });
});

describe('offsets and Unicode', () => {
  it('maps offsets back through diacritics', () => {
    const input = 'Zażółć gęślą jaźń w środę o 9:15';
    const r = parseNaturalDate(input, NOW);
    expect(r).toMatchObject({ date: '2026-10-21', time: '09:15', matched: 'w środę o 9:15' });
    expect(input.slice(r!.index, r!.index + r!.matched.length)).toBe(r!.matched);
  });
  it('handles decomposed (NFD) input', () => {
    const phrase = 'w s\u0301rode\u0328'; // "w środę" with combining marks
    const input = `Termin: ${phrase}`;
    const r = parseNaturalDate(input, NOW);
    expect(r?.date).toBe('2026-10-21');
    expect(r?.matched).toBe(phrase);
    expect(r?.index).toBe(8);
  });
  it('handles emoji before the phrase', () => {
    expect(parseNaturalDate('🎂 urodziny 3 maja', NOW)).toMatchObject({ matched: '3 maja', index: 12 });
  });
  it('returns null for an invalid "now"', () => {
    expect(parseNaturalDate('jutro', new Date(Number.NaN))).toBeNull();
  });
});

describe('findNaturalDates', () => {
  it('returns all phrases left to right', () => {
    const all = findNaturalDates('jutro o 9 i 20.10 wieczorem', NOW);
    expect(all.map((r) => [r.matched, r.date, r.time])).toEqual([
      ['jutro o 9', '2026-10-15', '09:00'],
      ['20.10 wieczorem', '2026-10-20', '19:00'],
    ]);
  });
  it('returns [] for empty input', () => {
    expect(findNaturalDates('', NOW)).toEqual([]);
  });
});

describe('stripNaturalDate', () => {
  function strip(input: string): string {
    const r = parseNaturalDate(input, NOW);
    if (!r) throw new Error(`no date in ${input}`);
    return stripNaturalDate(input, r);
  }
  it('removes the phrase and tidies whitespace', () => {
    expect(strip('Zadzwoń do mamy jutro o 18')).toBe('Zadzwoń do mamy');
    expect(strip('jutro o 18 zadzwonić do mamy')).toBe('zadzwonić do mamy');
    expect(strip('Kupić mleko na jutro')).toBe('Kupić mleko');
    expect(strip('Spotkanie 15.10 w biurze')).toBe('Spotkanie w biurze');
    expect(strip('Zadzwoń, jutro o 18, do mamy')).toBe('Zadzwoń do mamy');
    expect(strip('Zadzwoń jutro.')).toBe('Zadzwoń.');
    expect(strip('jutro')).toBe('');
  });
});

describe('review regressions', () => {
  it('does not read "się" typed without diacritics as August after "o <hour>"', () => {
    expect(when('o 15 sie spotkamy')).toBe('2026-10-14 15:00');
    expect(when('o 18 sie zobaczymy')).toBe('2026-10-14 18:00');
    expect(when('jutro o 15 sie spotkamy')).toBe('2026-10-15 15:00');
    expect(matched('jutro o 15 sie spotkamy')).toBe('jutro o 15');
    // A real abbreviated date still works.
    expect(when('urodziny 15 sie')).toBe('2027-08-15');
    expect(when('15 sie 2027')).toBe('2027-08-15');
    expect(when('Pamiętaj o 15 sierpnia')).toBe('2027-08-15');
  });
  it('does not read the verb "mają" (folded "maja") as May', () => {
    expect(when('Oni 5 mają wolne')).toBeNull();
    expect(when('Jutro o 5 mają spotkanie')).toBe('2026-10-15 05:00');
    expect(when('Pamiętaj o 5 maja')).toBe('2027-05-05');
    expect(when('urodziny 3 maja')).toBe('2027-05-03');
  });
  it.each([
    ['7 rano', '2026-10-15 07:00', '7 rano'],
    ['jutro 7 rano', '2026-10-15 07:00', 'jutro 7 rano'],
    ['Pobudka jutro 6:30 rano', '2026-10-15 06:30', 'jutro 6:30 rano'],
    ['8 wieczorem', '2026-10-14 20:00', '8 wieczorem'],
    ['w piątek 9 wieczorem', '2026-10-16 21:00', 'w piątek 9 wieczorem'],
    ['3 po południu', '2026-10-14 15:00', '3 po południu'],
    ['11 w nocy', '2026-10-14 23:00', '11 w nocy'],
    ['tomorrow 7 in the morning', '2026-10-15 07:00', 'tomorrow 7 in the morning'],
  ])('keeps the hour in "<hour> <time of day>": %j', (input, expected, m) => {
    expect(when(input)).toBe(expected);
    expect(matched(input)).toBe(m);
  });
  it('does not turn quantities before a time-of-day word into hours', () => {
    expect(when('2 kg rano')).toBe('2026-10-15 08:00');
    expect(matched('2 kg rano')).toBe('rano');
  });
  it.each([
    ['Jutro: 18:00', '2026-10-15 18:00', 'Jutro: 18:00'],
    ['jutro - 18:00', '2026-10-15 18:00', 'jutro - 18:00'],
    ['15.10 – 18:30', '2026-10-15 18:30', '15.10 – 18:30'],
    ['Fri - 6pm', '2026-10-16 18:00', 'Fri - 6pm'],
  ])('merges a date and a time separated by ":" or a dash: %j', (input, expected, m) => {
    expect(when(input)).toBe(expected);
    expect(matched(input)).toBe(m);
  });
  it('keeps time ranges and date ranges apart', () => {
    expect(findNaturalDates('12:30 - 14:00', NOW).map((x) => x.time)).toEqual(['12:30', '14:00']);
    expect(findNaturalDates('15.10 - 20.10', NOW).map((x) => x.date)).toEqual(['2026-10-15', '2026-10-20']);
  });
  it.each([
    ['dziś o północy', '2026-10-15 00:00'],
    ['jutro o północy', '2026-10-16 00:00'],
    ['w piątek o północy', '2026-10-17 00:00'],
    ['15.10 o północy', '2026-10-16 00:00'],
    ['tonight at midnight', '2026-10-15 00:00'],
    ['tomorrow at midnight', '2026-10-16 00:00'],
    ['dziś o 2 w nocy', '2026-10-15 02:00'],
    ['jutro o 12 w nocy', '2026-10-16 00:00'],
    // An evening word must not turn midnight into noon.
    ['wieczorem o północy', '2026-10-15 00:00'],
    ['this evening at midnight', '2026-10-15 00:00'],
    ['tonight at 0:00', '2026-10-15 00:00'],
    ['tonight at 1am', '2026-10-15 01:00'],
    // Unchanged: evening/late night hours, numeric midnight and undated phrases.
    ['jutro w nocy', '2026-10-15 22:00'],
    ['jutro o 11 w nocy', '2026-10-15 23:00'],
    ['jutro o 0:00', '2026-10-15 00:00'],
    ['o północy', '2026-10-15 00:00'],
    ['o 2 w nocy', '2026-10-15 02:00'],
  ])('midnight / small hours after a dated day mean the following night: %j', (input, expected) => {
    expect(when(input)).toBe(expected);
  });
  it('treats a malformed weekStartsOn as Monday', () => {
    const bad = { weekStartsOn: Number.NaN } as unknown as NaturalDateOptions;
    expect(when('w przyszły poniedziałek', bad)).toBe('2026-10-19');
    expect(when('next week', bad)).toBe('2026-10-19');
    expect(when('koniec tygodnia', bad)).toBe('2026-10-18');
  });
  it('never throws near the end of the supported range', () => {
    const lastDay = new Date(9999, 11, 31, 10, 0);
    expect(() => findNaturalDates('jutro, za tydzień, za 2 godziny, w przyszłym roku, next friday', lastDay)).not.toThrow();
    expect(when('jutro', undefined, lastDay)).toBeNull();
    expect(when('dziś', undefined, lastDay)).toBe('9999-12-31');
  });
  it('stays fast on long inputs with many phrases (no quadratic scans)', () => {
    const t0 = performance.now();
    expect(findNaturalDates('15.10 '.repeat(20_000), NOW).length).toBeGreaterThan(0);
    expect(findNaturalDates('o 9 x '.repeat(20_000), NOW)).toEqual([]);
    expect(findNaturalDates('jutro '.repeat(20_000), NOW)).toHaveLength(1);
    // Quadratic scans took > 30 s here; linear ones well under a second.
    expect(performance.now() - t0).toBeLessThan(8000);
  }, 30_000);
  it('is linear on long whitespace gaps between phrases (no ReDoS when merging)', () => {
    const t0 = performance.now();
    expect(findNaturalDates(`jutro${' '.repeat(40_000)}x o 18 w biurze`, NOW).map((x) => x.matched)).toEqual([
      'jutro',
      'o 18',
    ]);
    expect(when(`jutro${' '.repeat(40_000)}18:00`)).toBe('2026-10-15 18:00');
    expect(performance.now() - t0).toBeLessThan(2000);
  }, 60_000);
  it('stripNaturalDate is linear on long whitespace runs (no ReDoS)', () => {
    const input = `${' '.repeat(50_000)}x jutro`;
    const t0 = performance.now();
    expect(stripNaturalDate(input, { matched: 'jutro', index: input.length - 5 })).toBe('x');
    expect(stripNaturalDate(`a jutro${' ,'.repeat(25_000)}b`, { matched: 'jutro', index: 2 })).toBe('a b');
    expect(performance.now() - t0).toBeLessThan(2000);
  });
});
