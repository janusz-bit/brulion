/**
 * Identifier handling: user-facing names (Unicode, e.g. `ilość`, `cena_netto`) are mapped to
 * internal mathjs-safe names and back.
 *
 * Internal form: ASCII letters, digits and `_` are kept; every other code point becomes
 * `$<hex>$` (`ilość` → `ilo$15b$$107$`). The mapping is deterministic, reversible and cannot
 * collide with names users type, because the lexer never accepts `$` inside identifiers.
 * Names starting with `$$` are reserved for engine temporaries (aggregates, table references).
 */

/** Characters allowed to start a user identifier. */
export const IDENT_START_RE = /[\p{L}_]/u;
/** Characters allowed inside a user identifier. */
export const IDENT_PART_RE = /[\p{L}\p{M}\p{Nd}_]/u;
/** A whole user identifier. */
export const IDENT_RE = /^[\p{L}_][\p{L}\p{M}\p{Nd}_]*$/u;

/** Prefix of engine-internal temporary names. */
export const TEMP_PREFIX = '$$';

/** True when `name` is a valid user identifier (after NFC normalisation). */
export function isIdentifier(name: string): boolean {
  return IDENT_RE.test(name.normalize('NFC'));
}

/** Map a user identifier to its internal mathjs name. */
export function toInternalName(name: string): string {
  let out = '';
  for (const ch of name.normalize('NFC')) {
    out += /[A-Za-z0-9_]/.test(ch) ? ch : `$${ch.codePointAt(0)!.toString(16)}$`;
  }
  return out;
}

/** Inverse of `toInternalName`; temporaries (`$$sum`) lose their prefix. */
export function toDisplayName(internal: string): string {
  if (internal.startsWith(TEMP_PREFIX)) return internal.slice(TEMP_PREFIX.length).replace(/_\d+$/, '');
  return internal.replace(/\$([0-9a-f]{1,6})\$/g, (m: string, hex: string) => {
    // Error messages may contain user text that only looks like a mangled name.
    const cp = parseInt(hex, 16);
    return cp <= 0x10ffff ? String.fromCodePoint(cp) : m;
  });
}

const INTERNAL_IN_TEXT_RE = /\$\$[A-Za-z0-9_]+|[A-Za-z0-9_]*(?:\$[0-9a-f]{1,6}\$[A-Za-z0-9_]*)+/g;

/** Replace every internal name occurring in `text` (e.g. an error message) by its display form. */
export function demangleText(text: string): string {
  return text.replace(INTERNAL_IN_TEXT_RE, (m) => toDisplayName(m));
}

/**
 * Turn an arbitrary key (frontmatter / property key such as `due-date`, `cena netto`, `2026`)
 * into a user identifier, or null when nothing usable remains.
 */
export function sanitizeIdentifier(key: string): string | null {
  let s = key
    .normalize('NFC')
    .trim()
    .replace(/[^\p{L}\p{M}\p{Nd}_]+/gu, '_')
    .replace(/^_+|_+$/g, '');
  if (!s) return null;
  if (!IDENT_START_RE.test(s[0]!)) s = `_${s}`;
  return IDENT_RE.test(s) ? s : null;
}
