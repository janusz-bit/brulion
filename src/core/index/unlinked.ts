/**
 * Unlinked references: plain-text mentions of a page's title or aliases in other notes —
 * whole words, case-insensitive, never inside code, links, URLs, tags or frontmatter.
 */
import { maskInline, splitLines } from '@/core/markdown';
import type { ParsedNote } from '@/core/types';
import { escapeRegExp, isWholeWord, lineExcerpt } from './text';

/** A compiled page name to look for. */
export interface MentionMatcher {
  /** Lower-cased name (cheap pre-filter with `includes`). */
  lower: string;
  re: RegExp;
}

/** One mention: 0-based line and an excerpt of about 120 characters. */
export interface Mention {
  line: number;
  excerpt: string;
}

/** Minimum name length (code points) worth looking for. */
const MIN_NAME_LENGTH = 2;

/** Tags (`#tag`, `#[[tag name]]`) — masked so `#Dom` is not a mention of "Dom". */
const TAG_RE = /(^|[\s(])(#(?:\[\[[^\]\n]*\]\]|[\p{L}\p{N}\p{M}_\-/]+))/gu;

/** Block kinds whose lines never contain mentions. */
const SKIPPED_KINDS = new Set(['code', 'html', 'frontmatter']);

/**
 * Build matchers for a page's names (title + aliases): NFC, de-duplicated case-insensitively,
 * longest first (so "Dom Jana" wins over "Dom" at the same position), too short names dropped.
 */
export function mentionMatchers(names: readonly string[]): MentionMatcher[] {
  const seen = new Set<string>();
  const out: MentionMatcher[] = [];
  for (const raw of names) {
    const name = raw.normalize('NFC').trim();
    if ([...name].length < MIN_NAME_LENGTH) continue;
    const lower = name.toLowerCase();
    if (seen.has(lower)) continue;
    seen.add(lower);
    out.push({ lower, re: new RegExp(escapeRegExp(name).replace(/\s+/g, '\\s+'), 'giu') });
  }
  return out.sort((a, b) => b.lower.length - a.lower.length);
}

/** Replace `[from, to)` of `chars` with spaces (newlines kept). */
function blank(chars: string[], from: number, to: number): void {
  for (let i = Math.max(0, from); i < to && i < chars.length; i++) if (chars[i] !== '\n' && chars[i] !== '\r') chars[i] = ' ';
}

/** Content with code spans, comments and every link / URL replaced by spaces (same length). */
function maskedContent(content: string, note: ParsedNote): string {
  const masked = maskInline(content);
  if (note.links.length === 0) return masked;
  const chars = masked.split('');
  for (const l of note.links) blank(chars, l.from, l.to);
  return chars.join('');
}

/** Lines (0-based) that belong to code, HTML or frontmatter blocks. */
function skippedLines(note: ParsedNote): Set<number> {
  const out = new Set<number>();
  for (const b of note.allBlocks) {
    if (!SKIPPED_KINDS.has(b.kind)) continue;
    for (let l = b.line; l <= b.ownEndLine; l++) out.add(l);
  }
  return out;
}

/** Earliest whole-word match of any matcher in `line` (longer names win ties). */
function firstMatch(line: string, matchers: readonly MentionMatcher[]): { start: number; end: number } | null {
  let best: { start: number; end: number } | null = null;
  for (const m of matchers) {
    m.re.lastIndex = 0;
    let hit: RegExpExecArray | null;
    while ((hit = m.re.exec(line)) !== null) {
      const start = hit.index;
      const end = start + hit[0].length;
      if (hit[0].length === 0) {
        m.re.lastIndex++;
        continue;
      }
      if (best && start >= best.start) break;
      if (isWholeWord(line, start, end)) {
        best = { start, end };
        break;
      }
    }
  }
  return best;
}

/**
 * Mentions of `matchers` in one note (at most `max`, one per line). `content` must be the
 * text `note` was parsed from (link offsets are used for masking).
 */
export function findMentions(content: string, note: ParsedNote, matchers: readonly MentionMatcher[], max: number): Mention[] {
  if (max <= 0 || matchers.length === 0) return [];
  const lines = splitLines(content);
  const masked = maskedContent(content, note);
  const skip = skippedLines(note);
  const out: Mention[] = [];
  for (let i = 0; i < lines.texts.length && out.length < max; i++) {
    if (skip.has(i)) continue;
    let orig = lines.texts[i]!;
    const start = lines.starts[i]!;
    let text = masked.slice(start, start + orig.length).replace(TAG_RE, (_m, pre: string, tag: string) => pre + ' '.repeat(tag.length));
    if (!text.trim()) continue;
    const nfc = orig.normalize('NFC');
    if (nfc !== orig) {
      // Decomposed text (e.g. from macOS): compare in NFC; offsets only feed the excerpt.
      orig = nfc;
      text = text.normalize('NFC');
    }
    const hit = firstMatch(text, matchers);
    if (hit) out.push({ line: i, excerpt: lineExcerpt(orig, hit.start, hit.end) });
  }
  return out;
}
