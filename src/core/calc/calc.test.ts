import { describe, expect, it } from 'vitest';
import type { Locale } from '../types';
import { evaluateCalc, evaluateInline, type CalcLineResult } from './calc';
import { CALC_META } from './model';
import { NBSP } from './numbers';

/** value or `!error` of every line */
function results(source: string, locale: Locale = 'pl'): (string | undefined)[] {
  return evaluateCalc(source, { locale }).lines.map((l) => l.value ?? (l.error ? `!${l.error}` : undefined));
}

/** result of the last line */
function last(source: string, locale: Locale = 'pl'): string | undefined {
  return results(source, locale).at(-1);
}

describe('evaluateCalc — spec example (SYNTAX.md §9)', () => {
  it('evaluates the documented block', () => {
    const src = ['cena = 12.50', 'ilość = 4', 'cena * ilość', 'rabat = 15%', 'ans - rabat', '5 km to mi', '20% of 150', 'sum'].join('\n');
    expect(results(src)).toEqual(['12,5', '4', '50', '15%', '42,5', '3,106855961 mi', '30', '139']);
  });

  it('reports assigned names and numeric values', () => {
    const { lines } = evaluateCalc('ilość = 4\n5 km to mi\n10% + 5%');
    expect(lines[0]).toMatchObject({ line: 0, input: 'ilość = 4', value: '4', assigned: 'ilość', numeric: 4 });
    expect(lines[1]!.numeric).toBeCloseTo(3.1068559611866697, 12);
    expect(lines[2]!.numeric).toBeCloseTo(0.15, 12);
  });

  it('returns one result per source line, also for CRLF input', () => {
    const { lines } = evaluateCalc('1 + 1\r\n\r\n# c\r\n2');
    expect(lines.map((l: CalcLineResult) => [l.line, l.input, l.value])).toEqual([
      [0, '1 + 1', '2'],
      [1, '', undefined],
      [2, '# c', undefined],
      [3, '2', '2'],
    ]);
  });

  it('handles an empty source', () => {
    expect(evaluateCalc('').lines).toEqual([{ line: 0, input: '' }]);
  });
});

describe('evaluateCalc — variables', () => {
  it('supports Polish and other Unicode identifiers', () => {
    expect(results('ilość = 4\nżółć = 3\nżółć * ilość')).toEqual(['4', '3', '12']);
    expect(last('цена = 5\nцена * 2')).toBe('10');
    expect(last('α = 2\nα^3')).toBe('8');
    expect(last('cena_netto = 100\ncena_netto * 1,23')).toBe('123');
    expect(last('ZAŻÓŁĆ = 1\nzażółć = 2\nZAŻÓŁĆ + zażółć * 10')).toBe('21');
  });

  it('returns the scope with display names', () => {
    const { scope } = evaluateCalc('ilość = 4\nżółć = ilość * 2');
    expect(scope['ilość']).toBe(4);
    expect(scope['żółć']).toBe(8);
  });

  it('reports unknown variables by their written name', () => {
    expect(last('brak_zmiennej + 1')).toBe('!Nieznana zmienna: brak_zmiennej');
    expect(last('zażółć + 1')).toBe('!Nieznana zmienna: zażółć');
    expect(last('zażółć + 1', 'en')).toBe('!Unknown variable: zażółć');
  });

  it('uses and does not mutate the given scope', () => {
    const input = { x: 21, 'cena netto': 10 };
    const r = evaluateCalc('x * 2\ncena_netto + 1\nx = 1', { scope: input });
    expect(r.lines.map((l) => l.value)).toEqual(['42', '11', '1']);
    expect(input.x).toBe(21);
    expect(r.scope.x).toBe(1);
  });

  it('tracks ans (percentages are rates and do not replace it)', () => {
    expect(results('2 + 3\nans * 2\nrate = 10%\nans + 1')).toEqual(['5', '10', '10%', '11']);
    expect(last('ans')).toBe('!Nieznana zmienna: ans');
  });

  it('defines functions', () => {
    const r = evaluateCalc('f(x) = x^2\nf(4)\nbrutto(netto) = netto * 1,23\nbrutto(100)');
    expect(r.lines.map((l) => l.value)).toEqual(['f(x)', '16', 'brutto(netto)', '123']);
    expect(r.lines[0]!.assigned).toBe('f');
    expect(typeof r.scope.f).toBe('function');
  });

  it('handles several statements on one line', () => {
    const r = evaluateCalc('a = 1; b = 2\na + b\nc = round(2,5; 0)');
    expect(r.lines.map((l) => l.value)).toEqual(['2', '3', '3']);
    expect(r.scope).toMatchObject({ a: 1, b: 2, c: 3 });
  });

  it('lets variables shadow keywords', () => {
    expect(results('total = 100\ntotal')).toEqual(['100', '100']);
  });
});

describe('evaluateCalc — labels and comments', () => {
  it('splits labels', () => {
    const { lines } = evaluateCalc('Czynsz: 1800 zł\nPrąd i gaz: 245,50 zł\nRazem:');
    expect(lines.map((l) => [l.label, l.value])).toEqual([
      ['Czynsz', '1800 zł'],
      ['Prąd i gaz', '245,50 zł'],
      ['Razem', '2045,50 zł'],
    ]);
  });

  it('allows labels with assignments and keywords', () => {
    const { lines } = evaluateCalc('Koszt (stały): k = 50\nWynik: k * 2\nSuma: sum');
    expect(lines.map((l) => [l.label, l.value, l.assigned])).toEqual([
      ['Koszt (stały)', '50', 'k'],
      ['Wynik', '100', undefined],
      ['Suma', '150', undefined],
    ]);
  });

  it('does not take ternaries, ranges in brackets or objects for labels', () => {
    expect(last('a = 5\na > 1 ? 2 : 3')).toBe('2');
    expect(last('v = [1, 2, 3, 4]\nv[2:3]')).toBe('[2; 3]');
    expect(evaluateCalc('2026: 1').lines[0]!.label).toBeUndefined();
  });

  it('shows nothing for a label without expression', () => {
    expect(evaluateCalc('Notatka:').lines[0]).toEqual({ line: 0, input: 'Notatka:', label: 'Notatka' });
  });

  it('ignores comments', () => {
    expect(results('# Rachunki\n// Notatka\n2 + 2 # cztery\n3 * 3 // dziewięć\nconcat("#", "x")')).toEqual([
      undefined,
      undefined,
      '4',
      '9',
      '#x',
    ]);
  });
});

describe('evaluateCalc — aggregates', () => {
  it('sums and averages the group above', () => {
    expect(results('10\n20\nsuma\navg\nrazem\nŚrednia\ntotal\naverage\nmean')).toEqual(['10', '20', '30', '15', '30', '15', '30', '15', '15']);
  });

  it('resets groups at blank lines', () => {
    expect(results('10\n20\n\n5\nsum')).toEqual(['10', '20', undefined, '5', '5']);
  });

  it('uses aggregates inside expressions', () => {
    expect(results('100\n200\nvat = suma * 23%\nsum')).toEqual(['100', '200', '69', '369']);
  });

  it('handles empty groups', () => {
    expect(last('sum')).toBe('0');
    expect(last('avg')).toBe('!Brak wartości liczbowych');
    expect(last('# x\nśrednia')).toBe('!Brak wartości liczbowych');
  });

  it('skips errors, percentages, booleans and text', () => {
    expect(last('10\nnope\n5%\ntrue\n"x"\n20\nsum')).toBe('30');
  });

  it('sums compatible units and falls back to numbers for mixed groups', () => {
    expect(last('1 km\n500 m\nsum')).toBe('1,5 km');
    expect(last('1 km\n2\n3\nsum')).toBe('5');
    expect(last('1 km\n2 kg\nsum')).toBe('!Niezgodne jednostki');
  });

  it('keeps an agreed currency', () => {
    expect(last('12 zł\n5,50 zł\n2\nsuma')).toBe('19,50 zł');
    expect(last('12 zł\n5 EUR\nsuma')).toBe('17');
  });

  it('allows assigning an aggregate', () => {
    const r = evaluateCalc('1\n2\nx = sum\nx * 10');
    expect(r.lines.map((l) => l.value)).toEqual(['1', '2', '3', '30']);
    expect(r.scope.x).toBe(3);
  });
});

describe('evaluateCalc — numbers and locales', () => {
  it('accepts decimal commas where unambiguous', () => {
    expect(results('12,5 * 2\nmax(1,5)\n(12,5 + 1) * 2\nround(2,3456; 2)\nmax(1,5; 2,5)')).toEqual(['25', '5', '27', '2,35', '2,5']);
  });

  it('accepts thousands separators', () => {
    expect(results('1 000 + 1\n12 345,5 * 2\n1 000 000,5')).toEqual(['1001', `24${NBSP}691`, `1${NBSP}000${NBSP}000,5`]);
    expect(last(`12${NBSP}345 + 0`)).toBe(`12${NBSP}345`);
  });

  it('formats English results', () => {
    expect(results('1,000 + 0.5\n12345.5 * 2\n5 > 3\n[1, 2] * 2', 'en')).toEqual(['1,000.5', '24,691', 'true', '[2, 4]']);
  });

  it('formats special values', () => {
    expect(results('1/3\n1/0\n0.1 + 0.2\n1e21\n5 > 3\n5 < 3\nsqrt(-4)\n2 + 3i\n[1, 2, 3] * 2\n"abc"')).toEqual([
      '0,3333333333',
      '∞',
      '0,3',
      '1e21',
      'prawda',
      'fałsz',
      '2i',
      '2 + 3i',
      '[2; 4; 6]',
      'abc',
    ]);
  });

  it('understands Unicode math symbols', () => {
    expect(results('2 × 3\n6 ÷ 4\n5 − 2\n√16\n2³\nπ\n2 ≤ 3\n3 ≠ 3\n5 m²')).toEqual([
      '6',
      '1,5',
      '3',
      '4',
      '8',
      '3,141592654',
      'prawda',
      'fałsz',
      '5 m^2',
    ]);
  });
});

describe('evaluateCalc — percentages', () => {
  it.each<[string, string]>([
    ['100 + 10%', '110'],
    ['100 - 10%', '90'],
    ['100 + 5% + 5%', '110,25'],
    ['20% of 150', '30'],
    ['20% z 150', '30'],
    ['20% od 150', '30'],
    ['200 * 10%', '20'],
    ['15%', '15%'],
    ['10% + 5%', '15%'],
    ['50 as % of 200', '25%'],
    ['50 jako % z 200', '25%'],
    ['10 % 3', '1'],
    ['mod(10, 3)', '1'],
    ['x = 80\nx + (25%)', '100'],
    ['-10% of 50', '-5'],
  ])('%s → %s', (src, expected) => {
    expect(last(src)).toBe(expected);
  });

  it('applies percent variables', () => {
    expect(results('vat = 23%\nnetto = 100\nnetto + vat\nvat of netto\nvat z netto\nnetto - vat')).toEqual([
      '23%',
      '100',
      '123',
      '23',
      '23',
      '77',
    ]);
  });
});

describe('evaluateCalc — units', () => {
  it.each<[string, string]>([
    ['5 km to mi', '3,106855961 mi'],
    ['3 h + 20 min', '3,333333333 h'],
    ['2 kg in lb', '4,409245244 lb'],
    ['90 min to h', '1,5 h'],
    ['1500 m', '1500 m'],
    ['2 kg * 3 m / s^2', '6 N'],
    ['5 km na mi', '3,106855961 mi'],
    ['20°C to °F', '68°F'],
    ['3 godz + 30 minut', '3,5 h'],
    ['2 dni to godz', '48 h'],
    ['1 tydzień to dni', '7 day'],
    ['100 km / 2 h', '50 km / h'],
    ['10 m / 2 m', '5'],
    ['5 km + 2 kg', '!Niezgodne jednostki'],
  ])('%s → %s', (src, expected) => {
    expect(last(src)).toBe(expected);
  });

  it('formats English units', () => {
    expect(last('5 km to mi', 'en')).toBe('3.106855961 mi');
  });
});

describe('evaluateCalc — currencies', () => {
  it.each<[string, string]>([
    ['12 zł + 5 zł', '17 zł'],
    ['12,50 zł * 3', '37,50 zł'],
    ['12 PLN + 5 PLN', '17 PLN'],
    ['12 zł + 5 PLN', '17 zł'],
    ['12 zł + 5 EUR', '17'],
    ['$12 + $5', '$17'],
    ['€10 * 1,5', '€15'],
    ['10 € + 2 €', '12 €'],
    ['£3 - £5', '-£2'],
    ['100 PLN - 10%', '90 PLN'],
    ['20% of 150 zł', '30 zł'],
    ['12 zł / kg * 2 kg', '24 zł'],
    ['12 zł/kg', '12 zł/kg'],
    ['100 USD to PLN', '!Przeliczanie walut nie jest obsługiwane'],
    ['1 000 zł * 12', `12${NBSP}000 zł`],
  ])('%s → %s', (src, expected) => {
    expect(last(src)).toBe(expected);
  });

  it('carries currencies through variables', () => {
    expect(results('x = 10 zł\nx * 3\ny = 2\nx * y')).toEqual(['10 zł', '30 zł', '2', '20 zł']);
  });

  it('treats a currency code defined as a variable as that variable (exchange rate)', () => {
    expect(last('EUR = 4,30\n100 EUR')).toBe('430');
  });
});

describe('evaluateCalc — errors never throw', () => {
  it.each(['1 +', '(1', '2 3', ')', '@', '"abc', 'f(', 'sin()', '1 = 2', '[1, 2', '{', '%', '√', '°', '$', '::', 'to', 'zł'])(
    '%j',
    (src) => {
      expect(() => evaluateCalc(src)).not.toThrow();
    },
  );

  it('gives readable syntax errors', () => {
    expect(results('1 +\n(1\n2 3\nfoo(1)\n"a" * 2')).toEqual([
      '!Niedokończone wyrażenie',
      '!Brakuje nawiasu „)”',
      '!Nieoczekiwany fragment „3”',
      '!Nieznana funkcja: foo',
      '!Nie można zamienić „a” na liczbę',
    ]);
  });

  it('continues after an error', () => {
    expect(results('x = 1\nbad +\nx + 1')).toEqual(['1', '!Niedokończone wyrażenie', '2']);
  });

  it('shows nothing for a bare currency symbol', () => {
    expect(evaluateCalc('zł').lines[0]!.value).toBeUndefined();
  });
});

describe('evaluateInline', () => {
  it('evaluates with a scope', () => {
    expect(evaluateInline('cena * 2', { cena: 12.5 })).toEqual({ value: '25' });
    expect(evaluateInline('= 2 + 2', {})).toEqual({ value: '4' });
    expect(evaluateInline('ilość * 2', { ilość: 3 }, 'en')).toEqual({ value: '6' });
  });

  it('returns nothing for empty expressions', () => {
    expect(evaluateInline('', {})).toEqual({});
    expect(evaluateInline('=', {})).toEqual({});
    expect(evaluateInline('   ', {})).toEqual({});
  });

  it('reports errors', () => {
    expect(evaluateInline('nope * 2', {})).toEqual({ error: 'Nieznana zmienna: nope' });
    expect(evaluateInline('1 +', {}, 'en')).toEqual({ error: 'Unexpected end of expression' });
  });

  it('does not modify the scope', () => {
    const scope: Record<string, unknown> = { x: 1 };
    expect(evaluateInline('x = 5', scope)).toEqual({ value: '5' });
    expect(evaluateInline('g(a) = a', scope).value).toBe('g(a)');
    expect(scope).toEqual({ x: 1 });
  });

  it('keeps percent and currency metadata from calc blocks', () => {
    const { scope } = evaluateCalc('rabat = 15%\nkwota = 10 zł');
    expect(evaluateInline('100 - rabat', scope).value).toBe('85');
    expect(evaluateInline('rabat', scope).value).toBe('15%');
    expect(evaluateInline('kwota * 2', scope).value).toBe('20 zł');
    // metadata survives object spread
    expect(evaluateInline('kwota * 2', { ...scope }).value).toBe('20 zł');
    expect(Object.keys(scope)).not.toContain(String(CALC_META));
  });

  it('does not treat aggregate words specially', () => {
    expect(evaluateInline('suma', { suma: 7 }).value).toBe('7');
    expect(evaluateInline('sum(1, 2, 3)', {}).value).toBe('6');
  });
});

describe('evaluateCalc — review regressions', () => {
  it('keeps whitespace inside string literals', () => {
    expect(evaluateInline('"a  b"', {}).value).toBe('a  b');
    expect(last('s = "zażółć   gęślą"\ns')).toBe('zażółć   gęślą');
    expect(evaluateInline('concat("x", "  ", "y")', {}).value).toBe('x  y');
  });

  it('explains calling a value that is not a function', () => {
    expect(last('x = 2\nx(3)')).toBe('!„x” nie jest funkcją');
    expect(last('x = 2\nx(3)', 'en')).toBe('!“x” is not a function');
  });

  it('formats English arrays without ambiguous thousands separators', () => {
    expect(evaluateInline('[1000, 2500.5]', {}, 'en').value).toBe('[1000, 2500.5]');
    expect(evaluateInline('[1000, 2500.5]', {}, 'pl').value).toBe('[1000; 2500,5]');
    expect(evaluateInline('[10000, 2]', {}, 'pl').value).toBe(`[10${NBSP}000; 2]`);
    expect(evaluateInline('12345', {}, 'en').value).toBe('12,345');
  });

  it('keeps a variable called __proto__ in the returned scope', () => {
    const { scope } = evaluateCalc('__proto__ = 5\nb = __proto__ + 1');
    expect(Object.getPrototypeOf(scope)).toBe(Object.prototype);
    expect(Object.hasOwn(scope, '__proto__')).toBe(true);
    expect(scope.b).toBe(6);
    expect(evaluateCalc('__proto__ + 1', { scope }).lines[0]!.value).toBe('6');
  });
});

describe('evaluateCalc — never throws on odd error texts', () => {
  it('survives strings that look like mangled names', () => {
    expect(() => evaluateCalc('"$ffffff$" + 1')).not.toThrow();
    expect(evaluateCalc('"$ffffff$" + 1').lines[0]!.error).toBe('Nie można zamienić „$ffffff$” na liczbę');
    expect(evaluateInline('"$110000$" * 2', {}).error).toBeTruthy();
  });
});
