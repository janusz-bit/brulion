/**
 * Block and page properties (docs/SYNTAX.md §4): `key:: value` lines (Logseq / Dataview),
 * YAML frontmatter conversion, page tags and aliases.
 */
import type { PropertyValue } from '../types';

/** Keys whose values are always lists of names (`tags:: a, [[b c]], #d`). */
const LIST_KEYS = new Set(['tags', 'tag', 'alias', 'aliases']);

/** Split on commas that are not inside `[[…]]`, `((…))` or quotes. */
export function splitTopLevelCommas(s: string): string[] {
  const out: string[] = [];
  let depth = 0;
  let quote: string | null = null;
  let start = 0;
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (quote) {
      if (c === quote) quote = null;
      continue;
    }
    if (c === '"') quote = c;
    else if ((c === '[' && s[i + 1] === '[') || (c === '(' && s[i + 1] === '(')) {
      depth++;
      i++;
    } else if (((c === ']' && s[i + 1] === ']') || (c === ')' && s[i + 1] === ')')) && depth > 0) {
      depth--;
      i++;
    } else if (c === ',' && depth === 0) {
      out.push(s.slice(start, i));
      start = i + 1;
    }
  }
  out.push(s.slice(start));
  return out;
}

/** `[[Page]]` -> "Page", `#tag` -> "tag", `#[[multi word]]` -> "multi word"; null for anything else. */
export function refName(part: string): string | null {
  const t = part.trim();
  let m = /^#?\[\[([^[\]]+)\]\]$/.exec(t);
  if (m) return m[1]!.trim() || null;
  m = /^#([\p{L}\p{N}\p{M}_\-/]+)$/u.exec(t);
  if (m && !/^\d+$/.test(m[1]!)) return m[1]!;
  return null;
}

/** Strip `#`, `[[`/`]]` and surrounding quotes/whitespace from a loose list item. */
export function cleanName(part: string): string {
  let t = part.trim();
  if ((t.startsWith('"') && t.endsWith('"')) || (t.startsWith("'") && t.endsWith("'"))) t = t.slice(1, -1).trim();
  if (t.startsWith('#')) t = t.slice(1);
  const m = /^\[\[(.*)\]\]$/.exec(t);
  if (m) t = m[1]!;
  return t.trim();
}

/**
 * Interpret a `key:: value` property value. `tags`/`tag`/`alias`/`aliases` always give a list
 * (comma separated, decoration stripped). For other keys, a value whose comma-separated parts
 * are all `[[links]]` or `#tags` becomes a list of names; anything else stays a raw string.
 */
export function parsePropertyValue(key: string, raw: string): PropertyValue {
  const value = raw.trim();
  if (LIST_KEYS.has(key)) {
    return splitTopLevelCommas(value)
      .map(cleanName)
      .filter((s) => s !== '');
  }
  if (value === '') return '';
  const parts = splitTopLevelCommas(value)
    .map((p) => p.trim())
    .filter((p) => p !== '');
  if (parts.length === 0) return value;
  const names: string[] = [];
  for (const p of parts) {
    const n = refName(p);
    if (n === null) return value;
    names.push(n);
  }
  return names;
}

/** Convert one frontmatter value to a property value (null for unsupported values). */
function frontmatterValue(v: unknown): PropertyValue | null {
  switch (typeof v) {
    case 'string':
      return v;
    case 'number':
    case 'boolean':
    case 'bigint':
      return String(v);
    default:
      break;
  }
  if (Array.isArray(v)) {
    const out: string[] = [];
    for (const item of v) {
      if (typeof item === 'string' || typeof item === 'number' || typeof item === 'boolean') out.push(String(item));
      // An unquoted `[[Page]]` in YAML parses as a nested list: keep it as a wiki link.
      else if (Array.isArray(item) && item.length === 1 && typeof item[0] === 'string') out.push(`[[${item[0]}]]`);
    }
    return out;
  }
  return null;
}

/**
 * An empty map without a prototype: looking up any key (`constructor`, `toString`, …) that was
 * not set gives `undefined`, so consumers can index property / block-id maps with user text.
 */
export function emptyMap<T>(): Record<string, T> {
  return Object.create(null) as Record<string, T>;
}

/** Merge maps (later ones win) into a new prototype-free map. */
export function mergeMaps<T>(...maps: Record<string, T>[]): Record<string, T> {
  const out = emptyMap<T>();
  for (const m of maps) for (const k of Object.keys(m)) out[k] = m[k]!;
  return out;
}

/** Page properties from frontmatter: scalars and lists, keys lower-cased; maps and nulls skipped. */
export function frontmatterProperties(data: Record<string, unknown>): Record<string, PropertyValue> {
  const out = emptyMap<PropertyValue>();
  for (const [key, value] of Object.entries(data)) {
    const pv = frontmatterValue(value);
    if (pv !== null) setProperty(out, key.toLowerCase(), pv);
  }
  return out;
}

/** Set a property as an own key (safe for `__proto__`, which would otherwise set the prototype). */
export function setProperty(props: Record<string, PropertyValue>, key: string, value: PropertyValue): void {
  if (key === '__proto__') Object.defineProperty(props, key, { value, enumerable: true, writable: true, configurable: true });
  else props[key] = value;
}

/** Value of a frontmatter key looked up case-insensitively. */
export function frontmatterGet(data: Record<string, unknown>, ...keys: string[]): unknown {
  for (const want of keys) {
    for (const [k, v] of Object.entries(data)) if (k.toLowerCase() === want) return v;
  }
  return undefined;
}

/**
 * Names from a frontmatter/property value: lists item by item, strings split on commas
 * (and, for tags, on whitespace). Decoration (`#`, `[[ ]]`) is stripped.
 */
export function namesFrom(value: unknown, splitWhitespace: boolean): string[] {
  const out: string[] = [];
  const add = (s: string): void => {
    const n = cleanName(s);
    if (n) out.push(n);
  };
  const addString = (s: string): void => {
    for (const part of splitTopLevelCommas(s)) {
      const t = part.trim();
      if (splitWhitespace && !t.startsWith('[[') && !t.startsWith('#[[')) for (const w of t.split(/\s+/)) add(w);
      else add(t);
    }
  };
  if (typeof value === 'string') addString(value);
  else if (typeof value === 'number') add(String(value));
  else if (Array.isArray(value)) {
    for (const item of value) {
      if (typeof item === 'string') {
        if (splitWhitespace && /^#\S+(\s+#\S+)+$/.test(item.trim())) addString(item);
        else add(item);
      } else if (typeof item === 'number') add(String(item));
      else if (Array.isArray(item) && item.length === 1 && typeof item[0] === 'string') add(item[0]);
    }
  }
  return out;
}

/** Lower-cased, NFC-normalised tag key. */
export function tagKey(tag: string): string {
  return (NON_ASCII_RE.test(tag) ? tag.normalize('NFC') : tag).toLowerCase();
}

const NON_ASCII_RE = /[\u0080-\uffff]/;

/** Deduplicate case-insensitively, keeping the first spelling. */
export function uniqueCaseInsensitive(items: Iterable<string>): string[] {
  const seen = new Set<string>();
  const out: string[] = [];
  for (const s of items) {
    const k = s.normalize('NFC').toLowerCase();
    if (seen.has(k)) continue;
    seen.add(k);
    out.push(s);
  }
  return out;
}
