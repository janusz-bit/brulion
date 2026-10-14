/**
 * Pure content edit helpers (content in → content out). Only the affected lines change;
 * everything else, including `\r\n` / `\n` line endings, is preserved byte for byte.
 */
import {
  EN_WEEKDAYS_SHORT,
  addDaysLocal,
  diffDaysLocal,
  joinDateTime,
  nextOccurrence,
  parseRecurrence,
  repeaterToRule,
  toFileDateTime,
  weekdayOf,
  type RecurrenceRule,
} from '@/core/dates';
import type { Block, LocalDate, LocalDateTime, TaskStatus } from '../types';
import { eolFor, indentOf, isBlank, type Lines } from './lines';
import { maskInline } from './mask';
import { blockAtLine, parseNote } from './parse';
import { matchListMarker, nodeAtLine, parseStructure, preOrder, type LineView, type Node, type Structure } from './structure';
import { STATUS_CHAR, checkboxCharFor, keywordFor, matchTaskMarker, type TaskMarker } from './tasks';
import { TOKENS, blockIdAtEnd, findTokens, type Token } from './tokens';

/** A text replacement in line coordinates. */
interface Edit {
  from: number;
  to: number;
  insert: string;
}

/** Apply non-overlapping edits (any order). */
function applyEdits(text: string, edits: Edit[]): string {
  const sorted = [...edits].sort((a, b) => a.from - b.from);
  let out = '';
  let last = 0;
  for (const e of sorted) {
    if (e.from < last) continue;
    out += text.slice(last, e.from) + e.insert;
    last = e.to;
  }
  return out + text.slice(last);
}

/** A removal of [from, to) that also swallows the whitespace before it. */
function removal(text: string, from: number, to: number): Edit {
  let f = from;
  while (f > 0 && (text[f - 1] === ' ' || text[f - 1] === '\t')) f--;
  return { from: f, to, insert: '' };
}

/** Replace the content of one line, keeping its terminator and the rest of the document. */
function replaceLines(content: string, lines: Lines, line: number, newLines: string[]): string {
  const start = lines.starts[line]!;
  const end = start + lines.texts[line]!.length;
  const eol = eolFor(lines, line);
  return content.slice(0, start) + newLines.join(eol) + content.slice(end);
}

/** Where a status change applies: the line text as seen by its container. */
interface Target {
  st: Structure;
  node: Node | null;
  /** Document line to rewrite. */
  line: number;
  /** Text of that line inside its container (callout prefixes removed). */
  view: LineView;
  /** Characters of container prefix before `view.text` in the document line. */
  prefix: number;
  /** Where to insert `- [ ] ` for a plain line (index in `view.text`). */
  insertAt: number;
}

function isCodeLine(node: Node, line: number): boolean {
  if (node.kind === 'code' || node.kind === 'html') return true;
  const own = node.own.find((l) => l.view.line === line);
  return own !== undefined && (own.role === 'code' || own.role === 'fence');
}

/**
 * Lines that must not become tasks: headings, rules, table rows, a callout's header line and
 * syntax-only lines (setext underlines, standalone `^id` lines). Prefixing them with `- [ ] `
 * would destroy the block (e.g. split a table).
 */
function isStructuralLine(node: Node, line: number): boolean {
  if (node.kind === 'heading' || node.kind === 'hr' || node.kind === 'table') return true;
  if (node.kind === 'callout' && line === node.line) return true;
  return node.own.some((l) => l.view.line === line && l.role === 'meta');
}

/** Container prefix (`> `, `> > `) of a document line. */
function quotePrefix(text: string): string {
  return /^(?:[ \t]{0,3}>[ \t]?)*/.exec(text)?.[0] ?? '';
}

/** Resolve the line a task edit applies to (a list item's first line), or null when not editable. */
function resolveTarget(content: string, line: number): Target | null {
  const st = parseStructure(content);
  if (!Number.isInteger(line) || line < 0 || line >= st.lines.texts.length) return null;
  const node = nodeAtLine(st.roots, line);
  if (node && (node.kind === 'frontmatter' || isCodeLine(node, line) || isStructuralLine(node, line))) return null;
  const lineMasked = st.views[line]!.masked;
  if (isBlank(lineMasked.slice(quotePrefix(lineMasked).length)) && inUnclosedFence(st, line)) return null;
  const targetLine = node?.kind === 'listItem' ? node.line : line;
  const own = node?.own.find((l) => l.view.line === targetLine);
  const view = own?.view ?? st.views[targetLine]!;
  const prefix = view.offset - st.lines.starts[targetLine]!;
  // Plain lines get `- [ ] ` after their quote markers (all nesting levels) and indentation;
  // this also covers lines that belong to no block's text, e.g. a blank `>` line in a callout.
  const from = own && node?.kind === 'quote' ? own.start : 0;
  const q = from + quotePrefix(view.text.slice(from)).length;
  const insertAt = q + indentOf(view.text.slice(q)).chars;
  // A plain line inside a multi-line `%%comment%%` is hidden: a task there would be invisible.
  if (!matchListMarker(maskInline(view.masked)) && insideComment(st.comments, view.offset + insertAt)) return null;
  return { st, node, line: targetLine, view, prefix, insertAt };
}

/**
 * Is blank `line` part of an unclosed fenced code block? Such a fence runs to the end of its
 * container (the parser leaves its trailing blank lines out of the block's range).
 */
function inUnclosedFence(st: Structure, line: number): boolean {
  // The deepest block ending last before `line` (pre-order: children after their parents).
  const prev: { node: Node | null; parent: Node | null } = { node: null, parent: null };
  preOrder(st.roots, (n, parent) => {
    if (n.endLine < line && (prev.node === null || n.endLine >= prev.node.endLine)) {
      prev.node = n;
      prev.parent = parent;
    }
  });
  const code = prev.node;
  return code !== null && code.kind === 'code' && !isClosedFence(code) && (prev.parent === null || prev.parent.endLine >= line);
}

/** Is document offset `pos` strictly inside one of the comment ranges? */
function insideComment(comments: readonly (readonly [number, number])[], pos: number): boolean {
  for (const [a, b] of comments) {
    if (a >= pos) return false;
    if (pos < b) return true;
  }
  return false;
}

/**
 * Index in the target's text where a `%%comment%%` that continues past the end of the line
 * starts (including the whitespace before it), or the text length. Tokens written after it
 * would be hidden in the comment, so edits only touch the text before it.
 */
function hiddenTailStart(t: Target): number {
  const text = t.view.text;
  const lineEnd = t.view.offset + text.length;
  for (const [a, b] of t.st.comments) {
    if (a >= lineEnd) break;
    if (a >= t.view.offset && b > lineEnd) {
      let k = a - t.view.offset;
      while (k > 0 && (text[k - 1] === ' ' || text[k - 1] === '\t')) k--;
      return k;
    }
  }
  return text.length;
}

/** Current task marker of a target line. */
function markerOf(t: Target): TaskMarker | null {
  const masked = maskInline(t.view.masked);
  const lm = matchListMarker(masked);
  return lm ? matchTaskMarker(t.view.text, masked, lm.contentStart) : null;
}

/** Append a token at the end of a line, before a trailing block id. */
function appendToken(text: string, token: string): string {
  const id = blockIdAtEnd(maskInline(text));
  if (id) {
    const before = text.slice(0, id.from).trimEnd();
    return `${before} ${token} ${text.slice(id.from).trim()}`;
  }
  return `${text.trimEnd()} ${token}`;
}

/**
 * Rewriting a line normalises its `🔁` rule to the canonical English form (SYNTAX §7), e.g.
 * "co tydzień w poniedziałek" -> "every week on Monday". Unparseable rules are kept.
 */
function canonicalRecurrenceEdit(t: Token): Edit | null {
  if (t.kind !== 'recurrence' || !t.canonical || t.canonical === t.value) return null;
  if (t.valueFrom === undefined || t.valueTo === undefined) return null;
  return { from: t.valueFrom, to: t.valueTo, insert: t.canonical };
}

/** Remove / add ✅ and ❌ dates for a status change (and canonicalise the rule). */
function updateCompletionDates(text: string, prev: TaskStatus | null, status: TaskStatus, today: LocalDate): string {
  const tokens = findTokens(maskInline(text));
  const edits: Edit[] = [];
  for (const t of tokens) {
    if (t.kind === 'done' && !(status === 'done' && prev === 'done')) edits.push(removal(text, t.from, t.to));
    if (t.kind === 'cancelled' && !(status === 'cancelled' && prev === 'cancelled')) edits.push(removal(text, t.from, t.to));
    const canonical = canonicalRecurrenceEdit(t);
    if (canonical) edits.push(canonical);
  }
  let out = applyEdits(text, edits);
  if (status === 'done' && prev !== 'done') out = appendToken(out, `${TOKENS.done} ${today}`);
  if (status === 'cancelled' && prev !== 'cancelled') out = appendToken(out, `${TOKENS.cancelled} ${today}`);
  return out;
}

/** Rewrite the marker of a line (or turn the line into a task) and update completion dates. */
function rewriteStatus(t: Target, marker: TaskMarker | null, status: TaskStatus, today: LocalDate): string {
  const text = t.view.text;
  let out: string;
  if (marker?.kind === 'checkbox') {
    out = text.slice(0, marker.from + 1) + checkboxCharFor(status, marker.marker) + text.slice(marker.from + 2);
  } else if (marker?.kind === 'keyword') {
    out = text.slice(0, marker.from) + keywordFor(status, marker.marker) + text.slice(marker.to);
  } else {
    const lm = matchListMarker(t.view.masked);
    const box = `[${STATUS_CHAR[status]}] `;
    if (lm) {
      const sep = lm.contentStart === lm.markerEnd ? ' ' : '';
      out = text.slice(0, lm.contentStart) + sep + box + text.slice(lm.contentStart);
    } else {
      const sep = t.insertAt > 0 && text[t.insertAt - 1] === '>' ? ' ' : '';
      out = text.slice(0, t.insertAt) + sep + '- ' + box + text.slice(t.insertAt);
    }
  }
  return updateCompletionDates(out, marker?.status ?? null, status, today);
}

/** How a recurring task repeats. */
interface Repeat {
  rule: RecurrenceRule;
  /** Logseq `++` repeater: move on until the date is after today (keeping the rhythm). */
  catchUp: boolean;
}

/** Recurrence rule of a task: `🔁` on its line, else a Logseq repeater on a planning line. */
function recurrenceRuleOf(lineTokens: Token[], planning: Token[]): Repeat | null {
  const rec = lineTokens.find((t) => t.kind === 'recurrence' && t.value);
  if (rec) {
    const rule = parseRecurrence(rec.value!);
    return rule ? { rule, catchUp: false } : null;
  }
  for (const p of planning) {
    if (p.repeater) {
      const rule = repeaterToRule(p.repeater);
      if (rule) return { rule, catchUp: p.repeater.startsWith('++') };
    }
  }
  return null;
}

/** Upper bound of catch-up steps (a daily `++1d` repeater ~27 years behind). */
const MAX_CATCH_UP = 10_000;

/** Next date of `repeat` after `from`; `++` repeaters skip occurrences up to and including today. */
function nextDate(repeat: Repeat, from: LocalDate | LocalDateTime, today: LocalDate): LocalDate | LocalDateTime {
  let next = nextOccurrence(repeat.rule, from, today);
  for (let k = 0; repeat.catchUp && next.slice(0, 10) <= today && k < MAX_CATCH_UP; k++) {
    const after = nextOccurrence(repeat.rule, next, today);
    if (after === next) break;
    next = after;
  }
  return next;
}

/** Planning lines (`SCHEDULED:` / `DEADLINE:`) of a list item, with their tokens. */
function planningLinesOf(t: Target): { line: number; text: string; tokens: Token[] }[] {
  if (t.node?.kind !== 'listItem') return [];
  const out: { line: number; text: string; tokens: Token[] }[] = [];
  for (const own of t.node.own.slice(1)) {
    if (own.role !== 'text') continue;
    const docText = t.st.lines.texts[own.view.line]!;
    const masked = maskInline(docText);
    if (!/^[ \t>]*(?:(?:SCHEDULED|DEADLINE):[ \t]*<[^>]*>[ \t]*)+$/.test(masked)) continue;
    out.push({ line: own.view.line, text: docText, tokens: findTokens(masked).filter((k) => k.kind === 'planning') });
  }
  return out;
}

/** Shift a date(-time) by `days`; null when out of range or malformed. */
function shiftValue(value: string, days: number): string | null {
  try {
    return toFileDateTime(addDaysLocal(value, days));
  } catch {
    return null;
  }
}

/** Shift the date inside a Logseq planning timestamp, updating an English weekday abbreviation. */
function shiftPlanning(text: string, tok: Token, days: number): Edit | null {
  if (tok.valueFrom === undefined || tok.valueTo === undefined || !tok.value) return null;
  const inner = text.slice(tok.valueFrom, tok.valueTo);
  const shifted = shiftValue(tok.value, days);
  if (!shifted) return null;
  let next = inner.replace(tok.value, shifted);
  const wd = EN_WEEKDAYS_SHORT[weekdayOf(shifted)] ?? '';
  next = next.replace(/^(\s*\d{4}-\d{2}-\d{2}\s+)(\p{L}+\.?)/u, (_m, head: string) => head + wd);
  return { from: tok.valueFrom, to: tok.valueTo, insert: next };
}

/**
 * Next occurrence of a recurring task (Obsidian Tasks semantics): the reference date (due,
 * else scheduled, else start, else the first reminder) moves to the rule's next date —
 * computed from today for `when done` rules — and every other date keeps its distance to it.
 * Returns the new lines (task line + copied planning lines) or null.
 */
function nextInstance(
  t: Target,
  text: string,
  repeat: Repeat,
  planning: { line: number; text: string; tokens: Token[] }[],
  today: LocalDate,
): string[] | null {
  const masked = maskInline(text);
  const lm = matchListMarker(masked);
  const marker = lm ? matchTaskMarker(text, masked, lm.contentStart) : null;
  const lineTokens = findTokens(masked);
  const first = (kind: Token['kind']): Token | undefined => lineTokens.find((k) => k.kind === kind && k.value);
  const plan = (field: 'due' | 'scheduled'): Token | undefined =>
    planning.flatMap((p) => p.tokens).find((k) => k.field === field && k.value);
  const ref = first('due') ?? plan('due') ?? first('scheduled') ?? plan('scheduled') ?? first('start') ?? first('reminder') ?? first('reminderCompat');
  let days = 0;
  if (ref?.value) {
    const refValue = joinDateTime(ref.value, ref.time);
    const next = nextDate(repeat, refValue, today);
    if (next === refValue) return null;
    try {
      days = diffDaysLocal(refValue, next);
    } catch {
      return null;
    }
  }
  const edits: Edit[] = [];
  if (marker?.kind === 'checkbox') edits.push({ from: marker.from + 1, to: marker.from + 2, insert: STATUS_CHAR.todo });
  else if (marker?.kind === 'keyword') edits.push({ from: marker.from, to: marker.to, insert: keywordFor('todo', marker.marker) });
  for (const k of lineTokens) {
    switch (k.kind) {
      case 'recurrence': {
        const e = canonicalRecurrenceEdit(k);
        if (e) edits.push(e);
        break;
      }
      case 'done':
      case 'cancelled':
        edits.push(removal(text, k.from, k.to));
        break;
      case 'created':
        edits.push({ from: k.valueFrom!, to: k.valueTo!, insert: today });
        break;
      case 'due':
      case 'scheduled':
      case 'start':
      case 'reminder':
      case 'reminderCompat': {
        const shifted = shiftValue(joinDateTime(k.value!, k.time), days);
        if (shifted === null) return null;
        edits.push({ from: k.valueFrom!, to: k.valueTo!, insert: shifted });
        break;
      }
      default:
        break;
    }
  }
  const id = blockIdAtEnd(masked);
  if (id) edits.push(removal(text, id.from, id.to));
  const docLine = t.st.lines.texts[t.line]!;
  const newFirst = docLine.slice(0, t.prefix) + applyEdits(text, edits).trimEnd();
  const out = [newFirst];
  for (const p of planning) {
    const pe: Edit[] = [];
    for (const tok of p.tokens) {
      const e = shiftPlanning(p.text, tok, days);
      if (e) pe.push(e);
    }
    out.push(applyEdits(p.text, pe));
  }
  return out;
}

/**
 * Set the status of the task at `line` (any line of a list item targets the item's first
 * line). Checkboxes get the status character, Logseq keywords the matching keyword. Becoming
 * done appends `✅ today`, becoming cancelled `❌ today`; leaving those states removes them.
 * Completing a recurring task (`🔁` or a Logseq repeater) inserts its next occurrence above
 * it (same indentation, status todo, dates shifted, no ✅/❌/block id) like Obsidian Tasks.
 * A non-task line becomes a task (`[ ] ` after its list marker, or a `- [ ] ` prefix).
 * Lines in code blocks / frontmatter and out-of-range lines leave the content unchanged.
 */
export function setTaskStatus(content: string, line: number, status: TaskStatus, today: LocalDate): string {
  const t = resolveTarget(content, line);
  return t ? applyStatus(content, t, markerOf(t), status, today) : content;
}

/** Rewrite the target's line for `status` (plus the next occurrence of a recurring task). */
function applyStatus(content: string, t: Target, marker: TaskMarker | null, status: TaskStatus, today: LocalDate): string {
  if (marker && marker.status === status) return content;
  // Edit only the visible part of the line; a comment running past its end is kept as is.
  const tailAt = hiddenTailStart(t);
  const tail = t.view.text.slice(tailAt);
  const vt: Target = tail ? { ...t, view: { ...t.view, text: t.view.text.slice(0, tailAt), masked: t.view.masked.slice(0, tailAt) } } : t;
  const visible = rewriteStatus(vt, marker, status, today);
  const glue = !tail || /[ \t]$/.test(visible) || /^[ \t]/.test(tail) ? '' : ' ';
  const docLine = t.st.lines.texts[t.line]!;
  const out = [docLine.slice(0, t.prefix) + visible + glue + tail];
  if (status === 'done' && marker?.status !== 'done') {
    const lineTokens = findTokens(maskInline(vt.view.text));
    const planning = planningLinesOf(t);
    const repeat = recurrenceRuleOf(lineTokens, planning.flatMap((p) => p.tokens));
    if (repeat) {
      // A line that only now became a task is copied in its converted form (without the comment).
      const inst = nextInstance(t, marker ? vt.view.text : visible, repeat, planning, today);
      if (inst) out.unshift(...inst);
    }
  }
  return replaceLines(content, t.st.lines, t.line, out);
}

/**
 * Toggle the task at `line`: todo / doing / deferred → done; done / cancelled → todo. A line
 * that is not a task becomes a todo task.
 */
export function toggleTask(content: string, line: number, today: LocalDate): string {
  const t = resolveTarget(content, line);
  if (!t) return content;
  const marker = markerOf(t);
  const next: TaskStatus = marker && marker.status !== 'done' && marker.status !== 'cancelled' ? 'done' : 'todo';
  return applyStatus(content, t, marker, next, today);
}

const ID_ALPHABET = 'abcdefghijklmnopqrstuvwxyz0123456789';

/** Random 6-character block id `[a-z0-9]` (crypto.getRandomValues when available, unbiased). */
export function generateBlockId(): string {
  const c = (globalThis as { crypto?: { getRandomValues?: (a: Uint8Array) => Uint8Array } }).crypto;
  let out = '';
  while (out.length < 6) {
    const buf = new Uint8Array(16);
    if (c?.getRandomValues) c.getRandomValues(buf);
    else for (let i = 0; i < buf.length; i++) buf[i] = Math.floor(Math.random() * 256);
    for (const b of buf) {
      // 252 = 7 * 36: rejecting larger bytes keeps the distribution uniform.
      if (b < 252 && out.length < 6) out += ID_ALPHABET[b % 36];
    }
  }
  return out;
}

/** Prefix of the container around a quote whose lines start with `prefix` (one `>` level less). */
function outerPrefix(prefix: string): string {
  const p = prefix.slice(0, prefix.lastIndexOf('>'));
  return p.includes('>') ? p : '';
}

/** Is the fenced code block `node` closed by a fence line (not by the end of its container)? */
function isClosedFence(node: Node): boolean {
  const fences = node.own.filter((l) => l.role === 'fence');
  return fences.length >= 2;
}

/**
 * Candidate documents with ` ^id` written for `block`, in order of preference (SYNTAX §4):
 * end of the first line for list items / headings / callouts, end of the last line for
 * paragraphs and quotes (the other end as a fallback), a separate `^id` line after code
 * blocks, tables, HTML, rules and quotes with nested blocks (Obsidian style).
 */
function* idPlacements(content: string, st: Structure, block: Block, id: string): Generator<string> {
  const lines = st.lines;
  const node = nodeAtLine(st.roots, block.line);
  const append = (ln: number): string => replaceLines(content, lines, ln, [`${lines.texts[ln]!.trimEnd()} ^${id}`]);
  /** `^id` on its own line after line `last` (optionally after a blank line), then a blank line before following text. */
  const idLineAfter = (last: number, prefix: string, blankBefore: boolean): string => {
    const blank = prefix.trimEnd();
    const out = [lines.texts[last]!];
    if (blankBefore) out.push(blank);
    out.push(`${prefix}^${id}`);
    const next = lines.texts[last + 1];
    if (next !== undefined && next.trim() !== '' && next.trim() !== prefix.trim()) out.push(blank);
    return replaceLines(content, lines, last, out);
  };
  // Container prefix (`> ` of an enclosing callout / quote) of the block's lines.
  const first = node?.own[0]?.view;
  const prefix = first ? lines.texts[first.line]!.slice(0, first.offset - lines.starts[first.line]!) : '';
  switch (block.kind) {
    case 'listItem':
      if (node?.own[0]?.role === 'fence') {
        // First line opens a code fence: the id goes on its own line after the item's content.
        const last = block.ownEndLine;
        const indent = ' '.repeat(Math.max(node.contentCol, 0));
        const docText = lines.texts[last]!;
        yield replaceLines(content, lines, last, [docText, `${quotePrefix(docText)}${indent}^${id}`]);
        return;
      }
      yield append(block.line);
      if (block.ownEndLine !== block.line) yield append(block.ownEndLine);
      return;
    case 'heading':
    case 'callout':
      yield append(block.line);
      return;
    case 'quote':
      if (block.children.length) {
        // A line right after the quote would be a lazy continuation of its last block.
        yield idLineAfter(block.endLine, outerPrefix(quotePrefix(lines.texts[block.line]!)), true);
        return;
      }
      yield append(block.ownEndLine);
      if (block.ownEndLine !== block.line) yield append(block.line);
      return;
    case 'paragraph':
      yield append(block.ownEndLine);
      if (block.ownEndLine !== block.line) yield append(block.line);
      return;
    case 'html':
      // HTML blocks of CommonMark types 6/7 only end at a blank line.
      yield idLineAfter(block.endLine, prefix, true);
      return;
    case 'code':
      // An id line after an unclosed fence would be code.
      if (!node || !isClosedFence(node)) return;
      yield idLineAfter(block.endLine, prefix, false);
      return;
    default:
      yield idLineAfter(block.endLine, prefix, false);
      yield idLineAfter(block.endLine, prefix, true);
  }
}

/**
 * Return the id of the block at `line`, adding one when missing: ` ^abc123` at the end of a
 * list item's / heading's / callout's first line or of a paragraph's / quote's last line; a
 * separate `^abc123` line after code blocks, tables, HTML, rules and quotes with nested
 * blocks (Obsidian style). Every placement is verified by re-parsing; when the other end of
 * the block works but the preferred one does not (e.g. it lies inside a `%%comment%%`), the
 * other end is used. Existing `^id` and Logseq `id::` ids are returned unchanged. For blank
 * lines, frontmatter and blocks that cannot carry an id (an unclosed code fence) the content
 * is returned unchanged with `id: ''`.
 */
export function ensureBlockId(content: string, line: number): { content: string; id: string } {
  const note = parseNote('', content);
  const block = blockAtLine(note, line);
  if (!block || block.kind === 'frontmatter') return { content, id: '' };
  if (block.id) return { content, id: block.id };
  let id = generateBlockId();
  while (Object.prototype.hasOwnProperty.call(note.blockIds, id)) id = generateBlockId();
  for (const candidate of idPlacements(content, parseStructure(content), block, id)) {
    const found = parseNote('', candidate).blockIds[id];
    if (found && found.line === block.line && found.kind === block.kind) return { content: candidate, id };
  }
  return { content, id: '' };
}

/**
 * Character range of the list item at `line` together with all its children (for moving,
 * folding and zooming in the editor): from the start of the item's first line to the end of
 * its last line (line terminator excluded). Null when `line` is not inside a list item.
 */
export function listItemRange(content: string, line: number): { from: number; to: number } | null {
  const st = parseStructure(content);
  if (!Number.isInteger(line) || line < 0 || line >= st.lines.texts.length) return null;
  let item: Node | null = null;
  let level: readonly Node[] = st.roots;
  for (;;) {
    const hit = level.find((n) => n.line <= line && line <= n.endLine);
    if (!hit) break;
    if (hit.kind === 'listItem') item = hit;
    level = hit.children;
  }
  if (!item) return null;
  const from = st.lines.starts[item.line]!;
  const to = st.lines.starts[item.endLine]! + st.lines.texts[item.endLine]!.length;
  return { from, to };
}
