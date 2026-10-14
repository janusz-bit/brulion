/**
 * Runtime guards of the sandboxed mathjs engine (see limits.ts for the limits themselves).
 *
 * mathjs evaluates operators (`a + b`), relational chains (`a < b < c`) and index assignments
 * (`x[5] = 1`) with functions bound at compile time, so they cannot be intercepted through the
 * evaluation scope. `guardTree` therefore rewrites the parsed expression before compiling it:
 * * `a + b` → operator node calling `$$op_add` (every operator goes through a guarded helper),
 * * `a and b` → `$$lazy_and` (a raw-args helper that keeps short-circuit evaluation),
 * * `a < b <= c` → `$$rel("smaller,smallerEq", a, b, c)`,
 * * `x[i, j] = v` → `x[$$dim(0, 2, ctx, i), $$dim(1, 2, ctx, j)] = v`, where `ctx` evaluates to
 *   the matrix being assigned into (checks the size it would be resized to *before* mathjs
 *   allocates it).
 * The helpers live in the engine namespace under `$$` names, which user input can never produce
 * (the lexer turns `$` into a currency symbol and mangled names never start with `$$`).
 *
 * Function calls and function values (`concat(...)`, `map(x, sqrt)`) are resolved through the
 * scope first, where `GuardedScope` hands out guarded wrappers of the namespace functions
 * (`wrapNamespaceFunction`; typed functions stay typed so callbacks keep working).
 *
 * A guarded call estimates the size of the result from the arguments (matrix broadcasting,
 * matrix products, Cartesian products, power sets, allocation sizes, string lengths, bits of
 * exact numbers) and rejects it *before* calling mathjs when it would exceed a limit; it then
 * charges the work budget and checks the actual result.
 */
import type { AccessorNode, ConstantNode, EvalFunction, MathJsInstance, MathNode, SymbolNode } from 'mathjs';
import {
  charge,
  checkChars,
  checkElements,
  checkResultSize,
  elementCount,
  elementWork,
  exactBits,
  isTooBigExact,
  MAX_EXACT_BITS,
  shapeOf,
} from './limits';
import { CalcError } from './messages';

type AnyFn = (...args: unknown[]) => unknown;

/** Prefix of the guarded operator helpers (`a + b` → `$$op_add(a, b)`). */
export const OP_PREFIX = '$$op_';
/** Prefix of the short-circuiting operator helpers (`a and b` → `$$lazy_and(a, b)`). */
export const LAZY_PREFIX = '$$lazy_';
/** Helper evaluating a relational chain (`a < b < c`). */
export const REL_NAME = '$$rel';
/** Helper checking one dimension of an index assignment (`x[i] = v`). */
export const DIM_NAME = '$$dim';

/** Operator functions of the mathjs parser that evaluate all operands. */
export const EAGER_OPS: readonly string[] = [
  'add',
  'subtract',
  'multiply',
  'divide',
  'dotMultiply',
  'dotDivide',
  'dotPow',
  'pow',
  'mod',
  'unaryMinus',
  'unaryPlus',
  'bitNot',
  'not',
  'factorial',
  'ctranspose',
  'transpose',
  'to',
  'equal',
  'unequal',
  'smaller',
  'larger',
  'smallerEq',
  'largerEq',
  'bitXor',
  'leftShift',
  'rightArithShift',
  'rightLogShift',
  'xor',
];

/** Operators mathjs evaluates lazily (short-circuit, raw-args transforms). */
export const LAZY_OPS = ['and', 'or', 'bitAnd', 'bitOr', 'nullish'] as const;
type LazyOp = (typeof LAZY_OPS)[number];

const EAGER_OP_SET: ReadonlySet<string> = new Set(EAGER_OPS);
const LAZY_OP_SET: ReadonlySet<string> = new Set(LAZY_OPS);

// ---------------------------------------------------------------------------
// Size estimates
// ---------------------------------------------------------------------------

/** What a call is expected to produce / cost, computed from its arguments. */
interface Estimate {
  /** Elements of the result (checked against MAX_ELEMENTS before the call). */
  elements?: number;
  /** Characters of a string result. */
  chars?: number;
  /** Bits of an exact (fraction / bigint) result. */
  bits?: number;
  /** Work beyond touching the inputs (e.g. n³ for a matrix inverse, loop counts). */
  work?: number;
}

type Estimator = (math: MathJsInstance, args: readonly unknown[]) => Estimate;

function toNum(math: MathJsInstance, v: unknown): number | null {
  if (typeof v === 'number') return v;
  if (typeof v === 'bigint') return Number(v);
  if (math.isBigNumber(v) || math.isFraction(v)) return Number(v.valueOf());
  return null;
}

function isCollection(math: MathJsInstance, v: unknown): boolean {
  return Array.isArray(v) || math.isMatrix(v);
}

/** Numbers found in size-like arguments (`zeros(2, 3)`, `zeros([2, 3])`, `zeros(matrix([2,3]))`). */
function sizeNumbers(math: MathJsInstance, args: readonly unknown[]): number[] {
  const out: number[] = [];
  for (const a of args) {
    const n = toNum(math, a);
    if (n !== null) out.push(n);
    else if (Array.isArray(a)) out.push(...sizeNumbers(math, a));
    else if (math.isMatrix(a)) out.push(...sizeNumbers(math, a.toArray() as unknown[]));
  }
  return out;
}

function product(nums: readonly number[]): number {
  return nums.reduce((a, b) => a * Math.max(0, Math.abs(b)), 1);
}

/** Elements of the broadcast result of element-wise operations on the given arguments. */
export function broadcastSize(math: MathJsInstance, args: readonly unknown[]): number {
  const shapes = args.map((a) => shapeOf(math, a)).filter((s): s is number[] => s !== null);
  const rank = Math.max(0, ...shapes.map((s) => s.length));
  let total = 1;
  for (let i = 1; i <= rank; i++) {
    total *= Math.max(...shapes.map((s) => s[s.length - i] ?? 1));
  }
  return total;
}

/** Order of the (square) matrices among the arguments: the largest of their first two sizes. */
function matrixOrder(math: MathJsInstance, args: readonly unknown[]): number {
  let n = 0;
  for (const a of args) {
    const s = shapeOf(math, a);
    if (s) n = Math.max(n, s[0] ?? 0, s[1] ?? 0);
  }
  return n;
}

function numbersIn(math: MathJsInstance, v: unknown): unknown[] {
  if (Array.isArray(v)) return v.flatMap((x) => numbersIn(math, x));
  if (math.isMatrix(v)) return numbersIn(math, v.toArray());
  return [v];
}

/** Characters a value occupies when printed (rough upper bound). */
function printedChars(math: MathJsInstance, v: unknown, depth = 0): number {
  if (typeof v === 'string') return v.length;
  if (isCollection(math, v)) return elementCount(math, v) * 24;
  if (v && typeof v === 'object' && depth < 2 && Object.getPrototypeOf(v) === Object.prototype) {
    return Object.values(v).reduce<number>((sum, x) => sum + printedChars(math, x, depth + 1), 0);
  }
  return 32;
}

/** Elements of `object` after assigning at `index` (mathjs resizes the matrix as needed). */
function assignedSize(math: MathJsInstance, object: unknown, index: unknown): number | undefined {
  if (!math.isIndex(index)) return undefined;
  const max = (index as unknown as { max(): number[] }).max();
  const current = typeof object === 'string' ? [object.length] : (shapeOf(math, object) ?? []);
  let total = 1;
  for (let d = 0; d < Math.max(max.length, current.length); d++) {
    total *= Math.max(current[d] ?? 1, (max[d] ?? 0) + 1);
  }
  return total;
}

/** Matrix product sizes: [m,k]·[k,n] → m×n elements, m·k·n multiplications. */
const multiplyEstimate: Estimator = (math, args) => {
  let shape: number[] | null = null;
  let work = 0;
  for (const a of args) {
    const s = shapeOf(math, a);
    if (!s) continue;
    if (!shape) {
      shape = s;
      continue;
    }
    const rows: number = shape.length === 1 ? 1 : (shape[0] ?? 0);
    const inner: number = shape.length === 1 ? (shape[0] ?? 0) : (shape[1] ?? 0);
    const cols: number = s.length === 1 ? 1 : (s[1] ?? 0);
    work += rows * inner * cols;
    shape = [rows, cols];
  }
  return { elements: shape ? product(shape) : 1, work };
};

/** Matrix power by squaring, or the bits of an exact power (`fraction(3)^1e7`). */
const powEstimate: Estimator = (math, [base, exponent]) => {
  const p = Math.abs(toNum(math, exponent) ?? 1);
  if (isCollection(math, base)) {
    const n = matrixOrder(math, [base]);
    return { elements: n * n, work: n ** 3 * (Math.log2(p + 1) + 1) };
  }
  const bits = exactBits(math, base);
  return bits > 0 ? { bits: Math.max(0, bits - 1) * p, work: p } : {};
};

/** Exact factorials (bigint / BigNumber) loop n times and have ~n·log2(n) bits. */
const factorialEstimate: Estimator = (math, [n]) => {
  if (typeof n !== 'bigint' && !math.isBigNumber(n) && !math.isFraction(n)) return {};
  const v = Math.abs(toNum(math, n) ?? 0);
  return { work: v, ...(typeof n === 'bigint' ? { bits: v * Math.log2(v + 1) } : {}) };
};

const cubic =
  (factor: number): Estimator =>
  (math, args) => ({ work: factor * matrixOrder(math, args) ** 3 });

const loop =
  (count: (nums: number[]) => number): Estimator =>
  (math, args) => {
    const nums = args.map((a) => Math.abs(toNum(math, a) ?? 0));
    return { work: count(nums) };
  };

const ESTIMATORS: Record<string, Estimator> = {
  zeros: (m, a) => ({ elements: product(sizeNumbers(m, a)) }),
  ones: (m, a) => ({ elements: product(sizeNumbers(m, a)) }),
  identity: (m, a) => {
    const dims = sizeNumbers(m, a);
    return { elements: dims.length === 1 ? product([dims[0]!, dims[0]!]) : product(dims) };
  },
  random: (m, a) => ({ elements: isCollection(m, a[0]) ? product(sizeNumbers(m, [a[0]])) : 1 }),
  randomInt: (m, a) => ({ elements: isCollection(m, a[0]) ? product(sizeNumbers(m, [a[0]])) : 1 }),
  pickRandom: (m, a) => {
    const opt = a[1] as { number?: unknown } | undefined;
    const n = toNum(m, opt) ?? (opt && typeof opt === 'object' ? toNum(m, opt.number) : null);
    return { elements: n === null ? 1 : Math.abs(n) };
  },
  matrixFromFunction: (m, a) => ({ elements: product(sizeNumbers(m, [a[0]])) }),
  resize: (m, a) => ({ elements: product(sizeNumbers(m, [a[1]])) }),
  reshape: (m, a) => ({ elements: product(sizeNumbers(m, [a[1]])) }),
  diag: (m, [x, k]) => {
    const s = shapeOf(m, x);
    if (!s) return {};
    if (s.length === 1) {
      const n = (s[0] ?? 0) + Math.abs(toNum(m, k) ?? 0);
      return { elements: n * n };
    }
    return { elements: Math.min(s[0] ?? 0, s[1] ?? 0) };
  },
  kron: (m, a) => ({ elements: elementCount(m, a[0]) * elementCount(m, a[1]) }),
  concat: (m, a) => {
    const strings = a.filter((x): x is string => typeof x === 'string');
    if (strings.length === a.length - (typeof a[a.length - 1] === 'number' ? 1 : 0) && strings.length) {
      return { chars: strings.reduce((n, s) => n + s.length, 0) };
    }
    return { elements: a.reduce<number>((n, x) => n + (isCollection(m, x) ? elementCount(m, x) : 0), 0) };
  },
  setCartesian: (m, a) => ({ elements: elementCount(m, a[0]) * elementCount(m, a[1]) * 2 }),
  setPowerset: (m, a) => {
    const n = elementCount(m, a[0]);
    return { elements: n > 40 ? Infinity : 2 ** n * Math.max(1, n / 2) };
  },
  nthRoots: (m, a) => ({ elements: Math.abs(toNum(m, a[1]) ?? 2) }),
  multiply: multiplyEstimate,
  divide: (m, [a, b]) => {
    if (!isCollection(m, b)) return {};
    const n = matrixOrder(m, [b]);
    return { elements: Math.max(1, elementCount(m, a)), work: n ** 3 + elementCount(m, a) * n };
  },
  pow: powEstimate,
  dotPow: (m, a) => (isCollection(m, a[0]) || isCollection(m, a[1]) ? {} : powEstimate(m, a)),
  factorial: factorialEstimate,
  gamma: factorialEstimate,
  combinations: loop(([n = 0, k = 0]) => Math.max(0, Math.min(k, n - k))),
  combinationsWithRep: loop(([, k = 0]) => k),
  permutations: loop(([n = 0, k]) => k ?? n),
  stirlingS2: loop(([n = 0, k = 0]) => n * k),
  bellNumbers: loop(([n = 0]) => n * n),
  catalan: loop(([n = 0]) => n),
  composition: loop(([n = 0, k = 0]) => n * k),
  bernoulli: loop(([n = 0]) => n * n),
  multinomial: (m, [a]) => ({
    work: numbersIn(m, a).reduce<number>((s, x) => s + Math.abs(toNum(m, x) ?? 0), 0),
  }),
  isPrime: (m, [x]) => ({
    work: numbersIn(m, x).reduce<number>((s, v) => s + (typeof v === 'number' ? Math.sqrt(Math.abs(v)) / 6 : 64), 0),
  }),
  det: cubic(0.5),
  inv: cubic(0.5),
  pinv: cubic(0.5),
  lup: cubic(0.5),
  lusolve: cubic(0.5),
  lsolve: cubic(0.5),
  usolve: cubic(0.5),
  lsolveAll: cubic(0.5),
  usolveAll: cubic(0.5),
  qr: cubic(0.5),
  slu: cubic(0.5),
  schur: cubic(0.5),
  sylvester: cubic(0.5),
  lyap: cubic(0.5),
  norm: cubic(0.5),
  eigs: cubic(10),
  expm: cubic(10),
  sqrtm: cubic(10),
  print: (m, [template, values]) => {
    const placeholders = typeof template === 'string' ? (template.match(/\$/g) ?? []).length : 0;
    return { chars: (typeof template === 'string' ? template.length : 0) + placeholders * printedChars(m, values) };
  },
  format: (m, [, opt]) => {
    const p = toNum(m, opt) ?? (opt && typeof opt === 'object' ? toNum(m, (opt as { precision?: unknown }).precision) : null);
    return p === null ? {} : { chars: Math.abs(p) };
  },
  solveODE: (m, a) => {
    const opts = a[3] as { maxIter?: unknown } | undefined;
    const maxIter = (opts && typeof opts === 'object' ? toNum(m, opts.maxIter) : null) ?? 1e4;
    return { work: maxIter * (elementCount(m, a[2]) + 1) * 10 };
  },
  subset: (m, [object, index, replacement]) =>
    replacement === undefined ? {} : { elements: assignedSize(m, object, index) ?? 1 },
};

/** Functions taking several collections that do not broadcast them against each other. */
const NO_BROADCAST: ReadonlySet<string> = new Set([
  'dot',
  'cross',
  'distance',
  'intersect',
  'deepEqual',
  'matrixFromRows',
  'matrixFromColumns',
  'setUnion',
  'setIntersect',
  'setDifference',
  'setSymDifference',
  'setIsSubset',
  'setMultiplicity',
  'setDistinct',
  'setSize',
  'lusolve',
  'lsolve',
  'usolve',
  'lsolveAll',
  'usolveAll',
  'sylvester',
  'lyap',
  'sum',
  'prod',
  'max',
  'min',
  'mean',
  'median',
  'mode',
  'std',
  'variance',
  'mad',
  'quantileSeq',
  'count',
  'if',
  'freqz',
  'zpk2tf',
  'polynomialRoot',
  'solveODE',
  'apply',
  'sort',
  'partitionSelect',
]);

/** The estimator of a function (own entries only: never `Object.prototype` members). */
function estimatorFor(name: string): Estimator | undefined {
  return Object.hasOwn(ESTIMATORS, name) ? ESTIMATORS[name] : undefined;
}

function estimate(math: MathJsInstance, name: string, args: readonly unknown[]): Estimate {
  const estimator = estimatorFor(name);
  if (!estimator) return {};
  try {
    return estimator(math, args);
  } catch {
    // Unusual arguments: mathjs validates them itself; the result is still checked afterwards.
    return {};
  }
}

/**
 * Call `fn(...args)` within the limits: estimates the result size and work from the arguments
 * (throwing "too large" / "too complex" / "number too large" before mathjs runs), charges the
 * work budget and checks the actual result.
 */
export function guardedCall(math: MathJsInstance, name: string, fn: AnyFn, args: readonly unknown[]): unknown {
  // Fast path: scalar numbers / booleans into a function without a size estimate cannot
  // allocate anything big (operators in loops and recursion take this path).
  if (estimatorFor(name) === undefined && args.every(isPlainScalar)) {
    charge(1);
    const result = fn(...args);
    if (typeof result === 'object' || typeof result === 'string' || typeof result === 'bigint') checkResultSize(math, result);
    return result;
  }
  let inputs = 1;
  let collections = 0;
  let bits = 0;
  for (const a of args) {
    if (typeof a === 'string') inputs += a.length >> 4;
    else if (isCollection(math, a)) {
      inputs += elementWork(elementCount(math, a));
      collections++;
    } else if (typeof a === 'bigint' || math.isFraction(a)) {
      if (isTooBigExact(math, a)) throw new CalcError('calc.error.tooBig');
      bits += exactBits(math, a);
    }
  }
  // Exact arithmetic: results of + - * / have at most the summed size; cost grows ~quadratically.
  if (bits > MAX_EXACT_BITS) throw new CalcError('calc.error.tooBig');
  inputs += ((bits / 64) * (bits / 64)) / 16;
  const est = estimate(math, name, args);
  let elements = est.elements;
  if (elements === undefined && collections >= 2 && !NO_BROADCAST.has(name)) elements = broadcastSize(math, args);
  if (elements !== undefined) checkElements(elements);
  if (est.chars !== undefined) checkChars(est.chars);
  if (est.bits !== undefined && !(est.bits <= MAX_EXACT_BITS)) throw new CalcError('calc.error.tooBig');
  charge(inputs + (est.work ?? 0));
  const result = fn(...args);
  checkResultSize(math, result);
  if (isCollection(math, result)) charge(elementWork(elementCount(math, result)));
  else if (typeof result === 'string') charge(result.length >> 4);
  return result;
}

function isPlainScalar(v: unknown): boolean {
  return typeof v === 'number' || typeof v === 'boolean';
}

/** A guarded wrapper of `fn` (estimates and checks of `guardedCall` under `name`). */
export function guardFunction(math: MathJsInstance, name: string, fn: AnyFn): AnyFn {
  return (...args: unknown[]) => guardedCall(math, name, fn, args);
}

// ---------------------------------------------------------------------------
// Raw-args helpers (operands are evaluated by the helper itself)
// ---------------------------------------------------------------------------

type RawFn = AnyFn & { rawArgs: true };

const compiledNodes = new WeakMap<MathNode, EvalFunction>();

/** Evaluate a (guarded) expression node against a mathjs scope; compiled code is cached. */
function evalNode(node: MathNode, scope: unknown): unknown {
  let compiled = compiledNodes.get(node);
  if (!compiled) {
    compiled = node.compile();
    compiledNodes.set(node, compiled);
  }
  return compiled.evaluate(scope as Map<string, unknown>);
}

/** mathjs calls raw-args functions as `fn(argNodes, math, scope)`. */
function isRawCall(math: MathJsInstance, callArgs: readonly unknown[]): callArgs is [MathNode[], unknown, unknown] {
  const nodes = callArgs[0];
  return callArgs.length === 3 && Array.isArray(nodes) && nodes.every((n) => math.isNode(n));
}

function namespaceFunction(math: MathJsInstance, name: string): AnyFn {
  const fn = (math as unknown as Record<string, unknown>)[name];
  if (typeof fn !== 'function') throw new CalcError('calc.error.undefinedFunction', { name });
  return fn as AnyFn;
}

const CONTINUE = Symbol('continue');

/** Short-circuit results of the lazy operators (same rules as mathjs' transforms). */
// mathjs' `and` / `or` accept booleans at run time; their typings only list numeric types.
type Logical = (a: unknown, b: boolean) => boolean;
const SHORTCUTS: Record<LazyOp, (math: MathJsInstance, left: unknown) => unknown> = {
  and: (math, a) => (!isCollection(math, a) && !(math.and as unknown as Logical)(a, true) ? false : CONTINUE),
  or: (math, a) => (!isCollection(math, a) && (math.or as unknown as Logical)(a, false) ? true : CONTINUE),
  bitAnd: (math, a) => {
    if (isCollection(math, a)) return CONTINUE;
    if (Number.isNaN(Number(a))) return NaN;
    return a === 0 || a === false ? 0 : CONTINUE;
  },
  bitOr: (math, a) => {
    if (isCollection(math, a)) return CONTINUE;
    if (Number.isNaN(Number(a))) return NaN;
    if (a === -1) return -1;
    return a === true ? 1 : CONTINUE;
  },
  nullish: (math, a) => (!isCollection(math, a) && a !== null && a !== undefined ? a : CONTINUE),
};

/** A short-circuiting operator (`and`, `or`, `&`, `|`, `??`) with guarded evaluation. */
export function lazyOperator(math: MathJsInstance, name: LazyOp): RawFn {
  const fn = (...callArgs: unknown[]): unknown => {
    const plain = namespaceFunction(math, name);
    if (!isRawCall(math, callArgs)) return guardedCall(math, name, plain, callArgs);
    const [nodes, , scope] = callArgs;
    if (nodes.length !== 2) {
      return guardedCall(
        math,
        name,
        plain,
        nodes.map((n) => evalNode(n, scope)),
      );
    }
    const left = evalNode(nodes[0]!, scope);
    const short = SHORTCUTS[name](math, left);
    if (short !== CONTINUE) return short;
    return guardedCall(math, name, plain, [left, evalNode(nodes[1]!, scope)]);
  };
  return Object.assign(fn, { rawArgs: true as const });
}

/** `map` / `filter` / `forEach`: evaluate the collections, check their broadcast size, delegate. */
export function collectionFunction(math: MathJsInstance, name: string, transform: AnyFn): RawFn {
  const fn = (...callArgs: unknown[]): unknown => {
    if (!isRawCall(math, callArgs)) return guardedCall(math, name, namespaceFunction(math, name), callArgs);
    const [nodes, ns, scope] = callArgs;
    if (nodes.length < 2) return transform(nodes, ns, scope);
    const last = nodes.length - 1;
    const values = nodes.slice(0, last).map((n) => evalNode(n, scope));
    let inputs = 1;
    for (const v of values) inputs += elementWork(elementCount(math, v));
    if (values.length >= 2) checkElements(broadcastSize(math, values));
    charge(inputs);
    const constants = values.map((v) => new math.ConstantNode(v as number) as MathNode);
    const result = transform([...constants, nodes[last]!], ns, scope);
    checkResultSize(math, result);
    if (isCollection(math, result)) charge(elementWork(elementCount(math, result)));
    return result;
  };
  return Object.assign(fn, { rawArgs: true as const });
}

/** `$$rel("smaller,smallerEq", a, b, c)`: a relational chain with lazily evaluated operands. */
function relationalChain(math: MathJsInstance, op: (name: string) => AnyFn): RawFn {
  const fn = (...callArgs: unknown[]): unknown => {
    if (!isRawCall(math, callArgs)) throw new CalcError('calc.error.generic');
    const [nodes, , scope] = callArgs;
    const conditionals = String((nodes[0] as ConstantNode).value).split(',');
    let right = evalNode(nodes[1]!, scope);
    for (let i = 0; i < conditionals.length; i++) {
      const left = right;
      right = evalNode(nodes[i + 2]!, scope);
      if (!op(conditionals[i]!)(left, right)) return false;
    }
    return true;
  };
  return Object.assign(fn, { rawArgs: true as const });
}

/** Largest (1-based) position addressed by an index value (number, range, list, boolean mask). */
function maxIndex(math: MathJsInstance, value: unknown): number {
  const n = toNum(math, value);
  if (n !== null) return Number.isFinite(n) ? Math.abs(n) : Infinity;
  if (!isCollection(math, value)) return 1;
  const items = numbersIn(math, value);
  if (items.length && items.every((x) => typeof x === 'boolean')) return items.length;
  let max = 1;
  for (const x of items) max = Math.max(max, Math.abs(toNum(math, x) ?? 1));
  return max;
}

/** Size accumulators of the index assignments being evaluated (innermost last). */
const dimFrames: number[] = [];

/** Forget unfinished index-assignment checks (an evaluation was aborted). Call per evaluation. */
export function resetGuards(): void {
  dimFrames.length = 0;
}

/**
 * `$$dim(pos, count, object, value)`: one dimension of an index assignment; `object` is the
 * matrix being assigned into (the evaluation context of the index). Accumulates the size the
 * object will be resized to and rejects it at the last dimension; returns `value`. Dimensions are
 * evaluated in order, and index assignments nested in index expressions complete in between, so
 * a stack of accumulators keeps them apart.
 */
function dimensionGuard(math: MathJsInstance): AnyFn {
  return (pos: unknown, count: unknown, object: unknown, value: unknown) => {
    const p = Number(pos);
    const n = Number(count);
    const current = typeof object === 'string' ? [object.length] : (shapeOf(math, object) ?? []);
    if (p === 0) dimFrames.push(1);
    if (!dimFrames.length) throw new CalcError('calc.error.generic');
    const top = dimFrames.length - 1;
    dimFrames[top] = dimFrames[top]! * Math.max(current[p] ?? 1, maxIndex(math, value));
    if (p === n - 1) {
      let size = dimFrames.pop()!;
      for (let j = n; j < current.length; j++) size *= current[j]!;
      checkElements(size);
      charge(elementWork(size));
    }
    return value;
  };
}

type NodeBase = new () => object;
type CompiledNode = (scope: unknown, args: unknown, context: unknown) => unknown;

/**
 * A node that evaluates to its evaluation context. Inside an index, mathjs passes the object
 * being indexed as the context — for an assignment, exactly the matrix it is about to resize.
 */
function contextNodeFactory(math: MathJsInstance): () => MathNode {
  const Base = math.Node as unknown as NodeBase;
  class ContextNode extends Base {
    get type(): string {
      return 'ContextNode';
    }
    _compile(): CompiledNode {
      return (_scope, _args, context) => context;
    }
    forEach(): void {}
    map(): MathNode {
      return new ContextNode() as unknown as MathNode;
    }
    clone(): MathNode {
      return new ContextNode() as unknown as MathNode;
    }
    _toString(): string {
      return 'context';
    }
  }
  return () => new ContextNode() as unknown as MathNode;
}

/**
 * The namespace helpers referenced by `guardTree` (`$$op_*`, `$$lazy_*`, `$$rel`, `$$dim`), to be
 * imported into the engine. `ns` is mathjs' expression namespace (functions with transforms).
 */
export function guardHelpers(math: MathJsInstance, ns: Record<string, unknown>): Record<string, unknown> {
  const ops = new Map<string, AnyFn>();
  const op = (name: string): AnyFn => {
    let fn = ops.get(name);
    if (!fn) {
      fn = (...args: unknown[]) => {
        const target = ns[name];
        if (typeof target !== 'function') throw new CalcError('calc.error.undefinedFunction', { name });
        return guardedCall(math, name, target as AnyFn, args);
      };
      ops.set(name, fn);
    }
    return fn;
  };
  const helpers: Record<string, unknown> = {};
  for (const name of EAGER_OPS) helpers[OP_PREFIX + name] = op(name);
  for (const name of LAZY_OPS) helpers[LAZY_PREFIX + name] = lazyOperator(math, name);
  helpers[REL_NAME] = relationalChain(math, op);
  helpers[DIM_NAME] = dimensionGuard(math);
  return helpers;
}

type TypedFactory = ((name: string, signatures: Record<string, AnyFn>) => AnyFn) & {
  isTypedFunction(fn: unknown): boolean;
};

/**
 * A typed-function wrapper of a typed function: same name and signatures, every implementation
 * guarded. Staying a typed function matters when it is passed as a callback (`map(x, sqrt)`):
 * mathjs inspects the signatures to decide how many arguments to pass.
 */
function guardTyped(math: MathJsInstance, name: string, fn: AnyFn): AnyFn {
  const typed = math.typed as unknown as TypedFactory;
  const signatures: Record<string, AnyFn> = {};
  for (const [signature, impl] of Object.entries((fn as unknown as { signatures: Record<string, AnyFn> }).signatures)) {
    signatures[signature] = (...args: unknown[]) => guardedCall(math, name, impl, args);
  }
  return typed(name, signatures);
}

/**
 * Wrap a function of mathjs' expression namespace for use through the evaluation scope. Raw-args
 * functions keep their raw calling convention, typed functions stay typed functions.
 */
export function wrapNamespaceFunction(math: MathJsInstance, name: string, fn: AnyFn): AnyFn {
  if ((math.typed as unknown as TypedFactory).isTypedFunction(fn)) return guardTyped(math, name, fn);
  if ((fn as { rawArgs?: boolean }).rawArgs) {
    if (LAZY_OP_SET.has(name)) return lazyOperator(math, name as LazyOp);
    if (name === 'map' || name === 'filter' || name === 'forEach') return collectionFunction(math, name, fn);
    const passThrough = (...args: unknown[]) => {
      charge(1);
      return fn(...args);
    };
    return Object.assign(passThrough, { rawArgs: true as const });
  }
  // Keep unary plain functions recognisably unary for mathjs' callback arity detection.
  if (fn.length === 1) return (value: unknown) => guardedCall(math, name, fn, [value]);
  return guardFunction(math, name, fn);
}

// ---------------------------------------------------------------------------
// Expression tree rewrite
// ---------------------------------------------------------------------------

function isObjectPropertyIndex(index: unknown): boolean {
  return (index as { isObjectProperty(): boolean }).isObjectProperty();
}

type OperatorNodeCtor = new (op: string, fn: string, args: MathNode[], implicit?: boolean) => MathNode;

/**
 * Rewrite operators, relational chains and index assignments into guarded helper calls. The
 * helpers are emitted as operator nodes whose function is the helper's namespace name: mathjs
 * binds operator functions at compile time (no scope lookups at run time) and honours their
 * raw-args flag, so this is as fast as the original operators.
 */
const contextNodes = new WeakMap<MathJsInstance, () => MathNode>();

export function guardTree(math: MathJsInstance, root: MathNode): MathNode {
  const Operator = math.OperatorNode as unknown as OperatorNodeCtor;
  let contextNode = contextNodes.get(math);
  if (!contextNode) {
    contextNode = contextNodeFactory(math);
    contextNodes.set(math, contextNode);
  }
  const context = contextNode;
  const call = (op: string, name: string, args: MathNode[], implicit = false): MathNode => new Operator(op, name, args, implicit);
  const constant = (value: string | number): MathNode => new math.ConstantNode(value as number);
  const rec = (node: MathNode): MathNode => {
    if (math.isOperatorNode(node)) {
      const fn: string = node.fn;
      const args = node.args.map(rec);
      if (LAZY_OP_SET.has(fn)) return call(node.op, LAZY_PREFIX + fn, args);
      if (EAGER_OP_SET.has(fn)) return call(node.op, OP_PREFIX + fn, args, node.implicit);
      return node.map((child) => rec(child));
    }
    if (math.isRelationalNode(node)) {
      return call('rel', REL_NAME, [constant(node.conditionals.join(',')), ...node.params.map(rec)]);
    }
    if (math.isAssignmentNode(node) && node.index && !isObjectPropertyIndex(node.index)) {
      const dims = node.index.dimensions;
      const guarded = dims.map((dim, pos) => call('dim', DIM_NAME, [constant(pos), constant(dims.length), context(), rec(dim)]));
      const object = rec(node.object) as SymbolNode | AccessorNode;
      return new math.AssignmentNode(object, new math.IndexNode(guarded, node.index.dotNotation), rec(node.value));
    }
    return node.map((child) => rec(child));
  };
  return rec(root);
}
