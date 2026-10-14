/**
 * Text helpers for the vault index: diacritic folding, Unicode tokenising, word boundaries
 * and excerpts. Everything here is pure and allocation-light — it runs over the whole vault.
 */

/** Strings without non-ASCII characters fold with a plain `toLowerCase()`. */
const NON_ASCII_RE = /[\u0080-\uffff]/;
const MARKS_RE = /\p{M}/gu;
const L_STROKE_RE = /ł/g;
const WORD_CHAR_RE = /[\p{L}\p{N}\p{M}_]/u;
const TOKEN_SPLIT_RE = /[^\p{L}\p{N}\p{M}]+/u;

/**
 * Lower-case and strip diacritics: "Żółw" → "zolw", "Łódź" → "lodz". Same result as
 * `foldText` from `@/core/dates` for practical input, but vectorised (one native pass per
 * step instead of per character), which matters when folding whole notes.
 */
export function fold(s: string): string {
  if (!NON_ASCII_RE.test(s)) return s.toLowerCase();
  return s.toLowerCase().replace(L_STROKE_RE, 'l').normalize('NFD').replace(MARKS_RE, '');
}

/** Folded text with, for every UTF-16 unit of it, the offset of its source in the original. */
export interface FoldMap {
  text: string;
  /** `map[i]` = original offset of folded unit `i`; `map[text.length]` = original length. */
  map: number[];
}

/** Fold one code point (may return '' for a lone combining mark). */
function foldChar(ch: string): string {
  const lower = ch.toLowerCase();
  if (lower === 'ł') return 'l';
  return lower.normalize('NFD').replace(MARKS_RE, '');
}

/** Like {@link fold} but keeps a map back to the original offsets (use on short strings). */
export function foldWithMap(s: string): FoldMap {
  let text = '';
  const map: number[] = [];
  let offset = 0;
  for (const ch of s) {
    const f = foldChar(ch);
    for (let k = 0; k < f.length; k++) map.push(offset);
    text += f;
    offset += ch.length;
  }
  map.push(s.length);
  return { text, map };
}

/**
 * Split text into word tokens on anything that is not a Unicode letter, digit or combining
 * mark: "Kupić farbę (2 l)" → ["Kupić", "farbę", "2", "l"].
 */
export function tokenize(text: string): string[] {
  const out: string[] = [];
  for (const t of text.split(TOKEN_SPLIT_RE)) if (t) out.push(t);
  return out;
}

/** MiniSearch term processor: fold, drop empty terms. */
export function processTerm(term: string): string | null {
  const f = fold(term);
  return f ? f : null;
}

/** Is `ch` (one code point) part of a word (letter, digit, mark or `_`)? */
export function isWordChar(ch: string | undefined): boolean {
  return ch !== undefined && ch !== '' && WORD_CHAR_RE.test(ch);
}

/** The code point ending right before offset `i` (surrogate-pair aware), or undefined. */
export function charBefore(s: string, i: number): string | undefined {
  if (i <= 0) return undefined;
  const lo = s.charCodeAt(i - 1);
  if (lo >= 0xdc00 && lo <= 0xdfff && i >= 2) {
    const hi = s.charCodeAt(i - 2);
    if (hi >= 0xd800 && hi <= 0xdbff) return s.slice(i - 2, i);
  }
  return s[i - 1];
}

/** The code point starting at offset `i`, or undefined at the end. */
export function charAt(s: string, i: number): string | undefined {
  if (i >= s.length) return undefined;
  const cp = s.codePointAt(i);
  return cp === undefined ? undefined : String.fromCodePoint(cp);
}

/** Is `[start, end)` of `s` a whole word (no word character touching either side)? */
export function isWholeWord(s: string, start: number, end: number): boolean {
  return !isWordChar(charBefore(s, start)) && !isWordChar(charAt(s, end));
}

/** Index of the first occurrence of `needle` in `hay` at a word start, or -1. */
export function indexOfWordStart(hay: string, needle: string, from = 0): number {
  if (!needle) return -1;
  let i = hay.indexOf(needle, from);
  while (i !== -1) {
    if (!isWordChar(charBefore(hay, i))) return i;
    i = hay.indexOf(needle, i + 1);
  }
  return -1;
}

/** Collapse runs of whitespace (tabs, newlines) into single spaces and trim. */
export function squash(s: string): string {
  return s.replace(/\s+/g, ' ').trim();
}

/** Default excerpt length (characters). */
export const EXCERPT_LENGTH = 120;

/** Do not cut inside a surrogate pair. */
function safeCut(s: string, i: number): number {
  const c = s.charCodeAt(i);
  return c >= 0xdc00 && c <= 0xdfff ? i + 1 : i;
}

/**
 * A short excerpt of `line` showing `[start, end)`: the whole (trimmed) line when it is short,
 * otherwise a window of about `max` characters around the match, cut at word boundaries and
 * marked with "…".
 */
export function excerptAround(line: string, start: number, end: number, max = EXCERPT_LENGTH): string {
  if (line.length <= max) return squash(line);
  const len = Math.max(0, Math.min(end, line.length) - start);
  const before = Math.max(0, Math.min(start, Math.floor((max - Math.min(len, max)) * 0.35)));
  let a = start - before;
  let b = Math.min(line.length, a + max);
  if (b - a < max) a = Math.max(0, b - max);
  if (a > 0) {
    // Start at the next word, unless that would skip too much or reach the match.
    const sp = line.slice(a, start).search(/\s/);
    if (sp !== -1 && sp < 20) a += sp + 1;
  }
  if (b < line.length) {
    const tail = line.slice(Math.max(end, a), b);
    const sp = tail.search(/\s\S*$/);
    if (sp !== -1 && tail.length - sp < 20) b = Math.max(end, a) + sp;
  }
  a = safeCut(line, a);
  b = safeCut(line, b);
  const body = squash(line.slice(a, b));
  return `${a > 0 ? '…' : ''}${body}${b < line.length ? '…' : ''}`;
}

/** Leading Markdown container syntax of a line: indentation, list marker + checkbox, heading, quote. */
const LINE_PREFIX_RE = /^[ \t]*(?:(?:[-*+]|\d{1,9}[.)])[ \t]+(?:\[.\][ \t]+)?|#{1,6}[ \t]+|(?:>[ \t]?)+)?/;
/** Trailing block id (` ^abc123`). */
const TRAILING_ID_RE = /[ \t]+\^[A-Za-z0-9-]+[ \t]*$/;

/**
 * Excerpt of a document line around `[start, end)` without the line's Markdown container
 * syntax (indentation, `- [ ]`, `## `, `> `) and without a trailing block id.
 */
export function lineExcerpt(line: string, start: number, end: number, max = EXCERPT_LENGTH): string {
  let cut = LINE_PREFIX_RE.exec(line)?.[0].length ?? 0;
  if (cut > start) cut = 0;
  let body = line.slice(cut);
  const id = TRAILING_ID_RE.exec(body);
  if (id && id.index >= end - cut) body = body.slice(0, id.index);
  return excerptAround(body, start - cut, end - cut, max);
}

/** Plain excerpt of the start of a text (for hits without a located match). */
export function excerptStart(text: string, max = EXCERPT_LENGTH): string {
  const s = squash(text);
  if (s.length <= max) return s;
  let b = s.lastIndexOf(' ', max);
  if (b < max * 0.6) b = max;
  return `${s.slice(0, safeCut(s, b))}…`;
}

/** Locale-aware comparison for display names (Polish rules, case-insensitive first). */
const COLLATOR = new Intl.Collator('pl', { sensitivity: 'base', numeric: true });

/** Compare two display names (Polish alphabet: "ł" after "l", "ż" after "ź"). */
export function compareNames(a: string, b: string): number {
  return COLLATOR.compare(a, b) || (a < b ? -1 : a > b ? 1 : 0);
}

/** Plain code-unit comparison (deterministic across locales). */
export function compareCode(a: string, b: string): number {
  return a < b ? -1 : a > b ? 1 : 0;
}

/** Escape a string for use inside a RegExp. */
export function escapeRegExp(s: string): string {
  return s.replace(/[.*+?^${}()|[\]\\/]/g, '\\$&');
}

/** Folded block texts, cached per block object (blocks are replaced when a note changes). */
const blockFoldCache = new WeakMap<object, string>();

/** {@link fold} of a block's text, cached. */
export function foldedText(block: { text: string }): string {
  let f = blockFoldCache.get(block);
  if (f === undefined) {
    f = fold(block.text);
    blockFoldCache.set(block, f);
  }
  return f;
}
