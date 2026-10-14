/**
 * Masking: replace regions that must not be parsed (inline code spans, `%%comments%%`,
 * `<!-- html comments -->`) by spaces. Masked strings keep the exact UTF-16 length and the
 * line breaks of the input, so every offset found in a masked string is valid in the
 * original one. Parsers detect syntax on the masked text and read values from the original.
 */
import { blankOut, indentOf, isBlank, type Lines } from './lines';

/** Options for {@link maskInline}. */
export interface MaskOptions {
  /** Mask `%%comments%%` (default true). */
  comments?: boolean;
  /** Mask `<!-- html comments -->` (default true). */
  htmlComments?: boolean;
}

/** Length of the run of `ch` (a char code) starting at `i`. */
function runLength(s: string, i: number, ch: number): number {
  let j = i;
  while (j < s.length && s.charCodeAt(j) === ch) j++;
  return j - i;
}

/**
 * Index of the maximal backtick runs of a string, grouped by length, answering "first run of
 * exactly `len` backticks at or after `from`" in amortised O(1) when `from` only grows (which
 * is how the scanners use it). Built lazily in one pass, so pairing code spans stays linear
 * even with many runs of different lengths (CommonMark code span rule).
 */
class BacktickRuns {
  private byLen: Map<number, number[]> | null = null;
  private readonly cursor = new Map<number, number>();

  constructor(private readonly s: string) {}

  private build(): Map<number, number[]> {
    const map = new Map<number, number[]>();
    const s = this.s;
    let i = s.indexOf('`');
    while (i !== -1) {
      const len = runLength(s, i, 96);
      let starts = map.get(len);
      if (!starts) map.set(len, (starts = []));
      starts.push(i);
      i = s.indexOf('`', i + len);
    }
    return map;
  }

  /** Start of the first maximal run of exactly `len` backticks at or after `from`, or -1. */
  closing(from: number, len: number): number {
    const starts = (this.byLen ??= this.build()).get(len);
    if (!starts) return -1;
    let k = this.cursor.get(len) ?? 0;
    while (k < starts.length && starts[k]! < from) k++;
    this.cursor.set(len, k);
    return k < starts.length ? starts[k]! : -1;
  }
}

/** Collects masked ranges and builds the masked string once. */
class Masker {
  private readonly parts: string[] = [];
  private last = 0;

  constructor(private readonly src: string) {}

  /** Mask [from, to). Ranges must be added in increasing, non-overlapping order. */
  mask(from: number, to: number): void {
    if (to <= from || from < this.last) return;
    this.parts.push(this.src.slice(this.last, from), blankOut(this.src.slice(from, to)));
    this.last = to;
  }

  result(): string {
    if (this.parts.length === 0) return this.src;
    this.parts.push(this.src.slice(this.last));
    return this.parts.join('');
  }
}

/**
 * Mask inline code spans (backtick runs of any length, CommonMark pairing), `%%comments%%`
 * and `<!-- html comments -->` in `s`. Unclosed comments and unmatched backticks are left
 * untouched (treated as literal text). Backslash escapes are honoured outside code spans.
 * Runs in (amortised) linear time.
 */
export function maskInline(s: string, opts: MaskOptions = {}): string {
  const comments = opts.comments !== false;
  const htmlComments = opts.htmlComments !== false;
  // Fast path: nothing to mask.
  if (s.indexOf('`') === -1 && (!comments || s.indexOf('%%') === -1) && (!htmlComments || s.indexOf('<!--') === -1)) {
    return s;
  }
  const m = new Masker(s);
  const runs = new BacktickRuns(s);
  let noCommentClose = false;
  let noHtmlClose = false;
  const n = s.length;
  let i = 0;
  while (i < n) {
    const c = s.charCodeAt(i);
    if (c === 92 /* \ */) {
      i += 2;
      continue;
    }
    if (c === 96 /* ` */) {
      const len = runLength(s, i, 96);
      const close = runs.closing(i + len, len);
      if (close !== -1) {
        m.mask(i, close + len);
        i = close + len;
        continue;
      }
      i += len;
      continue;
    }
    if (comments && c === 37 /* % */ && s.charCodeAt(i + 1) === 37 && !noCommentClose) {
      const close = s.indexOf('%%', i + 2);
      if (close !== -1) {
        m.mask(i, close + 2);
        i = close + 2;
        continue;
      }
      noCommentClose = true;
    }
    if (htmlComments && c === 60 /* < */ && !noHtmlClose && s.startsWith('<!--', i)) {
      const close = s.indexOf('-->', i + 4);
      if (close !== -1) {
        m.mask(i, close + 3);
        i = close + 3;
        continue;
      }
      noHtmlClose = true;
    }
    i++;
  }
  return m.result();
}

/** Opening code fence: indentation, fence run and info string. */
export interface FenceOpen {
  char: '`' | '~';
  len: number;
  info: string;
}

const FENCE_OPEN_RE = /^[ \t]*(`{3,}|~{3,})(.*)$/;
const FENCE_CLOSE_RE = /^[ \t]*(`{3,}|~{3,})[ \t]*$/;

/** Parse an opening code fence line (``` or ~~~, any indentation), or null. */
export function fenceOpen(s: string): FenceOpen | null {
  const m = FENCE_OPEN_RE.exec(s);
  if (!m) return null;
  const run = m[1]!;
  const info = m[2] ?? '';
  const char = run[0] === '`' ? '`' : '~';
  // A backtick fence's info string may not contain backticks (CommonMark).
  if (char === '`' && info.includes('`')) return null;
  return { char, len: run.length, info: info.trim() };
}

/** True when `s` closes the fence `open` (same character, at least as long, nothing after). */
export function isFenceClose(s: string, open: FenceOpen): boolean {
  const m = FENCE_CLOSE_RE.exec(s);
  if (!m) return false;
  const run = m[1]!;
  return run[0] === open.char && run.length >= open.len;
}

/** Blockquote markers at the start of a line (`> `, `> > `, `>`). */
const QUOTE_PREFIX_RE = /^(?:[ \t]{0,3}>[ \t]?)+/;
/** A list item marker followed by whitespace. */
const ITEM_PREFIX_RE = /^[ \t]*(?:[-*+]|\d{1,9}[.)])[ \t]+/;

/** Number of `>` markers in a line's blockquote prefix and the prefix length. */
function quotePrefixOf(text: string): { depth: number; len: number } {
  const m = QUOTE_PREFIX_RE.exec(text);
  if (!m) return { depth: 0, len: 0 };
  let depth = 0;
  for (let k = 0; k < m[0].length; k++) if (m[0].charCodeAt(k) === 62) depth++;
  return { depth, len: m[0].length };
}

/** A fenced code block opened somewhere in the document (possibly inside containers). */
interface OpenFence {
  open: FenceOpen;
  /** Blockquote depth of the opening line: the fence ends with its quote. */
  quotes: number;
  /** Marker column of a list item whose marker line opened the fence (-1 otherwise): a line indented at or before it ends the item and the fence. */
  itemCol: number;
}

/**
 * A fence opened on `text`, also behind container prefixes: blockquote / callout markers
 * (`> ```mermaid`) and a list item marker (`- ```js`).
 */
function fenceOpenIn(text: string): OpenFence | null {
  const q = quotePrefixOf(text);
  let rest = q.len ? text.slice(q.len) : text;
  let itemCol = -1;
  const im = ITEM_PREFIX_RE.exec(rest);
  if (im) {
    itemCol = indentOf(rest).col;
    rest = rest.slice(im[0].length);
  }
  const open = fenceOpen(rest);
  return open ? { open, quotes: q.depth, itemCol } : null;
}

/** Does line `text` still belong to the fenced code block `f`? Returns 'close' for its closing fence. */
function fenceLine(text: string, f: OpenFence): 'inside' | 'close' | 'outside' {
  const q = quotePrefixOf(text);
  if (q.depth < f.quotes) return 'outside';
  const rest = q.len ? text.slice(q.len) : text;
  if (f.itemCol >= 0 && !isBlank(rest) && indentOf(rest).col <= f.itemCol) return 'outside';
  return isFenceClose(rest, f.open) ? 'close' : 'inside';
}

/**
 * Document-level `%%comment%%` ranges `[from, to)` in document order. Comments may span many
 * lines (Obsidian style, e.g. Excalidraw drawings or Kanban settings); `%%` inside fenced code
 * (also fences inside list items, blockquotes and callouts — Mermaid uses `%%` for its
 * comments) or inline code spans does not count. An unclosed `%%` is treated as literal text
 * (so a typo never hides the rest of a note from the index). Linear time.
 */
export function docCommentRanges(content: string, lines: Lines): [number, number][] {
  const ranges: [number, number][] = [];
  if (content.indexOf('%%') === -1) return ranges;
  let commentStart = -1;
  let fence: OpenFence | null = null;
  for (let li = 0; li < lines.texts.length; li++) {
    const text = lines.texts[li]!;
    const base = lines.starts[li]!;
    if (commentStart === -1 && fence) {
      const where = fenceLine(text, fence);
      if (where !== 'outside') {
        if (where === 'close') fence = null;
        continue;
      }
      fence = null;
    }
    if (commentStart === -1) {
      const fo = fenceOpenIn(text);
      if (fo) {
        fence = fo;
        continue;
      }
    }
    if (commentStart === -1 && text.indexOf('%%') === -1) continue;
    let i = 0;
    const runs = new BacktickRuns(text);
    while (i < text.length) {
      if (commentStart !== -1) {
        const close = text.indexOf('%%', i);
        if (close === -1) break;
        ranges.push([commentStart, base + close + 2]);
        commentStart = -1;
        i = close + 2;
        continue;
      }
      const c = text.charCodeAt(i);
      if (c === 92) {
        i += 2;
        continue;
      }
      if (c === 96) {
        const len = runLength(text, i, 96);
        const close = runs.closing(i + len, len);
        i = close !== -1 ? close + len : i + len;
        continue;
      }
      if (c === 37 && text.charCodeAt(i + 1) === 37) {
        commentStart = base + i;
        i += 2;
        continue;
      }
      i++;
    }
  }
  return ranges;
}

/** Blank `ranges` (sorted, non-overlapping) of `content`, keeping length and line breaks. */
export function maskRanges(content: string, ranges: readonly (readonly [number, number])[]): string {
  if (ranges.length === 0) return content;
  const m = new Masker(content);
  for (const [a, b] of ranges) m.mask(a, b);
  return m.result();
}

/** `content` with its document-level `%%comments%%` blanked (see {@link docCommentRanges}). */
export function maskDocComments(content: string, lines: Lines): string {
  return maskRanges(content, docCommentRanges(content, lines));
}
