import { describe, expect, it } from 'vitest';
import { getEngine } from './engine';
import {
  charge,
  checkResultSize,
  elementCount,
  exactBits,
  isTooBigExact,
  MAX_ELEMENTS,
  MAX_EXACT_BITS,
  MAX_STRING_LENGTH,
  MAX_TOTAL_WORK,
  MAX_WORK,
  shapeOf,
  startExpression,
  withBudget,
} from './limits';
import { CalcError } from './messages';

const math = () => getEngine().math;

describe('work budget', () => {
  it('is not metered outside an evaluation', () => {
    expect(() => charge(MAX_TOTAL_WORK * 10)).not.toThrow();
  });

  it('throws "too complex" when one expression exceeds its budget', () => {
    const err = withBudget(() => {
      try {
        charge(MAX_WORK + 1);
        return null;
      } catch (e) {
        return e;
      }
    });
    expect(err).toBeInstanceOf(CalcError);
    expect((err as CalcError).key).toBe('calc.error.tooComplex');
  });

  it('gives every expression its own budget but caps the total', () => {
    withBudget(() => {
      for (let i = 0; i < MAX_TOTAL_WORK / MAX_WORK; i++) {
        startExpression();
        charge(MAX_WORK);
      }
      startExpression();
      expect(() => charge(1)).toThrow(CalcError);
    });
  });

  it('resets on the next outermost call and shares the budget when nested', () => {
    withBudget(() => charge(MAX_WORK));
    expect(() => withBudget(() => charge(MAX_WORK))).not.toThrow();
    expect(() =>
      withBudget(() => {
        charge(MAX_WORK);
        withBudget(() => charge(1));
      }),
    ).toThrow(CalcError);
  });

  it('restores the nesting depth when the callback throws', () => {
    expect(() =>
      withBudget(() => {
        throw new Error('x');
      }),
    ).toThrow('x');
    expect(() => charge(MAX_TOTAL_WORK * 10)).not.toThrow();
  });

  it('treats NaN as one unit and Infinity as exhausted', () => {
    withBudget(() => {
      expect(() => charge(NaN)).not.toThrow();
      expect(() => charge(Infinity)).toThrow(CalcError);
    });
  });
});

describe('value sizes', () => {
  it('measures shapes and element counts', () => {
    const m = math();
    expect(shapeOf(m, m.zeros(2, 3))).toEqual([2, 3]);
    expect(shapeOf(m, [[1, 2, 3]])).toEqual([1, 3]);
    expect(shapeOf(m, 5)).toBeNull();
    expect(shapeOf(m, 'abc')).toBeNull();
    expect(elementCount(m, [[1], [2, 3]])).toBe(3);
  });

  it('measures exact numbers in bits', () => {
    const m = math();
    expect(exactBits(m, 0n)).toBe(0);
    expect(exactBits(m, 1n)).toBe(1);
    expect(exactBits(m, 255n)).toBe(8);
    expect(exactBits(m, -256n)).toBe(9);
    expect(exactBits(m, m.fraction(1, 3))).toBe(2);
    expect(exactBits(m, 1.5)).toBe(0);
    expect(isTooBigExact(m, 1n << BigInt(MAX_EXACT_BITS))).toBe(true);
    expect(isTooBigExact(m, (1n << BigInt(MAX_EXACT_BITS)) - 1n)).toBe(false);
  });

  it('checks results', () => {
    const m = math();
    expect(() => checkResultSize(m, 'x'.repeat(MAX_STRING_LENGTH))).not.toThrow();
    expect(() => checkResultSize(m, 'x'.repeat(MAX_STRING_LENGTH + 1))).toThrow(CalcError);
    expect(() => checkResultSize(m, m.zeros(MAX_ELEMENTS + 1))).toThrow(CalcError);
    expect(() => checkResultSize(m, 1n << BigInt(MAX_EXACT_BITS + 1))).toThrow(CalcError);
    expect(() => checkResultSize(m, m.matrix([1n << BigInt(MAX_EXACT_BITS + 1)]))).toThrow(CalcError);
    expect(() => checkResultSize(m, { a: 1 })).not.toThrow();
    expect(() => checkResultSize(m, null)).not.toThrow();
  });
});
