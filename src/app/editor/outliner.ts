/**
 * Outliner editing (Logseq-like) on plain Markdown lists: list continuation, indent/outdent and
 * move with subtrees, task cycling, folding and block selection.
 *
 * Everything works on the document text (CodeMirror `Text`, 1-based line numbers) — no syntax
 * tree needed except to skip code blocks — so the commands are `StateCommand`s that can be
 * tested on plain strings with an `EditorState` (see outliner.test.ts).
 *
 * Outline model (docs/SYNTAX.md §4): a list item is a line starting with `-`, `*`, `+`, `1.` or
 * `1)` (after indentation). Its subtree is the item line plus all following lines indented
 * deeper than the item (blank lines inside included, trailing blank lines not). Indentation is
 * measured in columns with tab stops of 4.
 */
import { isolateHistory, moveLineDown, moveLineUp } from '@codemirror/commands';
import { foldAll, foldedRanges, foldService, indentUnit, syntaxTree, unfoldAll } from '@codemirror/language';
import {
  ChangeSet,
  EditorSelection,
  type ChangeSpec,
  type EditorState,
  type SelectionRange,
  type StateCommand,
  type Text,
  type TransactionSpec,
} from '@codemirror/state';
import type { Command } from '@codemirror/view';
import type { SyntaxNode } from '@lezer/common';
import { todayLocal } from '@/core/dates';
import { toggleTask } from '@/core/markdown';
import { editorNow } from './context';
import { minimalChange } from './diff';

export const TAB_WIDTH = 4;

// ---------------------------------------------------------------------------------------------
// Line model
// ---------------------------------------------------------------------------------------------

const LIST_RE = /^([ \t]*)([-*+]|(\d{1,9})([.)]))([ \t]+|$)/;
const CHECKBOX_RE = /^\[([ xX/\->])\](?:[ \t]+|$)/;
const HR_RE = /^[ \t]*([-*_])(?:[ \t]*\1){2,}[ \t]*$/;

export interface ListLine {
  /** Leading whitespace. */
  indent: string;
  /** Width of the indentation in columns. */
  indentCols: number;
  /** Marker as written: `-`, `*`, `+`, `3.`, `2)`. */
  marker: string;
  /** Number of an ordered item. */
  number: number | null;
  /** Delimiter of an ordered item. */
  delimiter: '.' | ')' | null;
  /** Offsets within the line. */
  markerFrom: number;
  markerTo: number;
  /** Status character inside `[ ]`, or null when there is no checkbox. */
  checkbox: string | null;
  /** Offset where the item text starts (after marker, checkbox and their spaces). */
  contentFrom: number;
}

/** Column width of `s.slice(0, upto)` with tab stops of {@link TAB_WIDTH}. */
export function columnsOf(s: string, upto = s.length): number {
  let col = 0;
  for (let i = 0; i < upto; i++) col = s.charCodeAt(i) === 9 ? col + TAB_WIDTH - (col % TAB_WIDTH) : col + 1;
  return col;
}

export function leadingWhitespace(s: string): string {
  return /^[ \t]*/.exec(s)![0];
}

export function indentColumns(s: string): number {
  return columnsOf(s, leadingWhitespace(s).length);
}

export function isBlank(s: string): boolean {
  return /^[ \t]*$/.test(s);
}

/** Parse a list item line; null for other lines (thematic breaks like `- - -` included). */
export function parseListLine(text: string): ListLine | null {
  if (HR_RE.test(text)) return null;
  const m = LIST_RE.exec(text);
  if (!m) return null;
  const indent = m[1]!;
  const markerFrom = indent.length;
  const marker = m[2]!;
  const markerTo = markerFrom + marker.length;
  let contentFrom = markerTo + m[5]!.length;
  let checkbox: string | null = null;
  if (m[5]) {
    const cb = CHECKBOX_RE.exec(text.slice(contentFrom));
    if (cb) {
      checkbox = cb[1]!;
      contentFrom += cb[0].length;
    }
  }
  return {
    indent,
    indentCols: columnsOf(text, markerFrom),
    marker,
    number: m[3] !== undefined ? Number(m[3]) : null,
    delimiter: (m[4] as '.' | ')' | undefined) ?? null,
    markerFrom,
    markerTo,
    checkbox,
    contentFrom,
  };
}

/** Column where children of this item start when indenting with spaces (after "- "). */
export function childColumn(text: string, item: ListLine): number {
  return columnsOf(text, item.markerTo) + 1;
}

/** Marker for the item after `item` (bullets repeat, numbers increment; tasks restart as todo). */
export function nextMarker(item: ListLine): string {
  const marker = item.number !== null ? `${item.number + 1}${item.delimiter}` : item.marker;
  return item.checkbox !== null ? `${marker} [ ] ` : `${marker} `;
}

// ---------------------------------------------------------------------------------------------
// Outline navigation on a document (1-based line numbers)
// ---------------------------------------------------------------------------------------------

/** Last line of the item at `lineNo` including its children. */
export function itemEndLine(doc: Text, lineNo: number): number {
  const base = indentColumns(doc.line(lineNo).text);
  let last = lineNo;
  for (let n = lineNo + 1; n <= doc.lines; n++) {
    const text = doc.line(n).text;
    if (isBlank(text)) continue;
    if (indentColumns(text) > base) last = n;
    else break;
  }
  return last;
}

/** The list item line that owns `lineNo` (the line itself, or the item a continuation line belongs to). */
export function itemAt(doc: Text, lineNo: number): number | null {
  const text = doc.line(lineNo).text;
  if (parseListLine(text)) return lineNo;
  if (isBlank(text)) return null;
  const cols = indentColumns(text);
  if (cols === 0) return null;
  for (let n = lineNo - 1; n >= 1; n--) {
    const t = doc.line(n).text;
    if (isBlank(t)) continue;
    const li = parseListLine(t);
    if (li && li.indentCols < cols) return itemEndLine(doc, n) >= lineNo ? n : null;
    if (indentColumns(t) === 0 && !li) return null;
  }
  return null;
}

/** Previous sibling item of the item at `lineNo` (same indentation, same parent). */
export function previousSibling(doc: Text, lineNo: number): number | null {
  const cols = indentColumns(doc.line(lineNo).text);
  for (let n = lineNo - 1; n >= 1; n--) {
    const t = doc.line(n).text;
    if (isBlank(t)) continue;
    const c = indentColumns(t);
    if (c > cols) continue;
    if (c === cols && parseListLine(t)) return n;
    return null;
  }
  return null;
}

/** Next sibling item of the item at `lineNo`. */
export function nextSibling(doc: Text, lineNo: number): number | null {
  const cols = indentColumns(doc.line(lineNo).text);
  for (let n = itemEndLine(doc, lineNo) + 1; n <= doc.lines; n++) {
    const t = doc.line(n).text;
    if (isBlank(t)) continue;
    return indentColumns(t) === cols && parseListLine(t) ? n : null;
  }
  return null;
}

/** Parent item of the item at `lineNo`. */
export function parentItem(doc: Text, lineNo: number): number | null {
  const cols = indentColumns(doc.line(lineNo).text);
  if (cols === 0) return null;
  for (let n = lineNo - 1; n >= 1; n--) {
    const t = doc.line(n).text;
    if (isBlank(t)) continue;
    if (indentColumns(t) >= cols) continue;
    if (parseListLine(t)) return n;
    // A continuation line of the parent: `- a` / `  text` / `    - child`.
    const owner = itemAt(doc, n);
    return owner !== null && indentColumns(doc.line(owner).text) < cols ? owner : null;
  }
  return null;
}

/**
 * Last line of the item's own text: the item line, or its last continuation line before the
 * first child item (`- a` / `  more text` / `  - child` → the `more text` line).
 */
export function itemTextEnd(doc: Text, lineNo: number): number {
  const end = itemEndLine(doc, lineNo);
  let last = lineNo;
  for (let n = lineNo + 1; n <= end; n++) {
    const t = doc.line(n).text;
    if (isBlank(t)) continue;
    if (parseListLine(t)) break;
    last = n;
  }
  return last;
}

/** First child item of the item at `lineNo` (null when it has none). */
export function firstChildItem(doc: Text, lineNo: number): number | null {
  const end = itemEndLine(doc, lineNo);
  for (let n = lineNo + 1; n <= end; n++) if (parseListLine(doc.line(n).text)) return n;
  return null;
}

/** Ancestor items of `lineNo`, outermost first. */
export function ancestorItems(doc: Text, lineNo: number): number[] {
  const out: number[] = [];
  for (let p = parentItem(doc, lineNo); p !== null; p = parentItem(doc, p)) out.unshift(p);
  return out;
}

/** Whether the document indents lists with tabs (default when nothing is indented yet). */
export function usesTabs(doc: Text, fallback = true): boolean {
  const max = Math.min(doc.lines, 5000);
  for (let n = 1; n <= max; n++) {
    const t = doc.line(n).text;
    const c = t.charCodeAt(0);
    if ((c === 9 || c === 32) && parseListLine(t)) return c === 9;
  }
  return fallback;
}

/** Whether the position is inside fenced code, frontmatter or a comment block. */
export function inCodeContext(state: EditorState, pos: number): boolean {
  for (let node: SyntaxNode | null = syntaxTree(state).resolveInner(pos, -1); node; node = node.parent) {
    const name = node.name;
    // Indented code ('CodeBlock') is ignored on purpose: deeply indented list items would
    // look like it to CommonMark, and the outliner treats indentation as nesting.
    if (name === 'FencedCode' || name === 'Frontmatter' || name === 'CommentBlock') return true;
  }
  return false;
}

/** Inside code, URLs or comments nothing is completed (autocomplete, slash menu). */
const NO_COMPLETE = new Set(['InlineCode', 'FencedCode', 'CodeText', 'CodeBlock', 'Frontmatter', 'URL', 'Comment', 'CommentBlock']);

export function inNoCompleteContext(state: EditorState, pos: number): boolean {
  for (let n: SyntaxNode | null = syntaxTree(state).resolveInner(pos, -1); n; n = n.parent) {
    if (NO_COMPLETE.has(n.name)) return true;
  }
  return false;
}

/** Line numbers touched by a selection range (a range ending at a line start excludes that line). */
function rangeLines(doc: Text, r: SelectionRange): [number, number] {
  const first = doc.lineAt(r.from).number;
  let last = doc.lineAt(r.to).number;
  if (last > first && doc.line(last).from === r.to) last--;
  return [first, last];
}

/** Outermost items touched by the selection (descendants of other touched items dropped). */
export function topItems(state: EditorState): number[] {
  const doc = state.doc;
  const candidates = new Set<number>();
  for (const r of state.selection.ranges) {
    const [first, last] = rangeLines(doc, r);
    const owner = itemAt(doc, first);
    if (owner !== null) candidates.add(owner);
    for (let n = first + 1; n <= last; n++) if (parseListLine(doc.line(n).text)) candidates.add(n);
  }
  const sorted = [...candidates].sort((a, b) => a - b);
  const tops: number[] = [];
  let coveredTo = 0;
  for (const n of sorted) {
    if (n <= coveredTo) continue;
    tops.push(n);
    coveredTo = itemEndLine(doc, n);
  }
  return tops;
}

// ---------------------------------------------------------------------------------------------
// Indent / outdent
// ---------------------------------------------------------------------------------------------

function wantsTabs(state: EditorState): boolean {
  return usesTabs(state.doc, state.facet(indentUnit).includes('\t'));
}

/**
 * Change removing `cols` columns of leading whitespace from a line. The outer part of the
 * indentation (the parent's prefix) is kept: "\t    - x" minus 4 columns is "\t- x".
 */
function dedentLine(doc: Text, lineNo: number, cols: number): { from: number; to: number; insert: string } | null {
  const line = doc.line(lineNo);
  const ws = leadingWhitespace(line.text);
  if (ws.length === 0) return null;
  const target = Math.max(0, columnsOf(ws) - cols);
  let i = 0;
  let col = 0;
  while (i < ws.length) {
    const next = ws.charCodeAt(i) === 9 ? col + TAB_WIDTH - (col % TAB_WIDTH) : col + 1;
    if (next > target) break;
    col = next;
    i++;
  }
  const insert = ' '.repeat(target - col);
  return { from: line.from + i, to: line.from + ws.length, insert };
}

/** Structural edits (indent, move, task toggle) are one undo step each, never merged. */
const ownUndoStep = isolateHistory.of('full');

/**
 * Transaction for structural `changes` plus `after(newDoc)`: follow-up changes computed on the
 * changed document (renumbering ordered lists). Composed into one change, one undo step.
 */
function structuralSpec(state: EditorState, changes: ChangeSpec[], userEvent: string, after?: (doc: Text) => ChangeSpec[]): TransactionSpec {
  let set = state.changes(changes);
  if (after) {
    const doc = set.apply(state.doc);
    const more = after(doc);
    if (more.length) set = set.compose(ChangeSet.of(more, doc.length));
  }
  return { changes: set, selection: state.selection.map(set, 1), userEvent, scrollIntoView: true, annotations: ownUndoStep };
}

/** Number the ordered item at `lineNo` `start` and its following ordered siblings consecutively. */
function renumberFrom(doc: Text, lineNo: number, start: number, visited?: Set<number>): { from: number; to: number; insert: string }[] {
  const changes: { from: number; to: number; insert: string }[] = [];
  let expected = start;
  for (let n: number | null = lineNo; n !== null; n = nextSibling(doc, n)) {
    const line = doc.line(n);
    const li = parseListLine(line.text);
    if (!li || li.number === null) break;
    visited?.add(n);
    if (li.number !== expected) changes.push({ from: line.from + li.markerFrom, to: line.from + li.markerTo, insert: `${expected}${li.delimiter}` });
    expected++;
  }
  return changes;
}

/**
 * Renumber the ordered list an item now belongs to, from the item on: after its previous
 * sibling's number, or from 1 when it is the first item (a new nested list).
 */
function renumberAt(doc: Text, lineNo: number, visited?: Set<number>): { from: number; to: number; insert: string }[] {
  if (parseListLine(doc.line(lineNo).text)?.number == null) return [];
  const prev = previousSibling(doc, lineNo);
  if (prev === null) return renumberFrom(doc, lineNo, 1, visited);
  const n = parseListLine(doc.line(prev).text)?.number;
  return n == null ? [] : renumberFrom(doc, prev, n, visited);
}

/** Renumber the ordered siblings following an item from its own number on. */
function renumberAfter(doc: Text, lineNo: number, visited?: Set<number>): { from: number; to: number; insert: string }[] {
  const n = parseListLine(doc.line(lineNo).text)?.number;
  return n == null ? [] : renumberFrom(doc, lineNo, n, visited);
}

/**
 * Renumbering after a structural edit: `moved` items continue the list they now belong to
 * (see renumberAt), the lists they left close the gap after `stayed` items (whose own number
 * is kept, so a list starting at 5 still does). Each change once.
 */
function renumberLines(doc: Text, moved: number[], stayed: number[]): ChangeSpec[] {
  const byFrom = new Map<number, { from: number; to: number; insert: string }>();
  // A list renumbered once (several moved items in it) is not renumbered again.
  const visited = new Set<number>();
  const add = (changes: { from: number; to: number; insert: string }[]) => {
    for (const c of changes) if (!byFrom.has(c.from)) byFrom.set(c.from, c);
  };
  for (const n of moved) if (n >= 1 && n <= doc.lines && !visited.has(n)) add(renumberAt(doc, n, visited));
  for (const n of stayed) if (n >= 1 && n <= doc.lines && !visited.has(n)) add(renumberAfter(doc, n, visited));
  return [...byFrom.values()].sort((a, b) => a.from - b.from);
}

/**
 * Indent the selected items with their subtrees under their previous sibling. Null when the
 * selection is not in a list; an empty spec when the first item cannot be indented. Ordered
 * lists are renumbered: the item continues its new parent's children (or starts at 1) and the
 * siblings it left close the gap.
 */
export function indentItemsSpec(state: EditorState): TransactionSpec | null {
  const doc = state.doc;
  const tops = topItems(state);
  if (tops.length === 0) return null;
  if (previousSibling(doc, tops[0]!) === null) return {};
  const tabs = wantsTabs(state);
  const changes: ChangeSpec[] = [];
  const moved: number[] = [];
  const stayed: number[] = [];
  for (const top of tops) {
    const prev = previousSibling(doc, top);
    if (prev === null) continue;
    const prevText = doc.line(prev).text;
    const delta = tabs
      ? '\t'
      : ' '.repeat(Math.max(1, childColumn(prevText, parseListLine(prevText)!) - indentColumns(doc.line(top).text)));
    const end = itemEndLine(doc, top);
    for (let n = top; n <= end; n++) {
      const line = doc.line(n);
      if (isBlank(line.text)) continue;
      // A tab at column 0 shifts everything by one tab stop; spaces go after the existing
      // indentation, because spaces in front of a tab are swallowed by its tab stop.
      const at = tabs ? line.from : line.from + leadingWhitespace(line.text).length;
      changes.push({ from: at, insert: delta });
    }
    // Line numbers don't change: the new list from the moved item, the old one from `prev`.
    moved.push(top);
    stayed.push(prev);
  }
  return structuralSpec(state, changes, 'input.indent', (d) => renumberLines(d, moved, stayed));
}

/**
 * Outdent the selected items with their subtrees to their parent's level. Ordered lists are
 * renumbered: the item follows its former parent, the siblings after it (now its children)
 * continue its own children's numbering.
 */
export function outdentItemsSpec(state: EditorState): TransactionSpec | null {
  const doc = state.doc;
  const tops = topItems(state);
  if (tops.length === 0) return null;
  const tabs = wantsTabs(state);
  const changes: ChangeSpec[] = [];
  const moved: number[] = [];
  for (const top of tops) {
    const cols = indentColumns(doc.line(top).text);
    if (cols === 0) continue;
    const parent = parentItem(doc, top);
    const target = parent !== null ? indentColumns(doc.line(parent).text) : Math.max(0, cols - (tabs ? TAB_WIDTH : 2));
    const remove = cols - target;
    const end = itemEndLine(doc, top);
    for (let n = top; n <= end; n++) {
      if (isBlank(doc.line(n).text)) continue;
      const ch = dedentLine(doc, n, remove);
      if (ch) changes.push(ch);
    }
    // The item now follows its former parent; its former next siblings became its children.
    moved.push(top);
    const adopted = nextSibling(doc, top);
    if (adopted !== null) moved.push(adopted);
  }
  return structuralSpec(state, changes, 'delete.dedent', (d) => renumberLines(d, moved, []));
}

function run(spec: (s: EditorState) => TransactionSpec | null): StateCommand {
  return ({ state, dispatch }) => {
    if (state.readOnly) return false;
    if (inCodeContext(state, state.selection.main.head)) return false;
    const tr = spec(state);
    if (!tr) return false;
    if (tr.changes || tr.selection) dispatch(state.update(tr));
    return true;
  };
}

export const indentItems: StateCommand = run(indentItemsSpec);
export const outdentItems: StateCommand = run(outdentItemsSpec);

// ---------------------------------------------------------------------------------------------
// Enter / Shift-Enter / Backspace
// ---------------------------------------------------------------------------------------------

/** Renumber ordered siblings following `lineNo` (whose number becomes `startNumber`). */
function renumberChanges(doc: Text, lineNo: number, startNumber: number): ChangeSpec[] {
  const changes: ChangeSpec[] = [];
  let expected = startNumber;
  for (let n = nextSibling(doc, lineNo); n !== null; n = nextSibling(doc, n)) {
    const line = doc.line(n);
    const li = parseListLine(line.text);
    if (!li || li.number === null) break;
    expected++;
    if (li.number === expected) break;
    changes.push({ from: line.from + li.markerFrom, to: line.from + li.markerTo, insert: `${expected}${li.delimiter}` });
  }
  return changes;
}

type RangeResult = { changes: ChangeSpec[]; range: SelectionRange };

/** Whether a fold starts at `pos` (the end of a folded item's first line). */
function isFoldedAt(state: EditorState, pos: number): boolean {
  let folded = false;
  foldedRanges(state).between(pos, pos, (from) => {
    if (from === pos) folded = true;
  });
  return folded;
}

/**
 * A new item after the text of the item at `itemLine` (its line and continuation lines): its
 * first child when it has child items (and `afterSubtree` is false), else its next sibling
 * after the whole subtree. `del` (trailing blanks at the cursor) is removed.
 */
function newItemAfterText(doc: Text, itemLine: number, del: { from: number; to: number }, afterSubtree: boolean): RangeResult {
  const item = parseListLine(doc.line(itemLine).text)!;
  const removed = del.to - del.from;
  const changes: ChangeSpec[] = [];
  if (removed > 0) changes.push({ from: del.from, to: del.to });
  const child = afterSubtree ? null : firstChildItem(doc, itemLine);
  if (child !== null) {
    const childLine = doc.line(child);
    const childItem = parseListLine(childLine.text)!;
    const marker = childItem.number !== null ? `${childItem.number}${childItem.delimiter}` : childItem.marker;
    const insert = `\n${childItem.indent}${marker} ${childItem.checkbox !== null ? '[ ] ' : ''}`;
    const at = doc.line(itemTextEnd(doc, itemLine)).to;
    changes.push({ from: at, insert });
    if (childItem.number !== null) {
      changes.push({
        from: childLine.from + childItem.markerFrom,
        to: childLine.from + childItem.markerTo,
        insert: `${childItem.number + 1}${childItem.delimiter}`,
      });
      changes.push(...renumberChanges(doc, child, childItem.number + 1));
    }
    return { changes, range: EditorSelection.cursor(at - (del.from < at ? removed : 0) + insert.length) };
  }
  const endTo = doc.line(itemEndLine(doc, itemLine)).to;
  const insert = `\n${item.indent}${nextMarker(item)}`;
  changes.push({ from: endTo, insert });
  if (item.number !== null) changes.push(...renumberChanges(doc, itemLine, item.number + 1));
  return { changes, range: EditorSelection.cursor(endTo - removed + insert.length) };
}

/**
 * Enter at the end of an item's last continuation line (`- a` / `  more text|`): a new item, as
 * at the end of a one-line item. Elsewhere in continuation lines (and in quotes or tables
 * inside items) Enter stays a line break in the item.
 */
function enterInContinuation(state: EditorState, range: SelectionRange): RangeResult | null {
  const doc = state.doc;
  const line = doc.lineAt(range.head);
  if (!range.empty || isBlank(line.text) || /^[ \t]*[>|]/.test(line.text)) return null;
  if (!isBlank(line.text.slice(range.head - line.from))) return null;
  const owner = itemAt(doc, line.number);
  if (owner === null || owner === line.number || itemTextEnd(doc, owner) !== line.number) return null;
  return newItemAfterText(doc, owner, { from: range.head, to: line.to }, false);
}

function enterInItem(state: EditorState, range: SelectionRange): RangeResult | null {
  const doc = state.doc;
  const line = doc.lineAt(range.from);
  if (range.to > line.to) return null;
  const item = parseListLine(line.text);
  if (!item) return enterInContinuation(state, range);
  const fromInLine = range.from - line.from;
  if (fromInLine <= item.markerFrom) return null;
  const cut = Math.max(fromInLine, item.contentFrom);
  const toInLine = Math.max(range.to - line.from, cut);
  const content = line.text.slice(item.contentFrom);

  // Enter on an empty item: outdent it, or end the list at the top level.
  if (isBlank(content)) {
    if (item.indentCols > 0) {
      const parent = parentItem(doc, line.number);
      const target = parent !== null ? indentColumns(doc.line(parent).text) : 0;
      const changes: ChangeSpec[] = [];
      const end = itemEndLine(doc, line.number);
      let removedOnLine = 0;
      for (let n = line.number; n <= end; n++) {
        if (isBlank(doc.line(n).text)) continue;
        const ch = dedentLine(doc, n, item.indentCols - target);
        if (!ch) continue;
        changes.push(ch);
        if (n === line.number) removedOnLine = ch.to - ch.from - ch.insert.length;
      }
      return { changes, range: EditorSelection.cursor(line.to - removedOnLine) };
    }
    return { changes: [{ from: line.from, to: line.to, insert: '' }], range: EditorSelection.cursor(line.from) };
  }

  // At the start of the text: an empty item goes above; this one keeps its text, checkbox
  // state, id and children (splitting would turn a done task into a new open one).
  if (range.empty && fromInLine <= item.contentFrom) {
    const marker = item.number !== null ? `${item.number}${item.delimiter}` : item.marker;
    const above = `${item.indent}${marker} ${item.checkbox !== null ? '[ ] ' : ''}\n`;
    const changes: ChangeSpec[] = [{ from: line.from, insert: above }];
    let shift = 0;
    if (item.number !== null) {
      const own = `${item.number + 1}${item.delimiter}`;
      changes.push({ from: line.from + item.markerFrom, to: line.from + item.markerTo, insert: own });
      shift = own.length - (item.markerTo - item.markerFrom);
      changes.push(...renumberChanges(doc, line.number, item.number + 1));
    }
    return { changes, range: EditorSelection.cursor(line.from + above.length + item.contentFrom + shift) };
  }

  const after = line.text.slice(toInLine).replace(/^[ \t]+/, '');
  const removeTo = line.from + line.text.length - after.length;
  const end = itemEndLine(doc, line.number);

  // At the end of the item's line, with children or continuation lines below: the new item
  // goes after its text (as the first child), or after its subtree when it is folded — never
  // between the item and its continuation lines, which would hand them to the new item.
  if (after === '' && end > line.number) {
    return newItemAfterText(doc, line.number, { from: line.from + cut, to: removeTo }, isFoldedAt(state, line.to));
  }

  // Split the item: the text after the cursor moves to a new sibling.
  const prefix = item.indent + nextMarker(item);
  const insert = `\n${prefix}`;
  const changes: ChangeSpec[] = [{ from: line.from + cut, to: removeTo, insert }];
  if (item.number !== null) changes.push(...renumberChanges(doc, line.number, item.number + 1));
  return { changes, range: EditorSelection.cursor(line.from + cut + insert.length) };
}

/** Enter in a list item: continue the list (see module doc). False outside lists. */
export const insertNewlineContinueList: StateCommand = ({ state, dispatch }) => {
  if (state.readOnly) return false;
  let ok = true;
  const results: RangeResult[] = [];
  for (const r of state.selection.ranges) {
    const res = inCodeContext(state, r.head) ? null : enterInItem(state, r);
    if (!res) {
      ok = false;
      break;
    }
    results.push(res);
  }
  if (!ok) return false;
  let i = 0;
  const tr = state.changeByRange(() => results[i++]!);
  dispatch(state.update(tr, { scrollIntoView: true, userEvent: 'input' }));
  return true;
};

/** Shift-Enter in a list item: a line break inside the item, aligned with its text. */
export const insertSoftBreak: StateCommand = ({ state, dispatch }) => {
  if (state.readOnly) return false;
  const doc = state.doc;
  const owner = itemAt(doc, doc.lineAt(state.selection.main.head).number);
  if (owner === null || inCodeContext(state, state.selection.main.head)) return false;
  const text = doc.line(owner).text;
  const item = parseListLine(text)!;
  const pad = item.indent + ' '.repeat(columnsOf(text, item.markerTo) + 1 - item.indentCols);
  dispatch(state.update(state.replaceSelection(`\n${pad}`), { scrollIntoView: true, userEvent: 'input' }));
  return true;
};

/**
 * Backspace right after a list marker: on an empty item the marker is removed (the line
 * becomes empty); with a checkbox the checkbox goes first. False otherwise.
 */
export const deleteListMarker: StateCommand = ({ state, dispatch }) => {
  if (state.readOnly) return false;
  const changes: ChangeSpec[] = [];
  for (const r of state.selection.ranges) {
    if (!r.empty) return false;
    const line = state.doc.lineAt(r.head);
    const item = parseListLine(line.text);
    if (!item) return false;
    const empty = isBlank(line.text.slice(item.contentFrom));
    if (r.head - line.from !== item.contentFrom && !(empty && r.head === line.to)) return false;
    if (inCodeContext(state, r.head)) return false;
    if (item.checkbox !== null) {
      // `- [ ] |` → `- |`: the checkbox goes first, the marker with the next Backspace.
      const cbFrom = line.text.indexOf('[', item.markerTo);
      changes.push({ from: line.from + cbFrom, to: empty ? line.to : line.from + item.contentFrom });
    } else if (empty) changes.push({ from: line.from, to: line.to, insert: '' });
    else return false;
  }
  dispatch(state.update({ changes, userEvent: 'delete.backward', scrollIntoView: true }));
  return true;
};

// ---------------------------------------------------------------------------------------------
// Move items
// ---------------------------------------------------------------------------------------------

/** Replace the number of an ordered item's first line. */
function withNumber(blockText: string, number: number | null): string {
  if (number === null) return blockText;
  const nl = blockText.indexOf('\n');
  const first = nl < 0 ? blockText : blockText.slice(0, nl);
  const li = parseListLine(first);
  if (!li || li.number === null) return blockText;
  return first.slice(0, li.markerFrom) + `${number}${li.delimiter}` + first.slice(li.markerTo) + (nl < 0 ? '' : blockText.slice(nl));
}

/** Move the item(s) at the selection with their subtrees past the previous (-1) or next (1) sibling. */
export function moveItemsSpec(state: EditorState, dir: -1 | 1): TransactionSpec | null {
  const doc = state.doc;
  const main = state.selection.main;
  const [firstLine, lastLine] = rangeLines(doc, main);
  const first = itemAt(doc, firstLine);
  if (first === null) return null;
  let last = first;
  while (itemEndLine(doc, last) < lastLine) {
    const ns = nextSibling(doc, last);
    if (ns === null) break;
    last = ns;
  }
  const blockFrom = doc.line(first).from;
  const blockTo = doc.line(itemEndLine(doc, last)).to;
  const block = doc.sliceString(blockFrom, blockTo);
  const ownNumber = parseListLine(doc.line(first).text)?.number ?? null;
  if (dir < 0) {
    const prev = previousSibling(doc, first);
    if (prev === null) return {};
    const prevFrom = doc.line(prev).from;
    const prevTo = doc.line(itemEndLine(doc, prev)).to;
    const prevNumber = parseListLine(doc.line(prev).text)?.number ?? null;
    const lastNumber = parseListLine(doc.line(last).text)?.number ?? null;
    const gap = doc.sliceString(prevTo, blockFrom);
    // Numbers stay in place: the moved block starts with the previous item's number.
    const movedBlock = ownNumber !== null && prevNumber !== null ? renumberBlock(block, prevNumber) : block;
    const prevBlock = withNumber(doc.sliceString(prevFrom, prevTo), lastNumber);
    const insert = movedBlock + gap + prevBlock;
    return {
      changes: { from: prevFrom, to: blockTo, insert },
      selection: movedSelection(state, blockFrom, blockTo, block, movedBlock, prevFrom),
      scrollIntoView: true,
      userEvent: 'move.line',
      annotations: ownUndoStep,
    };
  }
  const next = nextSibling(doc, last);
  if (next === null) return {};
  const nextFrom = doc.line(next).from;
  const nextTo = doc.line(itemEndLine(doc, next)).to;
  const nextNumber = parseListLine(doc.line(next).text)?.number ?? null;
  const gap = doc.sliceString(blockTo, nextFrom);
  const nextBlock = withNumber(doc.sliceString(nextFrom, nextTo), ownNumber);
  const lastNumberAfter = nextNumber;
  const movedBlock = ownNumber !== null && lastNumberAfter !== null ? renumberBlock(block, ownNumber + 1) : block;
  const insert = nextBlock + gap + movedBlock;
  const offset = nextBlock.length + gap.length;
  return {
    changes: { from: blockFrom, to: nextTo, insert },
    selection: movedSelection(state, blockFrom, blockTo, block, movedBlock, blockFrom + offset),
    scrollIntoView: true,
    userEvent: 'move.line',
    annotations: ownUndoStep,
  };
}

/** Renumber the top-level ordered items of a moved block (siblings at its first line's indent). */
function renumberBlock(block: string, start: number): string {
  const lines = block.split('\n');
  const base = parseListLine(lines[0] ?? '');
  if (!base || base.number === null) return block;
  let n = start;
  return lines
    .map((l) => {
      const li = parseListLine(l);
      if (!li || li.number === null || li.indentCols !== base.indentCols) return l;
      return l.slice(0, li.markerFrom) + `${n++}${li.delimiter}` + l.slice(li.markerTo);
    })
    .join('\n');
}

/**
 * Selection after moving `block` (at [from, to]) to `newFrom` as `moved` (same lines, numbers
 * possibly renumbered): positions keep their line and their place in the text, so a marker
 * growing from `9.` to `10.` does not shift the cursor within the item.
 */
function movedSelection(state: EditorState, from: number, to: number, block: string, moved: string, newFrom: number): EditorSelection {
  const oldLines = block.split('\n');
  const newLines = moved.split('\n');
  const map = (p: number): number => {
    if (p < from || p > to) return p;
    let lineStart = from;
    let newStart = newFrom;
    for (let i = 0; i < oldLines.length; i++) {
      const oldLine = oldLines[i]!;
      const newLen = newLines[i]?.length ?? oldLine.length;
      if (p <= lineStart + oldLine.length) {
        const col = p - lineStart;
        const li = parseListLine(oldLine);
        const shifted = li && col <= li.markerFrom ? col : col + newLen - oldLine.length;
        return newStart + Math.max(0, Math.min(newLen, shifted));
      }
      lineStart += oldLine.length + 1;
      newStart += newLen + 1;
    }
    return newStart;
  };
  return EditorSelection.create(
    state.selection.ranges.map((r) => EditorSelection.range(map(r.anchor), map(r.head))),
    state.selection.mainIndex,
  );
}

function moveCommand(dir: -1 | 1): StateCommand {
  return (target) => {
    const { state, dispatch } = target;
    if (state.readOnly) return false;
    const spec = inCodeContext(state, state.selection.main.head) ? null : moveItemsSpec(state, dir);
    if (spec === null) return dir < 0 ? moveLineUp(target) : moveLineDown(target);
    if (spec.changes) dispatch(state.update(spec));
    return true;
  };
}

export const moveItemUp: StateCommand = moveCommand(-1);
export const moveItemDown: StateCommand = moveCommand(1);

// ---------------------------------------------------------------------------------------------
// Tasks
// ---------------------------------------------------------------------------------------------

/**
 * Cycle the task state of the selected items: a plain line/item becomes `- [ ]`, todo/doing
 * become done (✅ date, recurring tasks get their next occurrence above), done/cancelled become
 * todo again (via `toggleTask` from @/core/markdown).
 */
export const cycleTask: StateCommand = ({ state, dispatch }) => {
  if (state.readOnly) return false;
  const doc = state.doc;
  const lines = new Set<number>();
  for (const r of state.selection.ranges) {
    const [first, last] = rangeLines(doc, r);
    for (let n = first; n <= last; n++) {
      const owner = itemAt(doc, n);
      if (owner !== null) lines.add(owner);
      else if (!isBlank(doc.line(n).text) || r.empty) lines.add(n);
    }
  }
  const today = todayLocal(editorNow(state));
  let text = doc.toString();
  let changes = ChangeSet.empty(text.length);
  // Bottom-up, so inserted recurrence lines don't shift the remaining line numbers.
  for (const n of [...lines].sort((a, b) => b - a)) {
    if (inCodeContext(state, doc.line(n).from)) continue;
    const next = toggleTask(text, n - 1, today);
    const ch = minimalChange(text, next);
    if (!ch) continue;
    changes = changes.compose(ChangeSet.of([ch], text.length));
    text = next;
  }
  if (!changes.empty) {
    dispatch(
      state.update({ changes, selection: state.selection.map(changes, 1), userEvent: 'input.task', scrollIntoView: true, annotations: ownUndoStep }),
    );
  }
  return true;
};

// ---------------------------------------------------------------------------------------------
// Block selection
// ---------------------------------------------------------------------------------------------

/** Select the item (with children) at the cursor; again → its parent. Paragraphs outside lists. */
export const selectBlock: StateCommand = ({ state, dispatch }) => {
  const doc = state.doc;
  const main = state.selection.main;
  const lineNo = doc.lineAt(main.empty ? main.head : main.from).number;
  let from: number;
  let to: number;
  const item = itemAt(doc, lineNo);
  if (item !== null) {
    from = doc.line(item).from;
    to = doc.line(itemEndLine(doc, item)).to;
    if (main.from === from && main.to === to) {
      const parent = parentItem(doc, item);
      if (parent !== null) {
        from = doc.line(parent).from;
        to = doc.line(itemEndLine(doc, parent)).to;
      }
    }
  } else {
    if (isBlank(doc.line(lineNo).text)) return false;
    let a = lineNo;
    let b = lineNo;
    while (a > 1 && !isBlank(doc.line(a - 1).text)) a--;
    while (b < doc.lines && !isBlank(doc.line(b + 1).text)) b++;
    from = doc.line(a).from;
    to = doc.line(b).to;
  }
  dispatch(state.update({ selection: EditorSelection.range(from, to), scrollIntoView: true, userEvent: 'select' }));
  return true;
};

// ---------------------------------------------------------------------------------------------
// Folding
// ---------------------------------------------------------------------------------------------

const HEADING_RE = /^ {0,3}(#{1,6})(?:[ \t]|$)/;

/** Foldable range for a line (list item with children, or heading section), pure on text. */
export function outlineFoldRange(doc: Text, lineNo: number, isCode?: (lineNo: number) => boolean): { from: number; to: number } | null {
  const line = doc.line(lineNo);
  if (parseListLine(line.text)) {
    const end = itemEndLine(doc, lineNo);
    return end > lineNo ? { from: line.to, to: doc.line(end).to } : null;
  }
  const h = HEADING_RE.exec(line.text);
  if (!h) return null;
  const level = h[1]!.length;
  let last = lineNo;
  for (let n = lineNo + 1; n <= doc.lines; n++) {
    const t = doc.line(n).text;
    const hh = HEADING_RE.exec(t);
    if (hh && hh[1]!.length <= level && !isCode?.(n)) break;
    if (!isBlank(t)) last = n;
  }
  return last > lineNo ? { from: line.to, to: doc.line(last).to } : null;
}

/** Fold list items with children and heading sections. */
export const outlineFolding = foldService.of((state, lineStart) => {
  if (inCodeContext(state, lineStart)) return null;
  const doc = state.doc;
  return outlineFoldRange(doc, doc.lineAt(lineStart).number, (n) => inCodeContext(state, doc.line(n).from));
});

/** Fold everything, or unfold everything when something is folded. */
export const toggleFoldAll: Command = (view) => {
  if (foldedRanges(view.state).size > 0) return unfoldAll(view);
  return foldAll(view);
};
