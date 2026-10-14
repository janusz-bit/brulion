/**
 * The sandboxed mathjs instance used by every calc feature.
 *
 * Security (see https://mathjs.org/docs/expressions/security.html): notes come from anywhere
 * (imports, sync), so expressions are untrusted.
 * * A dedicated instance (`create(all)`) — the global mathjs default instance is never touched.
 * * `parse` is captured first, then functions that evaluate code, change the instance or build
 *   symbolic trees are replaced by throwing stubs (`import`, `createUnit`, `evaluate`, `parse`,
 *   `compile`, `simplify`, `derivative`, `resolve`, `reviver`, `config`, ...).
 * * `typed` cannot be replaced in the namespace (mathjs uses it internally at run time), so it is
 *   shadowed — together with all disabled names — by the evaluation scope (`GuardedScope`),
 *   which mathjs consults before its namespace.
 * * Resource limits (limits.ts, guard.ts): every parsed expression is rewritten so operators,
 *   relational chains and index assignments go through guarded helpers, and every namespace
 *   function is resolved through the scope as a guarded wrapper. Calls whose result would be too
 *   big (matrix broadcasting, products, allocations, power sets, huge strings or exact numbers)
 *   are rejected before mathjs allocates anything, and a deterministic work budget bounds the
 *   CPU time of an expression (no runaway recursion or combinatorial loops).
 * * Never `eval` / `new Function`.
 */
import { all, create } from 'mathjs';
import type { EvalFunction, FactoryFunctionMap, MathJsInstance } from 'mathjs';
import { guardHelpers, guardTree, resetGuards, wrapNamespaceFunction } from './guard';
import { charge, checkElements, checkResultSize, elementWork, MAX_EXPRESSION_LENGTH, startExpression, withBudget } from './limits';
import { CalcError } from './messages';

export {
  checkResultSize,
  elementCount,
  MAX_ELEMENTS,
  MAX_EXACT_BITS,
  MAX_EXPRESSION_LENGTH,
  MAX_STRING_LENGTH,
  MAX_TOTAL_WORK,
  MAX_WORK,
  withBudget,
} from './limits';

/** Functions that are replaced by throwing stubs in the mathjs namespace. */
export const DISABLED_FUNCTIONS: readonly string[] = [
  'import',
  'createUnit',
  'evaluate',
  'parse',
  'compile',
  'simplify',
  'simplifyConstant',
  'simplifyCore',
  'derivative',
  'rationalize',
  'resolve',
  'reviver',
  'replacer',
  'parser',
  'config',
  'chain',
  'leafCount',
  'symbolicEqual',
  'help',
];

/**
 * Names blocked only through the scope, not replaced in the namespace: mathjs injects `typed` and
 * `config` (the configuration object) into functions it creates lazily, so replacing them there
 * would break e.g. `eigs`, `schur`, `intersect` or `factorial(bignumber(10))`.
 */
const SHADOWED_ONLY: readonly string[] = ['typed', 'config'];

/** A scope-like object mathjs can evaluate against (duck-typed Map). */
export interface ScopeLike {
  get(key: string): unknown;
  set(key: string, value: unknown): unknown;
  has(key: string): boolean;
  delete(key: string): boolean;
  keys(): IterableIterator<string>;
}

/** The engine: a secured mathjs instance plus a compile cache. */
export interface CalcEngine {
  readonly math: MathJsInstance;
  /**
   * Compile (cached) and evaluate `code` against `scope`. Throws on errors. The scope must be a
   * `GuardedScope`: it shadows the blocked names that stay in the namespace (`typed`, `config`).
   */
  evaluate(code: string, scope: GuardedScope): unknown;
  /** Throwing stubs for every blocked name (used by `GuardedScope`). */
  readonly blocked: ReadonlyMap<string, () => never>;
  /**
   * Guarded wrapper of the expression-namespace function `name` (what a call `name(...)` or the
   * symbol `name` resolves to), or undefined when there is no such function.
   */
  functionFor(name: string): unknown;
}

function disabledStub(name: string): () => never {
  return () => {
    throw new CalcError('calc.error.disabled', { name });
  };
}

type AnyFn = (...args: unknown[]) => unknown;

function toNum(math: MathJsInstance, v: unknown): number | null {
  if (typeof v === 'number') return v;
  if (math.isBigNumber(v) || math.isFraction(v)) return Number(v.valueOf());
  return null;
}

/** Guard for `range` (also used by the `a:b` operator, so it lives in the namespace). */
function makeRangeGuard(math: MathJsInstance, original: AnyFn): AnyFn {
  return (...args: unknown[]) => {
    let nums: (number | null)[];
    if (typeof args[0] === 'string') {
      nums = args[0].split(':').map((p) => (p.trim() === '' ? null : Number(p)));
    } else {
      nums = args.slice(0, 3).map((a) => toNum(math, a));
    }
    const [start, end, step = 1] = nums;
    if (typeof start === 'number' && typeof end === 'number' && typeof step === 'number') {
      const n = step === 0 ? 0 : Math.floor((end - start) / step);
      const count = Number.isFinite(n) ? Math.max(0, n) : Infinity;
      checkElements(count);
      charge(elementWork(count));
    }
    return original(...args);
  };
}

function createEngine(): CalcEngine {
  // `all` is typed as possibly undefined; it is always defined in the full mathjs bundle.
  const math = create(all as FactoryFunctionMap, { number: 'number' });
  // Capture the real parser before it is disabled in the namespace.
  const parse = math.parse;
  const originalRange = math.range as unknown as AnyFn;
  // The expression parser resolves names in `mathWithTransform` (functions with transforms).
  const ns = (math as unknown as { expression: { mathWithTransform: Record<string, unknown> } }).expression.mathWithTransform;
  // The expression parser uses `range.transform` (end inclusive); keep it, guarded.
  const originalRangeTransform = (ns.range ?? originalRange) as AnyFn;

  const blocked = new Map<string, () => never>();
  const overrides: Record<string, unknown> = {};
  for (const name of DISABLED_FUNCTIONS) {
    const stub = disabledStub(name);
    blocked.set(name, stub);
    if (!SHADOWED_ONLY.includes(name)) overrides[name] = stub;
  }
  for (const name of SHADOWED_ONLY) if (!blocked.has(name)) blocked.set(name, disabledStub(name));
  overrides.range = Object.assign(makeRangeGuard(math, originalRange), {
    transform: makeRangeGuard(math, originalRangeTransform),
  });
  // Helpers referenced by rewritten expressions ($$op_add, $$lazy_and, $$rel, $$dim).
  Object.assign(overrides, guardHelpers(math, ns));
  math.import(overrides, { override: true });

  const wrappers = new Map<string, unknown>();
  function functionFor(name: string): unknown {
    const hit = wrappers.get(name);
    if (hit !== undefined) return hit;
    // Own properties only (never Object.prototype names); `$$` helpers are already guarded.
    if (name.startsWith('$$') || blocked.has(name) || !Object.hasOwn(ns, name)) return undefined;
    const value = ns[name];
    if (typeof value !== 'function') return undefined;
    const wrapped = wrapNamespaceFunction(math, name, value as AnyFn);
    wrappers.set(name, wrapped);
    return wrapped;
  }

  const cache = new Map<string, EvalFunction>();
  const CACHE_LIMIT = 1000;

  function compile(code: string): EvalFunction {
    const hit = cache.get(code);
    if (hit) {
      cache.delete(code);
      cache.set(code, hit);
      return hit;
    }
    const compiled = guardTree(math, parse(code)).compile();
    cache.set(code, compiled);
    if (cache.size > CACHE_LIMIT) cache.delete(cache.keys().next().value!);
    return compiled;
  }

  return {
    math,
    blocked,
    functionFor,
    evaluate(code: string, scope: GuardedScope): unknown {
      if (code.length > MAX_EXPRESSION_LENGTH) throw new CalcError('calc.error.tooLong');
      return withBudget(() => {
        startExpression();
        resetGuards();
        // mathjs accepts any object with get/set/has/keys as a Map scope.
        const result: unknown = compile(code).evaluate(scope as unknown as Map<string, unknown>);
        checkResultSize(math, result);
        return result;
      });
    },
  };
}

let engineInstance: CalcEngine | null = null;

/** The shared engine (created lazily on first use). */
export function getEngine(): CalcEngine {
  engineInstance ??= createEngine();
  return engineInstance;
}

/**
 * Evaluation scope handed to mathjs. Lookup order: blocked names (throwing stubs) → temporary
 * overlay (aggregates, table references) → user variables → helper functions → guarded
 * namespace functions. Assignments go to the user variables; assigning a blocked or reserved
 * name, or a value that is too big, throws.
 */
export class GuardedScope implements ScopeLike {
  constructor(
    private readonly vars: Map<string, unknown>,
    private readonly overlay: ReadonlyMap<string, unknown> = new Map(),
    private readonly extraBuiltins: ReadonlyMap<string, unknown> = new Map(),
    private readonly engine: CalcEngine = getEngine(),
  ) {}

  has(key: string): boolean {
    return (
      this.engine.blocked.has(key) ||
      this.overlay.has(key) ||
      this.vars.has(key) ||
      this.extraBuiltins.has(key) ||
      this.engine.functionFor(key) !== undefined
    );
  }

  get(key: string): unknown {
    const stub = this.engine.blocked.get(key);
    if (stub) return stub;
    if (this.overlay.has(key)) return this.overlay.get(key);
    if (this.vars.has(key)) return this.vars.get(key);
    if (this.extraBuiltins.has(key)) return this.extraBuiltins.get(key);
    return this.engine.functionFor(key);
  }

  set(key: string, value: unknown): this {
    if (this.engine.blocked.has(key) || key.startsWith('$$') || this.overlay.has(key)) {
      throw new CalcError('calc.error.cannotAssign', { name: key });
    }
    checkResultSize(this.engine.math, value);
    this.vars.set(key, value);
    return this;
  }

  delete(key: string): boolean {
    return this.vars.delete(key);
  }

  clear(): void {
    this.vars.clear();
  }

  *keys(): IterableIterator<string> {
    const seen = new Set<string>();
    const sources: Iterable<string>[] = [this.engine.blocked.keys(), this.overlay.keys(), this.vars.keys(), this.extraBuiltins.keys()];
    for (const src of sources) {
      for (const k of src) {
        if (!seen.has(k)) {
          seen.add(k);
          yield k;
        }
      }
    }
  }

  get size(): number {
    return [...this.keys()].length;
  }

  forEach(cb: (value: unknown, key: string, map: this) => void): void {
    for (const k of this.keys()) cb(this.get(k), k, this);
  }

  *entries(): IterableIterator<[string, unknown]> {
    for (const k of this.keys()) yield [k, this.get(k)];
  }

  *values(): IterableIterator<unknown> {
    for (const k of this.keys()) yield this.get(k);
  }

  [Symbol.iterator](): IterableIterator<[string, unknown]> {
    return this.entries();
  }
}
