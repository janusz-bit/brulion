import { describe, expect, it } from 'vitest';
import { demangleText, isIdentifier, sanitizeIdentifier, toDisplayName, toInternalName } from './names';

describe('identifier mangling', () => {
  it('keeps ASCII names unchanged', () => {
    expect(toInternalName('cena_netto')).toBe('cena_netto');
    expect(toInternalName('x1')).toBe('x1');
  });

  it('escapes non-ASCII characters', () => {
    expect(toInternalName('ilość')).toBe('ilo$15b$$107$');
    expect(toInternalName('ilość')).toMatch(/^[A-Za-z0-9_$]+$/);
  });

  it.each(['ilość', 'żółć', 'ZAŻÓŁĆ_gęślą_jaźń', 'цена', 'α', 'cena_netto', 'kąt2', '日本'])('round-trips %s', (name) => {
    expect(toDisplayName(toInternalName(name))).toBe(name);
  });

  it('normalises to NFC first', () => {
    const decomposed = 'ilos\u0301c\u0301';
    expect(toInternalName(decomposed)).toBe(toInternalName('ilość'));
  });

  it('shows temporaries without their prefix', () => {
    expect(toDisplayName('$$sum')).toBe('sum');
  });

  it('demangles names inside messages', () => {
    expect(demangleText('Undefined symbol ilo$15b$$107$')).toBe('Undefined symbol ilość');
    expect(demangleText('a + $$sum')).toBe('a + sum');
    expect(demangleText('plain text 12$')).toBe('plain text 12$');
  });
});

describe('isIdentifier / sanitizeIdentifier', () => {
  it('validates identifiers', () => {
    expect(isIdentifier('ilość')).toBe(true);
    expect(isIdentifier('_x')).toBe(true);
    expect(isIdentifier('2x')).toBe(false);
    expect(isIdentifier('a b')).toBe(false);
    expect(isIdentifier('a-b')).toBe(false);
  });

  it.each<[string, string | null]>([
    ['due-date', 'due_date'],
    ['cena netto', 'cena_netto'],
    ['Cena', 'Cena'],
    ['2026', '_2026'],
    ['  kurs EUR ', 'kurs_EUR'],
    ['', null],
    ['---', null],
  ])('%j → %j', (key, expected) => {
    expect(sanitizeIdentifier(key)).toBe(expected);
  });
});

describe('toDisplayName — review regressions', () => {
  it('leaves sequences that are not valid code points alone', () => {
    expect(toDisplayName('a$ffffff$b')).toBe('a$ffffff$b');
    expect(demangleText('Cannot convert "$110000$" to a number')).toBe('Cannot convert "$110000$" to a number');
    expect(toDisplayName('ilo$15b$$107$')).toBe('ilość');
  });
});
