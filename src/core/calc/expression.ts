/**
 * Evaluation of one calc expression (already tokenised): runs the transformation pipeline,
 * evaluates the code in the sandboxed engine and derives display metadata (percent, currency).
 */
import type { Locale } from '../types';
import { getBuiltins } from './builtins';
import { getEngine, GuardedScope } from './engine';
import type { Token } from './lexer';
import type { CalcScope, CalcValue, ValueMeta } from './model';
import type { CurrencyInfo } from './numbers';
import { AGGREGATE_NAMES, agreedCurrency, transformTokens, type AggregateKind, type TransformContext } from './transform';

/** A temporary value (table reference) that may stand for several cells. */
export interface TempValue extends CalcValue {
  /** Currencies of all cells behind the value (ranges); overrides `meta.currency`. */
  currencies?: CurrencyInfo[];
}

/** Environment of one evaluation. */
export interface ExprEnv {
  locale: Locale;
  /** User variables; mathjs-level assignments (`f(x) = …`) are written here. */
  scope: CalcScope;
  /** Temporaries keyed by internal `$$…` name (table references). */
  temps?: ReadonlyMap<string, TempValue>;
  /** Running aggregates of a calc block; enables the `suma` / `avg` keywords. */
  aggregate?: (kind: AggregateKind) => CalcValue;
}

function isUnitName(name: string): boolean {
  try {
    return getEngine().math.Unit.isValuelessUnit(name);
  } catch {
    return false;
  }
}

/** Build the transformer's view of an environment. */
export function transformContext(env: ExprEnv): TransformContext {
  const engine = getEngine();
  const builtins = getBuiltins();
  return {
    locale: env.locale,
    aggregates: !!env.aggregate,
    isDefined: (n) => env.scope.has(n) || !!env.temps?.has(n),
    metaOf: (n) => env.temps?.get(n)?.meta ?? env.scope.metaOf(n),
    isFunction: (n) => builtins.has(n) || (!engine.blocked.has(n) && engine.functionFor(n) !== undefined),
    isUnit: isUnitName,
  };
}

function isNumberLike(value: unknown): boolean {
  const math = getEngine().math;
  return typeof value === 'number' || math.isBigNumber(value) || math.isFraction(value);
}

function hasUnitValue(value: unknown): boolean {
  const math = getEngine().math;
  return math.isUnit(value) && (value as unknown as { value: unknown }).value !== null;
}

/** Evaluate one expression. Throws (CalcError or mathjs errors) on failure. */
export function evaluateTokens(tokens: Token[], env: ExprEnv): CalcValue {
  const engine = getEngine();
  const t = transformTokens(tokens, transformContext(env));
  if (!t.code) return { value: undefined, meta: {} };

  const overlay = new Map<string, unknown>();
  const currencies: CurrencyInfo[] = [...t.currencies];
  if (env.temps) {
    for (const [name, temp] of env.temps) overlay.set(name, temp.value);
    for (const ref of t.refs) {
      const temp = env.temps.get(ref);
      if (temp?.currencies) currencies.push(...temp.currencies);
    }
  }
  if (env.aggregate) {
    for (const kind of t.aggregates) {
      const agg = env.aggregate(kind);
      overlay.set(AGGREGATE_NAMES[kind], agg.value);
      if (agg.meta.currency) currencies.push(agg.meta.currency);
    }
  }

  const scope = new GuardedScope(env.scope.values, overlay, getBuiltins(), engine);
  const raw = engine.evaluate(t.code, scope);
  let value: unknown = raw;
  if (engine.math.isResultSet(raw)) {
    const entries = raw.entries;
    value = entries.length ? entries[entries.length - 1] : undefined;
  }

  const meta: ValueMeta = {};
  if (t.percent && isNumberLike(value)) meta.percent = true;
  else if (isNumberLike(value) || hasUnitValue(value)) {
    const currency = agreedCurrency(currencies);
    if (currency) meta.currency = currency;
  }
  return { value, meta };
}
