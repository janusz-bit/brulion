/**
 * When is Markdown syntax revealed? Pure helpers (no DOM) shared by the inline plugin and the
 * block field, unit-tested in reveal.test.ts.
 *
 * Rules (Obsidian-like):
 * * inline elements (emphasis, links, chips, refs, embeds) show their source while a selection
 *   range touches them — the cursor inside or directly next to them;
 * * line prefixes (heading `#`, quote `>`, block ids, property `::`) show while a selection
 *   range is on that line;
 * * block widgets (tables, frontmatter, queries) show their source while a selection range
 *   touches the block;
 * * an unfocused editor reveals nothing (journal streams render fully).
 */
import type { EditorState, Text } from '@codemirror/state';
import { previewFocus } from './state';

export interface Span {
  from: number;
  to: number;
}

/** Whether any selection range touches `[from, to]` (bounds inclusive). */
export function touches(sel: readonly Span[], from: number, to: number): boolean {
  for (const r of sel) if (r.from <= to && r.to >= from) return true;
  return false;
}

/** Whether any selection range lies strictly inside `(from, to)` or overlaps it. */
export function touchesInside(sel: readonly Span[], from: number, to: number): boolean {
  for (const r of sel) if (r.from < to && r.to > from) return true;
  return false;
}

/** The selection that reveals syntax: the real one when focused, none otherwise. */
export function revealSelection(state: EditorState): readonly Span[] {
  if (!state.field(previewFocus, false)) return [];
  return state.selection.ranges;
}

/** 1-based line numbers touched by the selection ranges. */
export function selectedLines(doc: Text, sel: readonly Span[]): Set<number> {
  const out = new Set<number>();
  for (const r of sel) {
    const a = doc.lineAt(r.from).number;
    const b = doc.lineAt(r.to).number;
    for (let n = a; n <= b && n - a < 10_000; n++) out.add(n);
  }
  return out;
}

/**
 * Marks to hide for an inline element `[from, to]` with syntax marks `marks`: all of them
 * while the selection does not touch the element, none otherwise.
 */
export function hiddenMarks(sel: readonly Span[], from: number, to: number, marks: readonly Span[]): Span[] {
  return touches(sel, from, to) ? [] : marks.filter((m) => m.to > m.from);
}

/** Sort and merge overlapping/adjacent spans. */
export function mergeSpans(spans: readonly Span[]): Span[] {
  const sorted = [...spans].sort((a, b) => a.from - b.from || a.to - b.to);
  const out: Span[] = [];
  for (const s of sorted) {
    const last = out[out.length - 1];
    if (last && s.from <= last.to + 1) last.to = Math.max(last.to, s.to);
    else out.push({ from: s.from, to: s.to });
  }
  return out;
}

/** Intersect line-aligned spans with the visible ranges. */
export function clipSpans(spans: readonly Span[], visible: readonly Span[]): Span[] {
  const out: Span[] = [];
  for (const s of spans) {
    for (const v of visible) {
      const from = Math.max(s.from, v.from);
      const to = Math.min(s.to, v.to);
      if (from <= to) out.push({ from, to });
    }
  }
  return out;
}

/**
 * Document ranges whose decorations may change when the selection moves from `before` to
 * `after`: every touched line of both selections, widened by `expand` (e.g. to the enclosing
 * paragraph or code block), line-aligned and merged.
 */
export function revealDelta(
  doc: Text,
  before: readonly Span[],
  after: readonly Span[],
  expand: (pos: number) => Span | null = () => null,
): Span[] {
  const spans: Span[] = [];
  for (const r of [...before, ...after]) {
    const from = doc.lineAt(Math.min(r.from, doc.length)).from;
    const to = doc.lineAt(Math.min(r.to, doc.length)).to;
    spans.push({ from, to });
    for (const p of r.from === r.to ? [r.from] : [r.from, r.to]) {
      const e = expand(Math.min(p, doc.length));
      if (e) spans.push({ from: doc.lineAt(e.from).from, to: doc.lineAt(Math.min(e.to, doc.length)).to });
    }
  }
  return mergeSpans(spans);
}

/** Whether two selections reveal the same thing (same ranges, or both empty). */
export function sameSelection(a: readonly Span[], b: readonly Span[]): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i]!.from !== b[i]!.from || a[i]!.to !== b[i]!.to) return false;
  return true;
}
