/**
 * Internal data model shared by the calc evaluators: values with their display metadata and the
 * variable scope (internal names → values).
 */
import type { CurrencyInfo } from './numbers';
import { isIdentifier, sanitizeIdentifier, toDisplayName, toInternalName, TEMP_PREFIX } from './names';

/** How a value should be displayed beyond its mathjs type. */
export interface ValueMeta {
  /** The value is a fraction that was written as a percentage (0.15 ↔ 15%). */
  percent?: boolean;
  /** The value is an amount of money. */
  currency?: CurrencyInfo;
}

/** A value plus its metadata. */
export interface CalcValue {
  value: unknown;
  meta: ValueMeta;
}

/**
 * Symbol under which `evaluateCalc` / `pageScope` attach variable metadata (percent / currency)
 * to the returned scope record. Only present when some variable has metadata; it is an
 * enumerable symbol property, so it survives object spread but not `Object.keys` / JSON.
 */
export const CALC_META: unique symbol = Symbol.for('brulion.calc.meta');

type MetaRecord = Record<string, ValueMeta>;

/** `obj[key] = value` as an own data property (a variable may be called `__proto__`). */
function setOwn<T>(obj: Record<string, T>, key: string, value: T): void {
  if (key === '__proto__') Object.defineProperty(obj, key, { value, enumerable: true, writable: true, configurable: true });
  else obj[key] = value;
}

/** Variables of a calc evaluation, keyed by internal (mathjs-safe) name. */
export class CalcScope {
  readonly values = new Map<string, unknown>();
  readonly meta = new Map<string, ValueMeta>();

  /** Build from a public scope record (display names); metadata is read from `CALC_META`. */
  static fromRecord(record?: Record<string, unknown>): CalcScope {
    const scope = new CalcScope();
    if (!record) return scope;
    const meta = (record as { [CALC_META]?: MetaRecord })[CALC_META] ?? {};
    for (const [key, value] of Object.entries(record)) {
      const name = isIdentifier(key) ? key.normalize('NFC') : sanitizeIdentifier(key);
      if (!name) continue;
      scope.set(toInternalName(name), value, Object.hasOwn(meta, key) ? meta[key] : undefined);
    }
    return scope;
  }

  /** A copy that can be modified independently (values themselves are shared). */
  clone(): CalcScope {
    const copy = new CalcScope();
    for (const [k, v] of this.values) copy.values.set(k, v);
    for (const [k, m] of this.meta) copy.meta.set(k, { ...m });
    return copy;
  }

  set(internal: string, value: unknown, meta?: ValueMeta): void {
    this.values.set(internal, value);
    if (meta && (meta.percent || meta.currency)) this.meta.set(internal, { ...meta });
    else this.meta.delete(internal);
  }

  has(internal: string): boolean {
    return this.values.has(internal);
  }

  metaOf(internal: string): ValueMeta | undefined {
    return this.meta.get(internal);
  }

  /** Public record keyed by display names (temporaries dropped), with `CALC_META` when needed. */
  toRecord(): Record<string, unknown> {
    const record: Record<string, unknown> = {};
    const meta: MetaRecord = {};
    for (const [internal, value] of this.values) {
      if (internal.startsWith(TEMP_PREFIX)) continue;
      const name = toDisplayName(internal);
      setOwn(record, name, value);
      const m = this.meta.get(internal);
      if (m) setOwn(meta, name, { ...m });
    }
    if (Object.keys(meta).length > 0) {
      Object.defineProperty(record, CALC_META, { value: meta, enumerable: true, writable: true, configurable: true });
    }
    return record;
  }
}
