import { describe, expect, it } from 'vitest';
import { evaluateCalc, evaluateInline } from './calc';
import { getEngine, GuardedScope, MAX_ELEMENTS } from './engine';
import { broadcastSize } from './guard';
import { CalcError } from './messages';
import { NBSP } from './numbers';

const TOO_LARGE = `Wynik jest za duży (limit ${MAX_ELEMENTS} elementów)`;
const TOO_COMPLEX = 'Obliczenie jest zbyt złożone';
const TOO_BIG = 'Liczba jest za duża';

const inline = (expr: string) => evaluateInline(expr, {}, 'pl');
/** Result (or `!error`) of the last line of a calc block. */
function last(source: string): string | undefined {
  const l = evaluateCalc(source).lines.at(-1)!;
  return l.value ?? (l.error ? `!${l.error}` : undefined);
}

describe('guard: matrix broadcasting and products (memory)', { timeout: 60_000 }, () => {
  it.each([
    'ones(30000, 1) + ones(1, 30000)',
    'ones(30000, 1) - ones(1, 30000)',
    'ones(30000, 1) .* ones(1, 30000)',
    'ones(30000, 1) ./ ones(1, 30000)',
    'ones(30000, 1) == ones(1, 30000)',
    'ones(30000, 1) < ones(1, 30000)',
    'ones(30000, 1) and ones(1, 30000)',
    'ones(30000, 1) or ones(1, 30000)',
    'ones(30000, 1) * ones(1, 30000)',
    'add(ones(30000, 1), ones(1, 30000))',
    'dotPow(ones(30000, 1), ones(1, 30000))',
    '0 < ones(30000, 1) < ones(1, 30000)',
    'map(ones(30000, 1), ones(1, 30000), f(a, b) = a + b)',
  ])('%s is rejected before allocating', (expr) => {
    expect(inline(expr).error).toBe(TOO_LARGE);
  });

  it('still broadcasts and multiplies small matrices', () => {
    expect(inline('[1, 2, 3] + [[10], [20]]').value).toBe('[[11; 12; 13]; [21; 22; 23]]');
    expect(inline('[[1, 2], [3, 4]] * [[5], [6]]').value).toBe('[[17]; [39]]');
    expect(inline('ones(1, 30000) * ones(30000, 1)').value).toBe(`[[30${NBSP}000]]`);
    expect(inline('[1, 2] .* [3, 4]').value).toBe('[3; 8]');
  });

  it('computes broadcast sizes', () => {
    const math = getEngine().math;
    expect(broadcastSize(math, [math.ones(3, 1), math.ones(1, 4)])).toBe(12);
    expect(broadcastSize(math, [[1, 2, 3], 5])).toBe(3);
    expect(broadcastSize(math, [1, 2])).toBe(1);
  });
});

describe('guard: allocation functions', { timeout: 60_000 }, () => {
  it.each([
    'setPowerset(1:30)',
    'setCartesian(1:400, 1:400)',
    'nthRoots(1, 1e7)',
    'subset([1], index(3e6), 1)',
    'f(x) = concat(x, x)\nf(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(ones(1000)))))))))))))))))))))',
    's = "abcdefghij"\nf(x) = concat(x, x)\nf(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(f(s)))))))))))))))))))))))))',
    'format(bignumber(1), {notation: "fixed", precision: 100000000})',
    'print("$a$a$a$a$a$a$a$a$a$a", {a: format(ones(5000))})',
  ])('%s is rejected', (src) => {
    expect(last(src)).toBe(`!${TOO_LARGE}`);
  });

  it('keeps small set and string operations', () => {
    expect(inline('setPowerset([1, 2])').value).toBe('[[]; [1]; [2]; [1; 2]]');
    expect(inline('concat("zażółć", " ", "gęślą")').value).toBe('zażółć gęślą');
    expect(inline('print("$a + $b", {a: 1, b: 2})').value).toBe('1 + 2');
    expect(inline('diag(ones(316, 316))').value).toMatch(/^\[1; 1; 1/);
  });
});

describe('guard: index assignments cannot grow matrices past the limit', () => {
  it.each(['x = [1]\nx[3e6] = 1', 'x = ones(100000)\nx[1, 100000] = 1', 'x = [1]\nx[1, 1:200000] = 1', 's = "a"\ns[1e7] = "b"'])(
    '%s is rejected',
    (src) => {
      expect(last(src)).toBe(`!${TOO_LARGE}`);
    },
  );

  it('keeps ordinary index and property assignments working', () => {
    expect(last('x = [1, 2]\nx[5] = 9\nx')).toBe('[1; 2; 0; 0; 9]');
    expect(last('x = [1, 2]\nx[end + 1] = 9\nx')).toBe('[1; 2; 9]');
    expect(last('x = [[1, 2], [3, 4]]\nx[2, 2] = 9\nx')).toBe('[[1; 2]; [3; 9]]');
    expect(last('a = [1, 2, 3]\na[2:3] = [7, 8]\na')).toBe('[1; 7; 8]');
    expect(last('a = [1, 2, 3]\na[[true, false, true]] = 0\na')).toBe('[0; 2; 0]');
    expect(last('o = {a: 1}\no.a = 5\no.a')).toBe('5');
    expect(last('m = {v: [1, 2]}\nm.v[2] = 5\nm.v')).toBe('[1; 5]');
    expect(last('ilość = [1, 2]\nilość[1] = 3\nilość')).toBe('[3; 2]');
  });

  it('uses the object mathjs actually resizes, even if an index expression changes it', () => {
    // The index expression replaces o.m, but mathjs resizes the original [1, 1000] matrix.
    expect(last('o = {m: ones(1, 1000)}\no.m[(o.m = 1) * 1000, 1] = 5')).toBe(`!${TOO_LARGE}`);
    // A nested index assignment inside an index expression must not disturb the outer check.
    expect(last('o = {m: ones(1000)}\ny = [0]\no.m[1, (y[1] = 1) * 1000] = 5')).toBe(`!${TOO_LARGE}`);
    expect(last('o = {m: [1, 2]}\ny = [0]\no.m[(y[1] = 3) + 1] = 5\no.m')).toBe('[1; 2; 0; 5]');
  });

  it('refuses to store an oversized value through the scope', () => {
    const scope = new GuardedScope(new Map());
    expect(() => scope.set('x', getEngine().math.ones(MAX_ELEMENTS + 1))).toThrow(CalcError);
    expect(() => scope.set('s', 'x'.repeat(200_000))).toThrow(CalcError);
  });
});

describe('guard: CPU budget', { timeout: 60_000 }, () => {
  it('stops exponential recursion with a message and continues with the next line', () => {
    const r = evaluateCalc('f(n) = n < 1 ? 1 : f(n - 1) + f(n - 1)\nf(40)\nf(10)\n2 + 2');
    expect(r.lines[1]!.error).toBe(TOO_COMPLEX);
    expect(r.lines[2]!.value).toBe('1024');
    expect(r.lines[3]!.value).toBe('4');
  });

  it.each(['combinations(1e9, 5e8)', 'isPrime(ones(10000) * 1e15)', 'solveODE(f(t, y) = y, [0, 10], 1, {maxIter: 1e12})', 'ones(316, 316) * ones(316, 316)'])(
    '%s is rejected quickly',
    (expr) => {
      const t0 = Date.now();
      expect(inline(expr).error).toBe(TOO_COMPLEX);
      expect(Date.now() - t0).toBeLessThan(5000);
    },
  );

  it('allows realistic workloads', () => {
    expect(inline('combinations(10, 3)').value).toBe('120');
    expect(inline('sum(1:100000)').value).toBe(`5${NBSP}000${NBSP}050${NBSP}000`);
    expect(inline('f(n) = n < 2 ? n : f(n - 1) + f(n - 2)').value).toBe('f(n)');
    expect(last('fib(n) = n < 2 ? n : fib(n - 1) + fib(n - 2)\nfib(15)')).toBe('610');
    expect(inline('det([[1, 2], [3, 4]])').value).toBe('-2');
  });

  it('limits the total work of one calc block, but not the next call', () => {
    const line = 'sum(ones(90000) + ones(90000))';
    const { lines } = evaluateCalc(Array.from({ length: 60 }, () => line).join('\n'));
    expect(lines[0]!.value).toBe(`180${NBSP}000`);
    expect(lines.at(-1)!.error).toBe(TOO_COMPLEX);
    expect(evaluateCalc(line).lines[0]!.value).toBe(`180${NBSP}000`);
  });

  it('meters direct engine calls per call', () => {
    const scope = new GuardedScope(new Map());
    const engine = getEngine();
    engine.evaluate('h(n) = n < 1 ? 1 : h(n - 1) + h(n - 1)', scope);
    // Each call alone fits the budget; together they exceed MAX_TOTAL_WORK, so each call must
    // start afresh.
    for (let i = 0; i < 25; i++) expect(engine.evaluate('sum(ones(90000) + ones(90000))', scope)).toBe(180_000);
    expect(engine.evaluate('h(10)', scope)).toBe(1024);
    expect(() => engine.evaluate('h(40)', scope)).toThrow(CalcError);
  });
});

describe('guard: exact numbers (fraction, bigint)', { timeout: 60_000 }, () => {
  it.each([
    'x = fraction(3, 7)\nf(a, n) = n == 0 ? a : f(a * a, n - 1)\nf(x, 30)',
    'fraction(3)^10000000',
    'bigint(3)^bigint(3000000)',
    'bigint(2)^bigint(6000) * bigint(2)^bigint(6000)',
  ])('%s is rejected', (src) => {
    const t0 = Date.now();
    expect(last(src)).toBe(`!${TOO_BIG}`);
    expect(Date.now() - t0).toBeLessThan(4000);
  });

  it('bounds BigNumber factorial loops', () => {
    expect(inline('factorial(bignumber(10000000))').error).toBe(TOO_COMPLEX);
    expect(inline('factorial(bignumber(10))').value).toBe(`3${NBSP}628${NBSP}800`);
  });

  it('keeps small exact arithmetic', () => {
    expect(inline('fraction(1, 3) + fraction(1, 6)').value).toBe('0,5');
    expect(inline('factorial(bigint(20))').value).toBe('2,432902008e18');
    expect(inline('fraction(1)^100000').value).toBe('1');
    expect(inline('bigint(2)^bigint(100)').value).toBe('1,2676506e30');
  });
});

describe('guard: operator semantics are preserved', () => {
  it('keeps short-circuit evaluation', () => {
    for (const [expr, value] of [
      ['false and brak', 'fałsz'],
      ['true or brak', 'prawda'],
      ['0 & brak', '0'],
      ['3 ?? brak', '3'],
      ['3 < 2 < brak', 'fałsz'],
      ['1 < 2 < 3', 'prawda'],
      ['1 < 2 <= 2 > 1', 'prawda'],
      ['true and false', 'fałsz'],
      ['null ?? 5', '5'],
      ['5 & 3', '1'],
      ['5 | 2', '7'],
      ['not true', 'fałsz'],
      ['true xor false', 'prawda'],
    ] as const) {
      expect(inline(expr).value, expr).toBe(value);
    }
    expect(inline('true and brak').error).toBe('Nieznana zmienna: brak');
  });

  it('keeps unary, postfix and conversion operators', () => {
    expect(inline('-(2 + 3)').value).toBe('-5');
    expect(inline('5!').value).toBe('120');
    expect(inline("[[1, 2], [3, 4]]'").value).toBe('[[1; 3]; [2; 4]]');
    expect(inline('2 km + 3 m to cm').value).toBe(`200${NBSP}300 cm`);
    expect(inline('10 mod 3').value).toBe('1');
    expect(inline('2^10').value).toBe('1024');
  });

  it('keeps map / filter / forEach with callbacks and inline expressions', () => {
    expect(inline('map([1, 2, 3], f(x) = x^2)').value).toBe('[1; 4; 9]');
    expect(inline('filter(1:10, x > 7)').value).toBe('[8; 9; 10]');
    expect(inline('filter([1, -2, 3], f(x) = x > 0)').value).toBe('[1; 3]');
    expect(inline('map([1, 2], [10, 20], f(a, b) = a + b)').value).toBe('[11; 22]');
    expect(inline('map([1, 4, 9], sqrt)').value).toBe('[1; 2; 3]');
    expect(inline('filter([-1, 2, 0, 3], isPositive)').value).toBe('[2; 3]');
    expect(inline('apply([[1, 2], [3, 4]], 1, sum)').value).toBe('[4; 6]');
    // Each callback result is checked and metered: the map stops after a few allocations.
    expect(inline('map(ones(1000) * 100000, zeros)').error).toBe(TOO_COMPLEX);
    expect(inline('forEach([1, 2], f(x) = x)').error).toBeUndefined();
  });

  it('meters function values passed around', () => {
    expect(last('g = sqrt\ng(16)')).toBe('4');
    expect(last('k = map\nk([1, 2], f(x) = x * 2)')).toBe('[2; 4]');
  });
});

describe('guard: namespace lookups', () => {
  it('only exposes own namespace functions, never prototype members or helpers', () => {
    const engine = getEngine();
    expect(typeof engine.functionFor('sin')).toBe('function');
    for (const name of ['constructor', 'toString', 'hasOwnProperty', '__proto__', '$$op_add', '$$rel', 'evaluate', 'pi', 'nothing']) {
      expect(engine.functionFor(name), name).toBeUndefined();
    }
  });

  it('cannot reach the internal helpers from note text', () => {
    expect(inline('$$op_add(1, 2)').value).not.toBe('3');
    expect(inline('CONSTRUCTOR(1)').error).toBe('Nieznana funkcja: CONSTRUCTOR');
  });
});
