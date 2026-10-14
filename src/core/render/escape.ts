/**
 * HTML escaping helpers used by every renderer rule. All user text that ends up in HTML goes
 * through these (the sanitizer is a second line of defence, not the first).
 */

const HTML_ESCAPES: Readonly<Record<string, string>> = {
  '&': '&amp;',
  '<': '&lt;',
  '>': '&gt;',
  '"': '&quot;',
  "'": '&#39;',
};

const HTML_ESCAPE_TEST = /[&<>"']/;
const HTML_ESCAPE_RE = /[&<>"']/g;

/** Escape text for use in HTML content or a double-quoted attribute value. */
export function escapeHtml(s: string): string {
  return HTML_ESCAPE_TEST.test(s) ? s.replace(HTML_ESCAPE_RE, (c) => HTML_ESCAPES[c] ?? c) : s;
}

/** Attribute value: string, number, `true` (boolean attribute), or false/null/undefined (omitted). */
export type AttrValue = string | number | boolean | null | undefined;

/**
 * Serialise attributes: `{ class: 'a', 'data-x': 1, disabled: true, hidden: false }` →
 * ` class="a" data-x="1" disabled`. Values are escaped; names must be trusted (code literals).
 */
export function renderAttrs(attrs: Readonly<Record<string, AttrValue>>): string {
  let out = '';
  for (const [name, value] of Object.entries(attrs)) {
    if (value === false || value === null || value === undefined) continue;
    out += value === true ? ` ${name}` : ` ${name}="${escapeHtml(String(value))}"`;
  }
  return out;
}

/** Join class names, skipping empty/false entries. */
export function classNames(...parts: (string | false | null | undefined)[]): string {
  return parts.filter((p): p is string => typeof p === 'string' && p !== '').join(' ');
}

/** Serialise markdown-it style attribute pairs (`token.attrs`). */
export function renderAttrPairs(pairs: readonly (readonly [string, string | number])[] | null | undefined): string {
  if (!pairs) return '';
  let out = '';
  for (const [name, value] of pairs) out += ` ${escapeHtml(name)}="${escapeHtml(String(value))}"`;
  return out;
}
