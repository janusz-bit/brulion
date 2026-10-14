import { describe, expect, it } from 'vitest';
import { formatFixed, formatMoney, formatNumber, NBSP, parseLocaleNumber, stripInlineMarkup, type CurrencyInfo } from './numbers';

const PLN: CurrencyInfo = { code: 'PLN', symbol: 'zł', prefix: false };
const USD: CurrencyInfo = { code: 'USD', symbol: '$', prefix: true };
const EUR: CurrencyInfo = { code: 'EUR', symbol: '€', prefix: false };

describe('formatNumber', () => {
  it.each<[number, string, string]>([
    [0, '0', '0'],
    [-0, '0', '0'],
    [5, '5', '5'],
    [0.5, '0,5', '0.5'],
    [1 / 3, '0,3333333333', '0.3333333333'],
    [2 / 3, '0,6666666667', '0.6666666667'],
    [0.1 + 0.2, '0,3', '0.3'],
    [1.1 * 3, '3,3', '3.3'],
    [1234, '1234', '1,234'],
    [9999, '9999', '9,999'],
    [10000, `10${NBSP}000`, '10,000'],
    [12345.678, `12${NBSP}345,678`, '12,345.678'],
    [-1234567.891, `-1${NBSP}234${NBSP}567,891`, '-1,234,567.891'],
    [123456789012, `123${NBSP}456${NBSP}789${NBSP}012`, '123,456,789,012'],
    [3.1068559611866697, '3,106855961', '3.106855961'],
    [9.99999999999, '10', '10'],
    [0.000001, '0,000001', '0.000001'],
    [1e-7, '1e-7', '1e-7'],
    [-1.25e-7, '-1,25e-7', '-1.25e-7'],
    [1e15, '1e15', '1e15'],
    [1.5e21, '1,5e21', '1.5e21'],
  ])('%s → pl %s / en %s', (n, pl, en) => {
    expect(formatNumber(n, 'pl')).toBe(pl);
    expect(formatNumber(n, 'en')).toBe(en);
  });

  it('formats non-finite values', () => {
    expect(formatNumber(NaN, 'pl')).toBe('NaN');
    expect(formatNumber(Infinity, 'pl')).toBe('∞');
    expect(formatNumber(-Infinity, 'en')).toBe('-∞');
  });

  it('never rounds integers below 1e15', () => {
    expect(formatNumber(999999999999999, 'en')).toBe('999,999,999,999,999');
  });
});

describe('formatFixed / formatMoney', () => {
  it('pads to the requested fraction digits', () => {
    expect(formatFixed(12.5, 2, 'pl')).toBe('12,50');
    expect(formatFixed(12345.5, 2, 'pl')).toBe(`12${NBSP}345,50`);
    expect(formatFixed(-0.001, 2, 'pl')).toBe('0,00');
    expect(formatFixed(Infinity, 2, 'pl')).toBe('∞');
  });

  it('formats money with suffix and prefix symbols', () => {
    expect(formatMoney(17, PLN, 'pl')).toBe('17 zł');
    expect(formatMoney(12.5, PLN, 'pl')).toBe('12,50 zł');
    expect(formatMoney(1 / 3, PLN, 'pl')).toBe('0,33 zł');
    expect(formatMoney(17, USD, 'en')).toBe('$17');
    expect(formatMoney(-5.5, USD, 'en')).toBe('-$5.50');
    expect(formatMoney(1234.5, EUR, 'en')).toBe('1,234.50 €');
    expect(formatMoney(25000, PLN, 'pl')).toBe(`25${NBSP}000 zł`);
  });
});

describe('parseLocaleNumber', () => {
  it.each<[string, number]>([
    ['12', 12],
    ['12,5', 12.5],
    ['12.5', 12.5],
    ['-3', -3],
    ['−5', -5],
    ['+7', 7],
    [',5', 0.5],
    ['1e3', 1000],
    ['1,5e3', 1500],
    ['12 000', 12000],
    [`12${NBSP}000`, 12000],
    ['12 000,25', 12000.25],
    ['1.234,56', 1234.56],
    ['1.000.000', 1000000],
    ['**12**', 12],
    ['`7`', 7],
  ])('pl: %s → %s', (text, value) => {
    expect(parseLocaleNumber(text, 'pl')?.value).toBeCloseTo(value, 10);
  });

  it('interprets commas by locale', () => {
    expect(parseLocaleNumber('1,234', 'pl')?.value).toBe(1.234);
    expect(parseLocaleNumber('1,234', 'en')?.value).toBe(1234);
    expect(parseLocaleNumber('1,234.56', 'en')?.value).toBe(1234.56);
    expect(parseLocaleNumber('1,234,567', 'en')?.value).toBe(1234567);
    expect(parseLocaleNumber('12,5', 'en')?.value).toBe(12.5);
  });

  it('recognises currencies', () => {
    expect(parseLocaleNumber('1 234,50 zł')).toEqual({ value: 1234.5, currency: PLN });
    expect(parseLocaleNumber('€12.50')).toEqual({ value: 12.5, currency: { code: 'EUR', symbol: '€', prefix: true } });
    expect(parseLocaleNumber('12 PLN')?.currency?.code).toBe('PLN');
    expect(parseLocaleNumber('PLN 12')?.currency).toEqual({ code: 'PLN', symbol: 'PLN', prefix: true });
    expect(parseLocaleNumber('-12,50 zł')?.value).toBe(-12.5);
    expect(parseLocaleNumber('-$5')).toEqual({ value: -5, currency: USD });
  });

  it('recognises percentages as fractions', () => {
    expect(parseLocaleNumber('23%')).toEqual({ value: 0.23, percent: true });
    expect(parseLocaleNumber('12,5 %')?.value).toBeCloseTo(0.125);
  });

  it.each(['', '   ', 'abc', '12 abc', '1 2', '5 zł 3', 'zł', '1,2,3', '12..5', '--5', '1 0000', '0x1F'])(
    'rejects %j',
    (text) => {
      expect(parseLocaleNumber(text, 'pl')).toBeNull();
    },
  );
});

describe('stripInlineMarkup', () => {
  it('removes nested emphasis', () => {
    expect(stripInlineMarkup('**_12_**')).toBe('12');
    expect(stripInlineMarkup('~~usunięte~~')).toBe('usunięte');
    expect(stripInlineMarkup('a*b')).toBe('a*b');
  });
});
