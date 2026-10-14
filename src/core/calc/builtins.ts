/**
 * Spreadsheet-style helper functions available in calc blocks, inline calculations and table
 * formulas (English + Polish, Excel-like names). They take any mix of scalars, arrays and
 * matrices (`sum(A1:A3, C1)`), and skip non-numeric entries (text cells) like spreadsheets do.
 */
import type { MathJsInstance, MathType } from 'mathjs';
import { getEngine } from './engine';
import { guardFunction } from './guard';
import { CalcError } from './messages';
import { toInternalName } from './names';

/** Flatten scalars / nested arrays / matrices into one list. */
export function flattenValues(math: MathJsInstance, args: readonly unknown[]): unknown[] {
  const out: unknown[] = [];
  const walk = (v: unknown) => {
    if (Array.isArray(v)) v.forEach(walk);
    else if (math.isMatrix(v)) (v.toArray() as unknown[]).forEach(walk);
    else if (v !== undefined && v !== null) out.push(v);
  };
  args.forEach(walk);
  return out;
}

/** Numbers, big numbers, fractions, complex numbers and units with a value. */
export function isNumericValue(math: MathJsInstance, v: unknown): boolean {
  if (typeof v === 'number' || math.isBigNumber(v) || math.isFraction(v) || math.isComplex(v)) return true;
  return math.isUnit(v) && (v as unknown as { value: unknown }).value !== null;
}

function numericArgs(math: MathJsInstance, args: readonly unknown[]): MathType[] {
  return flattenValues(math, args).filter((v) => isNumericValue(math, v)) as MathType[];
}

/** mathjs reducers called with one array argument (their overloads are too narrow for MathType[]). */
type Reducer = (values: MathType[]) => unknown;

function truthy(v: unknown): boolean {
  if (typeof v === 'string') return v.trim() !== '';
  if (v && typeof v === 'object' && 'valueOf' in v) {
    const p: unknown = (v as { valueOf(): unknown }).valueOf();
    if (typeof p === 'number') return p !== 0;
  }
  return Boolean(v);
}

function createBuiltins(math: MathJsInstance): Map<string, unknown> {
  const reducer = (name: 'sum' | 'mean' | 'min' | 'max') => math[name] as unknown as Reducer;
  const sum = (...args: unknown[]) => {
    const values = numericArgs(math, args);
    return values.length ? reducer('sum')(values) : 0;
  };
  const nonEmpty = (name: 'mean' | 'min' | 'max') => (...args: unknown[]) => {
    const values = numericArgs(math, args);
    if (!values.length) throw new CalcError('calc.error.noValues');
    return reducer(name)(values);
  };
  const avg = nonEmpty('mean');
  const count = (...args: unknown[]) => numericArgs(math, args).length;
  const iff = (cond: unknown, a: unknown, b?: unknown) => (truthy(cond) ? a : b === undefined ? false : b);
  const round = math.round as unknown as (...args: unknown[]) => unknown;

  // [names, canonical name for the resource guard, implementation]
  const table: [string[], string, (...args: unknown[]) => unknown][] = [
    [['sum', 'suma'], 'sum', sum],
    [['avg', 'average', 'mean', 'średnia', 'srednia'], 'mean', avg],
    [['min'], 'min', nonEmpty('min')],
    [['max'], 'max', nonEmpty('max')],
    [['count', 'ile'], 'count', count],
    [['if', 'jeżeli', 'jezeli', 'jesli', 'jeśli'], 'if', iff],
    [['zaokr'], 'round', round],
  ];
  const map = new Map<string, unknown>();
  for (const [names, canonical, fn] of table) {
    const guarded = guardFunction(math, canonical, fn);
    for (const name of names) map.set(toInternalName(name), guarded);
  }
  return map;
}

let builtins: Map<string, unknown> | null = null;

/** Helper functions keyed by internal name (shadowed by user variables of the same name). */
export function getBuiltins(): ReadonlyMap<string, unknown> {
  builtins ??= createBuiltins(getEngine().math);
  return builtins;
}
