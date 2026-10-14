import { describe, expect, it } from 'vitest';
import { emitTokens, lex, matchBrackets, significant } from './lexer';

const kinds = (src: string) => significant(lex(src).tokens).map((t) => `${t.kind}:${t.text}`);

describe('lex', () => {
  it('splits a simple expression', () => {
    expect(kinds('cena * 2')).toEqual(['ident:cena', 'op:*', 'num:2']);
  });

  it('reads Unicode identifiers', () => {
    expect(kinds('ilość + żółć_2')).toEqual(['ident:ilość', 'op:+', 'ident:żółć_2']);
  });

  it('reads numbers in several notations', () => {
    expect(kinds('1e5 + .5 + 1.25 + 0x1F + 2E-3')).toEqual([
      'num:1e5',
      'op:+',
      'num:.5',
      'op:+',
      'num:1.25',
      'op:+',
      'num:0x1F',
      'op:+',
      'num:2E-3',
    ]);
  });

  it('keeps commas separate from numbers (merged later by locale)', () => {
    expect(kinds('12,5')).toEqual(['num:12', 'comma:,', 'num:5']);
  });

  it('strips trailing comments', () => {
    expect(lex('2 + 3 # suma').comment).toBe('suma');
    expect(lex('2 + 3 // suma').comment).toBe('suma');
    expect(kinds('2 + 3 // suma')).toEqual(['num:2', 'op:+', 'num:3']);
  });

  it('does not treat # inside strings as comments', () => {
    const r = lex('"a#b" + \'c//d\'');
    expect(r.comment).toBeUndefined();
    expect(significant(r.tokens).map((t) => t.kind)).toEqual(['str', 'op', 'str']);
  });

  it("distinguishes transpose from single-quoted strings", () => {
    expect(kinds("a'")).toEqual(['ident:a', "op:'"]);
    expect(kinds("'abc'")).toEqual(["str:'abc'"]);
  });

  it('handles an unterminated string', () => {
    expect(kinds('"abc')).toEqual(['str:"abc']);
  });

  it('maps Unicode math symbols', () => {
    expect(emitTokens(lex('2 × 3 ÷ 4 − 1').tokens)).toBe('2 * 3 / 4 - 1');
    expect(emitTokens(lex('a ≤ b ≥ c ≠ d').tokens)).toBe('a <= b >= c != d');
    expect(emitTokens(lex('m²').tokens)).toBe('m^(2)');
    expect(emitTokens(lex('10⁻³').tokens)).toBe('10^(-3)');
    expect(emitTokens(lex('2π').tokens)).toBe('2pi');
    expect(emitTokens(lex('20°C').tokens)).toBe('20 degC');
    expect(emitTokens(lex('90°').tokens)).toBe('90 deg');
    expect(emitTokens(lex('∞').tokens)).toBe('Infinity');
  });

  it('recognises currency symbols', () => {
    expect(kinds('$12 + 5€ + £1')).toEqual(['cur:$', 'num:12', 'op:+', 'num:5', 'cur:€', 'op:+', 'cur:£', 'num:1']);
  });

  it('reads multi-character operators', () => {
    expect(kinds('a == b != c <= d >= e .* f')).toEqual([
      'ident:a',
      'op:==',
      'ident:b',
      'op:!=',
      'ident:c',
      'op:<=',
      'ident:d',
      'op:>=',
      'ident:e',
      'op:.*',
      'ident:f',
    ]);
  });

  it('turns unknown characters into "other" tokens', () => {
    expect(kinds('2 @ 3')).toEqual(['num:2', 'other:@', 'num:3']);
    expect(kinds('😀')).toEqual(['other:😀']);
  });

  it('never throws on arbitrary input', () => {
    for (const s of ['', '((((', ']]]', '"', "'", '\\', '\u0000', '#', '//']) {
      expect(() => lex(s)).not.toThrow();
    }
  });
});

describe('matchBrackets', () => {
  it('pairs nested brackets and marks unbalanced ones', () => {
    const { tokens } = lex('f([1, (2)], 3');
    const pairs = matchBrackets(tokens);
    const open = tokens.findIndex((t) => t.text === '(');
    expect(pairs.get(open)).toBe(-1);
    const square = tokens.findIndex((t) => t.text === '[');
    expect(tokens[pairs.get(square)!]!.text).toBe(']');
  });
});
