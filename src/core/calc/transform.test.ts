import { describe, expect, it } from 'vitest';
import { lex } from './lexer';
import { CalcError } from './messages';
import type { ValueMeta } from './model';
import { toInternalName } from './names';
import { collapseWhitespace, transformTokens, type TransformContext } from './transform';

function ctx(over: Partial<TransformContext> & { vars?: Record<string, ValueMeta> } = {}): TransformContext {
  const vars = over.vars ?? {};
  const internal = new Map(Object.entries(vars).map(([k, v]) => [toInternalName(k), v]));
  return {
    locale: 'pl',
    aggregates: false,
    isDefined: (n) => internal.has(n),
    metaOf: (n) => internal.get(n),
    isFunction: (n) => ['max', 'round', 'sqrt', 'sum', 'f'].includes(n),
    isUnit: (n) => ['km', 'mi', 'h', 'kg', 'lb', 'm'].includes(n),
    ...over,
  };
}

const run = (src: string, c: TransformContext = ctx()) => transformTokens(lex(src).tokens, c);
const code = (src: string, c?: TransformContext) => run(src, c).code;

describe('numbers', () => {
  it.each<[string, string]>([
    ['12,5 + 1', '12.5 + 1'],
    ['(12,5 + 1) * 2', '(12.5 + 1) * 2'],
    ['max(1,5)', 'max(1,5)'],
    ['max(1,5; 2,5)', 'max(1.5, 2.5)'],
    ['round(2,3456; 2)', 'round(2.3456, 2)'],
    ['[1,5]', '[1,5]'],
    ['1 000 000', '1000000'],
    ['1 000,5', '1000.5'],
    ['1 000 + 2 500', '1000 + 2500'],
    ['12 5', '12 5'],
    ['1, 5', '1, 5'],
    ['1,5e3', '1.5e3'],
  ])('pl: %s → %s', (src, expected) => {
    expect(code(src)).toBe(expected);
  });

  it.each<[string, string]>([
    ['1,000,000.5', '1000000.5'],
    ['1,000 + 2', '1000 + 2'],
    ['max(1,000)', 'max(1,000)'],
    ['12,5', '12.5'],
  ])('en: %s → %s', (src, expected) => {
    expect(code(src, ctx({ locale: 'en' }))).toBe(expected);
  });
});

describe('percent sugar', () => {
  it.each<[string, string, boolean]>([
    ['100 + 10%', '(100) * (1 + (10/100))', false],
    ['100 - 10%', '(100) * (1 - (10/100))', false],
    ['100 + 5% + 5%', '((100) * (1 + (5/100))) * (1 + (5/100))', false],
    ['20% of 150', '(20/100) * 150', false],
    ['20% z 150', '(20/100) * 150', false],
    ['20% od 150', '(20/100) * 150', false],
    ['200 * 10%', '200 * (10/100)', false],
    ['15%', '(15/100)', true],
    ['10% + 5%', '(10/100) + (5/100)', true],
    ['-10%', '-(10/100)', true],
    ['10 % 3', '10 % 3', false],
    ['50 as % of 200', '((50) / (200))', true],
    ['50 jako % z 200', '((50) / (200))', true],
    ['x + (10%)', '(x) * (1 + ((10/100)))', false],
    ['f(2)%', '(f(2)/100)', true],
  ])('%s → %s', (src, expected, percent) => {
    const r = run(src);
    expect(r.code).toBe(expected);
    expect(r.percent).toBe(percent);
  });

  it('treats variables holding percentages as percent terms', () => {
    const c = ctx({ vars: { rabat: { percent: true }, cena: {} } });
    expect(code('cena - rabat', c)).toBe('(cena) * (1 - rabat)');
    expect(code('rabat of cena', c)).toBe('rabat * cena');
    expect(run('rabat', c).percent).toBe(true);
  });

  it('does not treat a defined variable named z as "of"', () => {
    expect(code('20% z 5', ctx({ vars: { z: {} } }))).toBe('20% z 5');
  });
});

describe('currencies', () => {
  it('strips and collects currencies', () => {
    const r = run('12 zł + 5 PLN');
    expect(r.code).toBe('12 + 5');
    expect(r.currencies.map((c) => c.code)).toEqual(['PLN', 'PLN']);
  });

  it('detects prefix symbols', () => {
    const r = run('$12');
    expect(r.code).toBe('12');
    expect(r.currencies[0]).toEqual({ code: 'USD', symbol: '$', prefix: true });
  });

  it('includes currencies of referenced variables', () => {
    const r = run('kwota * 2', ctx({ vars: { kwota: { currency: { code: 'EUR', symbol: '€', prefix: false } } } }));
    expect(r.currencies.map((c) => c.code)).toEqual(['EUR']);
  });

  it('keeps a currency code that is a defined variable', () => {
    expect(code('100 EUR', ctx({ vars: { EUR: {} } }))).toBe('100 EUR');
  });

  it('rejects currency conversion', () => {
    expect(() => run('100 USD to PLN')).toThrow(CalcError);
    expect(() => run('5 zł in EUR')).toThrow(CalcError);
  });
});

describe('identifiers', () => {
  it('mangles Unicode identifiers', () => {
    expect(code('ilość * 2')).toBe('ilo$15b$$107$ * 2');
  });

  it('records references to defined variables only', () => {
    const r = run('a + b', ctx({ vars: { a: {} } }));
    expect([...r.refs]).toEqual(['a']);
  });

  it('applies unit aliases after quantities', () => {
    expect(code('20 min')).toBe('20 minute');
    expect(code('5 sec')).toBe('5 second');
    expect(code('3 godz + 30 minut')).toBe('3 h + 30 minute');
    expect(code('2 dni to godz')).toBe('2 day to h');
    expect(code('100 km/godz')).toBe('100 km/h');
    expect(code('min(1, 2)')).toBe('min(1, 2)');
  });

  it('translates Polish "na" conversions', () => {
    expect(code('5 km na mi')).toBe('5 km to mi');
    expect(code('na + 1')).toBe('na + 1');
  });

  it('lower-cases spreadsheet-style function names', () => {
    expect(code('SUM(1, 2)')).toBe('sum(1, 2)');
    expect(code('ROUND(2,5; 0)')).toBe('round(2.5, 0)');
  });

  it('replaces aggregate keywords only when enabled and not shadowed', () => {
    expect(code('suma * 2', ctx({ aggregates: true }))).toBe('$$sum * 2');
    expect(code('Średnia', ctx({ aggregates: true }))).toBe('$$avg');
    expect(code('sum(1, 2)', ctx({ aggregates: true }))).toBe('sum(1, 2)');
    expect(code('suma * 2')).toBe('suma * 2');
    expect(code('total', ctx({ aggregates: true, vars: { total: {} } }))).toBe('total');
  });

  it('expands √', () => {
    expect(code('√16')).toBe('sqrt(16)');
    expect(code('√(9) + √x')).toBe('sqrt(9) + sqrt(x)');
    expect(code('√f(4)')).toBe('sqrt(f(4))');
  });
});

describe('collapseWhitespace', () => {
  it('collapses whitespace outside string literals only', () => {
    expect(collapseWhitespace('  12   +  5 ')).toBe('12 + 5');
    expect(collapseWhitespace('concat("a   b",   \'c  d\')')).toBe('concat("a   b", \'c  d\')');
    expect(collapseWhitespace('"a \\"  b"  +  1')).toBe('"a \\"  b" + 1');
    expect(collapseWhitespace('"unterminated   x')).toBe('"unterminated   x');
  });
});
