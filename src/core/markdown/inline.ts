/**
 * Inline syntax scanner (docs/SYNTAX.md §2-3): wiki links, embeds, block references,
 * Markdown links and images, autolinks, bare URLs and tags.
 *
 * The scanner is a single left-to-right pass over a *masked* string (inline code and
 * comments blanked, see ./mask.ts) with values read from the original string at the same
 * positions. Constructs are matched with priority by position, so a tag inside a link or a
 * URL is never reported. Work per position is bounded, so the scan is linear in practice.
 */
import type { Link, LinkKind } from '../types';
import { maskInline } from './mask';
import { splitLines } from './lines';

/** A tag occurrence (name without `#`, as written, NFC-normalised). */
export interface TagHit {
  name: string;
  from: number;
  to: number;
  line: number;
}

/** Result of {@link scanInline}. */
export interface InlineScan {
  links: Link[];
  tags: TagHit[];
}

/**
 * Maps positions of a scanned string back to the document. The scanned string is made of
 * segments (usually lines); segment `k` starts at `pos[k]` in the scanned string, at document
 * offset `offset[k]` and on document line `line[k]`.
 */
export interface Segments {
  pos: number[];
  offset: number[];
  line: number[];
}

/** Longest link text / destination / wiki link we try to match (bounds work per `[`). */
const MAX_LINK_TEXT = 1000;
const MAX_DEST = 2048;

const TAG_BODY_RE = /[\p{L}\p{N}\p{M}_\-/]+/uy;
const HAS_LETTER_OR_DIGIT_RE = /[\p{L}\p{N}]/u;
const ALL_DIGITS_RE = /^\d+$/;
const BLOCKREF_RE = /\(\(([A-Za-z0-9_-]+)\)\)/y;
const AUTOLINK_RE = /<([A-Za-z][A-Za-z0-9+.-]{1,31}:[^\s<>]*)>/y;
const EMAIL_AUTOLINK_RE =
  /<([A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?(?:\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*)>/y;
const SCHEME_RE = /^[A-Za-z][A-Za-z0-9+.-]*:/;
const WINDOWS_DRIVE_RE = /^[A-Za-z]:[\\/]/;
const ESCAPE_RE = /\\([!-/:-@[-`{-~])/g;

/** Cheap pre-check: characters that can start a link or a tag. */
const TRIGGER_RE = /[[(<#]|https?:\/\//i;
const NON_ASCII_RE = /[\u0080-\uffff]/;

/** Position mapper with a moving cursor (queries are mostly increasing). */
class PosMap {
  private k = 0;
  constructor(private readonly seg: Segments) {}

  private seek(p: number): number {
    const pos = this.seg.pos;
    if (this.k >= pos.length || pos[this.k]! > p) this.k = 0;
    while (this.k + 1 < pos.length && pos[this.k + 1]! <= p) this.k++;
    return this.k;
  }

  offset(p: number): number {
    const k = this.seek(p);
    return (this.seg.offset[k] ?? 0) + (p - (this.seg.pos[k] ?? 0));
  }

  line(p: number): number {
    return this.seg.line[this.seek(p)] ?? 0;
  }
}

/** Segments for a plain string starting at document offset `base` on line `line`. */
export function segmentsForText(text: string, base = 0, line = 0): Segments {
  const lines = splitLines(text);
  return {
    pos: lines.starts,
    offset: lines.starts.map((s) => base + s),
    line: lines.starts.map((_, i) => line + i),
  };
}

function isWs(c: string | undefined): boolean {
  return c === ' ' || c === '\t' || c === '\n' || c === '\r' || (c !== undefined && c !== '' && /\s/.test(c));
}

/** Whitespace test on a char code (ASCII fast path, Unicode spaces via the regex). */
function isWsAt(s: string, i: number): boolean {
  const c = s.charCodeAt(i);
  if (c === 32 || c === 9 || c === 10 || c === 13) return true;
  return c > 127 && isWs(s[i]);
}

/** May a bare URL start after the character at `i`? */
function urlBoundary(s: string, i: number): boolean {
  return isWsAt(s, i) || URL_PRECEDERS.has(s[i]!);
}

/** May a tag start after the character at `i` (start, whitespace or "(")? */
function tagBoundary(s: string, i: number): boolean {
  return s.charCodeAt(i) === 40 || isWsAt(s, i);
}

/** Decode %XX escapes; malformed sequences are kept as written. */
export function safeDecode(s: string): string {
  if (s.indexOf('%') === -1) return s;
  try {
    return decodeURIComponent(s);
  } catch {
    return s.replace(/%[0-9A-Fa-f]{2}/g, (m) => {
      try {
        return decodeURIComponent(m);
      } catch {
        return m;
      }
    });
  }
}

/** Parts of a Markdown inline link `[text](dest "title")` (positions in the scanned string). */
export interface MdLinkParts {
  textFrom: number;
  textTo: number;
  destFrom: number;
  destTo: number;
  /** Destination written as `<…>`. */
  angle: boolean;
  /** Position after the closing `)`. */
  end: number;
}

/** Skip spaces and tabs. */
function skipBlanks(s: string, j: number): number {
  let c = s.charCodeAt(j);
  while (c === 32 || c === 9) c = s.charCodeAt(++j);
  return j;
}

/** Parse a Markdown inline link whose `[` is at `p` (masked string). Null when not a link. */
export function parseMdLink(s: string, p: number): MdLinkParts | null {
  const n = s.length;
  const limit = Math.min(n, p + 1 + MAX_LINK_TEXT);
  let depth = 1;
  let j = p + 1;
  for (; j < limit; j++) {
    const c = s.charCodeAt(j);
    if (c === 92 /* \ */) {
      j++;
      continue;
    }
    if (c === 91 /* [ */) depth++;
    else if (c === 93 /* ] */) {
      depth--;
      if (depth === 0) break;
    }
  }
  if (depth !== 0 || j >= limit || s.charCodeAt(j + 1) !== 40 /* ( */) return null;
  const textTo = j;
  j = skipBlanks(s, j + 2);
  let destFrom: number;
  let destTo: number;
  let angle = false;
  if (s.charCodeAt(j) === 60 /* < */) {
    destFrom = j + 1;
    let k = destFrom;
    for (; k < n && k - destFrom < MAX_DEST; k++) {
      const c = s.charCodeAt(k);
      if (c === 62 || c === 10 || c === 60) break;
    }
    if (s.charCodeAt(k) !== 62 /* > */) return null;
    destTo = k;
    angle = true;
    j = k + 1;
  } else {
    destFrom = j;
    let parens = 0;
    while (j < n && j - destFrom < MAX_DEST) {
      const c = s.charCodeAt(j);
      if (c === 32 || c === 9 || c === 10 || c === 13) break;
      if (c === 92) {
        j += 2;
        continue;
      }
      if (c === 40) parens++;
      else if (c === 41) {
        if (parens === 0) break;
        parens--;
      }
      j++;
    }
    destTo = Math.min(j, n);
  }
  j = skipBlanks(s, j);
  const q = s.charCodeAt(j);
  if (q === 34 || q === 39 || (q === 40 && j > destTo)) {
    const close = s.indexOf(q === 40 ? ')' : String.fromCharCode(q), j + 1);
    if (close === -1 || close - j > MAX_DEST) return null;
    j = skipBlanks(s, close + 1);
  }
  if (s.charCodeAt(j) !== 41 /* ) */) return null;
  return { textFrom: p + 1, textTo, destFrom, destTo, angle, end: j + 1 };
}

/** Split a wiki link's inner text into target / heading / block id / display. */
export function parseWikiInner(inner: string): Pick<Link, 'target' | 'display' | 'heading' | 'blockId'> | null {
  let left = inner;
  let display: string | undefined;
  const pipe = inner.indexOf('|');
  if (pipe !== -1) {
    left = inner.slice(0, pipe);
    // `[[Page\|Alias]]` inside tables escapes the pipe.
    if (left.endsWith('\\')) left = left.slice(0, -1);
    display = inner.slice(pipe + 1).trim();
  }
  const hash = left.indexOf('#');
  const target = (hash === -1 ? left : left.slice(0, hash)).trim();
  const frag = hash === -1 ? '' : left.slice(hash + 1).trim();
  if (!target && !frag) return null;
  const out: Pick<Link, 'target' | 'display' | 'heading' | 'blockId'> = { target };
  if (display) out.display = display;
  if (frag.startsWith('^')) {
    const id = frag.slice(1).trim();
    if (id) out.blockId = id;
  } else if (frag) out.heading = frag;
  return out;
}

/** Split a Markdown link destination into target / heading / block id. */
export function parseMdDestination(rawDest: string): Pick<Link, 'target' | 'heading' | 'blockId'> {
  const raw = rawDest.replace(ESCAPE_RE, '$1').trim();
  if (SCHEME_RE.test(raw) && !WINDOWS_DRIVE_RE.test(raw)) return { target: raw };
  const hash = raw.indexOf('#');
  const target = safeDecode(hash === -1 ? raw : raw.slice(0, hash));
  const out: Pick<Link, 'target' | 'heading' | 'blockId'> = { target };
  if (hash !== -1) {
    const frag = safeDecode(raw.slice(hash + 1));
    if (frag.startsWith('^')) {
      if (frag.length > 1) out.blockId = frag.slice(1);
    } else if (frag) out.heading = frag;
  }
  return out;
}

/** Characters that may precede a bare URL. */
const URL_PRECEDERS = new Set(['(', '*', '_', '~', '"', "'", '[', '>']);
const URL_TRAILING_PUNCT = new Set(['?', '!', '.', ',', ':', ';', '*', '_', '~', "'", '"']);

/** End of a bare URL starting at `p` (GFM autolink extension rules), or -1. Linear in the URL length. */
function bareUrlEnd(s: string, p: number): number {
  const c4 = s.charCodeAt(p + 4);
  // "http" followed by "://" or "s://" (case-insensitive).
  if ((s.charCodeAt(p + 1) | 32) !== 116 || (s.charCodeAt(p + 2) | 32) !== 116 || (s.charCodeAt(p + 3) | 32) !== 112) return -1;
  const schemeLen = s.startsWith('://', p + 4) ? 7 : (c4 | 32) === 115 && s.startsWith('://', p + 5) ? 8 : 0;
  if (!schemeLen) return -1;
  let j = p + schemeLen;
  // Bracket balance of the URL, kept up to date while trailing characters are trimmed.
  let parens = 0;
  let brackets = 0;
  while (j < s.length) {
    const c = s.charCodeAt(j);
    if (c === 60 || c === 62 || c === 34 || c === 96 || c === 32 || c === 9 || c === 10 || c === 13) break;
    if (c > 127 && isWs(s[j])) break;
    if (c === 40) parens++;
    else if (c === 41) parens--;
    else if (c === 91) brackets++;
    else if (c === 93) brackets--;
    j++;
  }
  // Trim trailing punctuation and unbalanced closing brackets.
  while (j > p + schemeLen) {
    const last = s[j - 1]!;
    if (URL_TRAILING_PUNCT.has(last)) j--;
    else if (last === ')' && parens < 0) {
      parens++;
      j--;
    } else if (last === ']' && brackets < 0) {
      brackets++;
      j--;
    } else break;
  }
  return j > p + schemeLen ? j : -1;
}

/** Validate a `#tag` body; returns the cleaned name or null. */
function cleanTagBody(body: string): string | null {
  let name = body;
  while (name.endsWith('/')) name = name.slice(0, -1);
  if (!name || ALL_DIGITS_RE.test(name) || !HAS_LETTER_OR_DIGIT_RE.test(name)) return null;
  return NON_ASCII_RE.test(name) ? name.normalize('NFC') : name;
}

/** Mutable scan state (caches that keep failed searches from repeating). */
interface ScanState {
  /** Cached position of the next `]]` (>= some earlier search start), -2 = none, -1 = unknown. */
  wikiClose: number;
  /** Same for the next `]` (bounds the work of Markdown link attempts on unclosed `[`). */
  bracketClose: number;
}

/**
 * Position of the next `needle` at or after `from`, cached in `st[key]` (the scan only moves
 * forward, so a cached hit at or after `from` is still the first one). -1 when there is none.
 */
function nextCached(s: string, from: number, needle: string, st: ScanState, key: 'wikiClose' | 'bracketClose'): number {
  const cached = st[key];
  if (cached === -2) return -1;
  if (cached >= from) return cached;
  const found = s.indexOf(needle, from);
  st[key] = found === -1 ? -2 : found;
  return found;
}

/** Find the `]]` closing a wiki link opened at `p` (first `[`), on the same line. */
function wikiCloseAt(s: string, p: number, st: ScanState): number {
  const close = nextCached(s, p + 2, ']]', st, 'wikiClose');
  if (close === -1 || close - p > MAX_LINK_TEXT) return -1;
  // Bounded by MAX_LINK_TEXT (searching the rest of a long line would be quadratic).
  for (let k = p + 2; k < close; k++) {
    const c = s.charCodeAt(k);
    if (c === 10 || c === 13) return -1;
  }
  return close;
}

/**
 * Scan `masked` for links and tags. `orig` must have the same length (values are read from
 * it); `seg` maps positions to document offsets and lines.
 */
export function scanInline(orig: string, masked: string, seg: Segments): InlineScan {
  const links: Link[] = [];
  const tags: TagHit[] = [];
  if (!TRIGGER_RE.test(masked)) return { links, tags };
  const map = new PosMap(seg);
  const st: ScanState = { wikiClose: -1, bracketClose: -1 };
  const s = masked;
  const n = s.length;

  const pushLink = (kind: LinkKind, from: number, to: number, parts: Omit<Link, 'kind' | 'line' | 'from' | 'to'>): void => {
    const link: Link = { kind, target: parts.target, line: map.line(from), from: map.offset(from), to: map.offset(to) };
    if (parts.display !== undefined) link.display = parts.display;
    if (parts.heading !== undefined) link.heading = parts.heading;
    if (parts.blockId !== undefined) link.blockId = parts.blockId;
    links.push(link);
  };

  /** Try a wiki link / embed whose first `[` is at `p`; returns the end position or -1. */
  const wiki = (p: number, start: number, kind: LinkKind): number => {
    const close = wikiCloseAt(s, p, st);
    if (close === -1) return -1;
    const inner = orig.slice(p + 2, close);
    if (inner.indexOf('[[') !== -1) return -1;
    const parts = parseWikiInner(inner);
    if (!parts) return -1;
    pushLink(kind, start, close + 2, parts);
    return close + 2;
  };

  /** Try a Markdown link / image whose `[` is at `p`. */
  const mdLink = (p: number, start: number): number => {
    if (s[p + 1] === '^') return -1; // footnote reference
    // No `]` within reach: not a link (cheap rejection of long runs of unclosed brackets).
    const rb = nextCached(s, p + 1, ']', st, 'bracketClose');
    if (rb === -1 || rb - p > MAX_LINK_TEXT) return -1;
    const parts = parseMdLink(s, p);
    if (!parts) return -1;
    const dest = parseMdDestination(orig.slice(parts.destFrom, parts.destTo));
    if (!dest.target && dest.heading === undefined && dest.blockId === undefined) return parts.end;
    const display = orig.slice(parts.textFrom, parts.textTo).trim();
    pushLink('markdown', start, parts.end, display ? { ...dest, display } : dest);
    return parts.end;
  };

  let i = 0;
  while (i < n) {
    const c = s.charCodeAt(i);
    let end = -1;
    switch (c) {
      case 92: // \
        i += 2;
        continue;
      case 33: {
        // !
        const c1 = s.charCodeAt(i + 1);
        if (c1 === 91) {
          end = s.charCodeAt(i + 2) === 91 ? wiki(i + 1, i, 'embed') : mdLink(i + 1, i);
        } else if (c1 === 40 && s.charCodeAt(i + 2) === 40) {
          BLOCKREF_RE.lastIndex = i + 1;
          const m = BLOCKREF_RE.exec(s);
          if (m) {
            end = i + 1 + m[0].length;
            pushLink('blockembed', i, end, { target: m[1]!, blockId: m[1]! });
          }
        }
        break;
      }
      case 91: // [
        end = s.charCodeAt(i + 1) === 91 ? wiki(i, i, 'wiki') : mdLink(i, i);
        break;
      case 40: // (
        if (s.charCodeAt(i + 1) === 40) {
          BLOCKREF_RE.lastIndex = i;
          const m = BLOCKREF_RE.exec(s);
          if (m) {
            end = i + m[0].length;
            pushLink('blockref', i, end, { target: m[1]!, blockId: m[1]! });
          }
        }
        break;
      case 60: {
        // <
        AUTOLINK_RE.lastIndex = i;
        const m = AUTOLINK_RE.exec(s);
        if (m) {
          end = i + m[0].length;
          pushLink('url', i, end, { target: orig.slice(i + 1, end - 1) });
          break;
        }
        EMAIL_AUTOLINK_RE.lastIndex = i;
        const e = EMAIL_AUTOLINK_RE.exec(s);
        if (e) {
          end = i + e[0].length;
          pushLink('url', i, end, { target: `mailto:${orig.slice(i + 1, end - 1)}` });
        }
        break;
      }
      case 104: // h
      case 72: {
        // H
        if (i === 0 || urlBoundary(s, i - 1)) {
          const e = bareUrlEnd(s, i);
          if (e !== -1) {
            end = e;
            pushLink('url', i, end, { target: orig.slice(i, end) });
          }
        }
        break;
      }
      case 35: {
        // #
        if (i !== 0 && !tagBoundary(s, i - 1)) break;
        if (s.charCodeAt(i + 1) === 91 && s.charCodeAt(i + 2) === 91) {
          const close = wikiCloseAt(s, i + 1, st);
          if (close !== -1) {
            const name = orig.slice(i + 3, close).trim();
            if (name && !/[[\]|]/.test(name)) {
              end = close + 2;
              tags.push({ name: name.normalize('NFC'), from: map.offset(i), to: map.offset(end), line: map.line(i) });
            }
          }
          break;
        }
        TAG_BODY_RE.lastIndex = i + 1;
        const m = TAG_BODY_RE.exec(s);
        if (!m) break;
        const name = cleanTagBody(orig.slice(i + 1, i + 1 + m[0].length));
        if (name === null) {
          end = i + 1 + m[0].length;
          break;
        }
        // The range covers the written characters (NFC may change the name's length).
        end = i + 1 + m[0].replace(/\/+$/, '').length;
        tags.push({ name, from: map.offset(i), to: map.offset(end), line: map.line(i) });
        break;
      }
      default:
        break;
    }
    i = end > i ? end : i + 1;
  }
  return { links, tags };
}

/** Deduplicate tag names case-insensitively, keeping the first-seen spelling. */
export function uniqueTags(names: Iterable<string>): string[] {
  const seen = new Set<string>();
  const out: string[] = [];
  for (const n of names) {
    const key = n.toLowerCase();
    if (seen.has(key)) continue;
    seen.add(key);
    out.push(n);
  }
  return out;
}

/**
 * Extract links and tags from inline Markdown `text` (one or more lines). Inline code,
 * `%%comments%%` and HTML comments are skipped. `baseOffset` / `line` position the text
 * in its document: link `from`/`to` are absolute offsets, `line` is 0-based.
 *
 * Tags are returned without `#`, deduplicated case-insensitively in first-seen spelling
 * (NFC). Same-page links (`[[#^id]]`) have an empty `target`.
 */
export function extractInline(text: string, baseOffset = 0, line = 0): { links: Link[]; tags: string[] } {
  const masked = maskInline(text);
  const res = scanInline(text, masked, segmentsForText(text, baseOffset, line));
  return { links: res.links, tags: uniqueTags(res.tags.map((t) => t.name)) };
}
