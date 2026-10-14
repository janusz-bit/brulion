/**
 * Resource limits of the calc engine: sizes of values (matrix elements, string length, bits of
 * exact numbers) and a deterministic work budget, so that a note — which may come from anywhere
 * (imports, sync) — cannot freeze or crash the app with one expression.
 *
 * Work is counted in abstract units (≈ one scalar operation, or two elements of a collection
 * touched) by the guarded operators and functions (see guard.ts). Every expression gets `MAX_WORK` units and
 * every public call (a calc block, an inline calculation, a table, a page scope) `MAX_TOTAL_WORK`
 * units in total; exceeding either aborts with "too complex".
 */
import type { MathJsInstance } from 'mathjs';
import { CalcError } from './messages';

/** Maximum number of elements of any matrix created, returned or stored. */
export const MAX_ELEMENTS = 100_000;
/** Maximum length of a string result. */
export const MAX_STRING_LENGTH = 100_000;
/** Maximum length of one expression (after preprocessing). */
export const MAX_EXPRESSION_LENGTH = 4_000;
/** Work units one expression (one calc line, one inline calculation, one cell) may use. */
export const MAX_WORK = 1_000_000;
/** Work units one public call (calc block, table, page scope) may use in total. */
export const MAX_TOTAL_WORK = 5_000_000;
/**
 * Largest exact number (fraction numerator/denominator, bigint) in bits (~3000 digits). Kept
 * small because fraction arithmetic normalises with a gcd that is quadratic in the size.
 */
export const MAX_EXACT_BITS = 10_000;

const EXACT_LIMIT = 1n << BigInt(MAX_EXACT_BITS);

interface Meter {
  /** Nesting depth of `withBudget`; work is only metered inside an evaluation. */
  depth: number;
  expression: number;
  total: number;
}

const meter: Meter = { depth: 0, expression: 0, total: 0 };

/**
 * Run `fn` as one metered unit of work. The outermost call resets the total budget; nested calls
 * share it. Use around every public entry point that evaluates expressions.
 */
export function withBudget<T>(fn: () => T): T {
  if (meter.depth === 0) {
    meter.total = 0;
    meter.expression = 0;
  }
  meter.depth++;
  try {
    return fn();
  } finally {
    meter.depth--;
  }
}

/** Start a new expression: resets the per-expression budget (the total keeps counting). */
export function startExpression(): void {
  meter.expression = 0;
}

/**
 * Account for `units` of work; throws "too complex" when a budget is exhausted. A no-op outside
 * `withBudget` (e.g. when the app calls a function value from a returned scope directly).
 */
export function charge(units: number): void {
  if (meter.depth === 0) return;
  const u = Number.isNaN(units) ? 1 : units;
  meter.expression += u;
  meter.total += u;
  if (!(meter.expression <= MAX_WORK && meter.total <= MAX_TOTAL_WORK)) {
    // One shared instance: once the budget is spent, thousands of cells may fail in a row.
    tooComplex ??= new CalcError('calc.error.tooComplex');
    throw tooComplex;
  }
}

let tooComplex: CalcError | undefined;

/**
 * Work units for touching `n` elements of a collection. Measured on a Raspberry Pi 4: about
 * 0.4 µs per element in mathjs collection functions vs about 1 µs per guarded scalar operation.
 */
export function elementWork(n: number): number {
  return n / 2;
}

/** Throw "too large" unless `n` elements are allowed. */
export function checkElements(n: number): void {
  if (!(n <= MAX_ELEMENTS)) throw new CalcError('calc.error.tooLarge', { max: MAX_ELEMENTS });
}

/** Throw "too large" unless a string of `n` characters is allowed. */
export function checkChars(n: number): void {
  if (!(n <= MAX_STRING_LENGTH)) throw new CalcError('calc.error.tooLarge', { max: MAX_STRING_LENGTH });
}

/** Number of scalar elements of a mathjs value (1 for scalars). */
export function elementCount(math: MathJsInstance, value: unknown): number {
  if (Array.isArray(value)) {
    let n = 0;
    for (const v of value) n += elementCount(math, v);
    return n;
  }
  if (math.isMatrix(value)) {
    return (value.size() as number[]).reduce((a, b) => a * b, 1);
  }
  return 1;
}

/** Dimensions of a matrix / (rectangular) nested array, or null for scalars. */
export function shapeOf(math: MathJsInstance, value: unknown): number[] | null {
  if (math.isMatrix(value)) return value.size() as number[];
  if (!Array.isArray(value)) return null;
  const dims: number[] = [];
  let cur: unknown = value;
  while (Array.isArray(cur)) {
    dims.push(cur.length);
    cur = cur[0];
  }
  return dims;
}

function absBigInt(v: bigint): bigint {
  return v < 0n ? -v : v;
}

/** True for a bigint or fraction whose size exceeds `MAX_EXACT_BITS`. */
export function isTooBigExact(math: MathJsInstance, value: unknown): boolean {
  if (typeof value === 'bigint') return absBigInt(value) >= EXACT_LIMIT;
  if (math.isFraction(value)) {
    const f = value as unknown as { n: bigint | number; d: bigint | number };
    return typeof f.n === 'bigint' && typeof f.d === 'bigint' && (absBigInt(f.n) >= EXACT_LIMIT || f.d >= EXACT_LIMIT);
  }
  return false;
}

/** Bit length of an exact number (bigint, or the larger part of a fraction); 0 otherwise. */
export function exactBits(math: MathJsInstance, value: unknown): number {
  const bits = (v: bigint) => {
    if (v === 0n) return 0;
    const hex = absBigInt(v).toString(16);
    return (hex.length - 1) * 4 + (32 - Math.clz32(parseInt(hex[0]!, 16)));
  };
  if (typeof value === 'bigint') return bits(value);
  if (math.isFraction(value)) {
    const f = value as unknown as { n: bigint | number; d: bigint | number };
    if (typeof f.n === 'bigint' && typeof f.d === 'bigint') return Math.max(bits(f.n), bits(f.d));
  }
  return 0;
}

/** How many elements of a collection are sampled for oversized exact numbers. */
const EXACT_SAMPLE = 16;

function hasTooBigExact(math: MathJsInstance, value: unknown): boolean {
  if (isTooBigExact(math, value)) return true;
  let items: unknown;
  if (Array.isArray(value)) items = value;
  // DenseMatrix.valueOf() returns its data without copying; sparse matrices are not sampled.
  else if (math.isMatrix(value) && (value as { type?: string }).type === 'DenseMatrix') items = value.valueOf();
  else return false;
  let seen = 0;
  const walk = (v: unknown): boolean => {
    if (seen >= EXACT_SAMPLE) return false;
    if (Array.isArray(v)) return v.some(walk);
    seen++;
    return isTooBigExact(math, v);
  };
  return walk(items);
}

/** Throws when a value is too big to keep (huge matrix or string, enormous exact number). */
export function checkResultSize(math: MathJsInstance, value: unknown): void {
  if (typeof value === 'string') {
    checkChars(value.length);
    return;
  }
  if (Array.isArray(value) || math.isMatrix(value)) checkElements(elementCount(math, value));
  if ((typeof value === 'bigint' || typeof value === 'object') && hasTooBigExact(math, value)) {
    throw new CalcError('calc.error.tooBig');
  }
}
