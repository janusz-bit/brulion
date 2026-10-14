/**
 * Outline geometry for the visible lines: which lines are list items (with their nesting
 * depth, task marker and children) and which are continuation lines of an item (soft breaks,
 * properties, code inside an item). Pure functions on CodeMirror `Text` (1-based line
 * numbers), following the outliner's model in ../outliner.ts — tested in lists.test.ts.
 *
 * Depth is the number of ancestor items, computed with an indentation stack seeded from the
 * nearest top-level line above the range, so it costs O(lines scanned), not O(lines²).
 */
import type { Text } from '@codemirror/state';
import { LOGSEQ_KEYWORDS, statusFromCheckbox } from '@/core/markdown';
import type { TaskStatus } from '@/core/types';
import { ancestorItems, columnsOf, indentColumns, isBlank, itemEndLine, parseListLine, TAB_WIDTH, type ListLine } from '../outliner';

export interface TaskMarkerInfo {
  kind: 'checkbox' | 'keyword';
  status: TaskStatus;
  /** Checkbox character or keyword as written. */
  marker: string;
  /** Offsets within the line: the marker (`[x]` / `DONE`) and the end of the following blanks. */
  from: number;
  to: number;
  end: number;
}

export interface ItemInfo {
  kind: 'item';
  /** 1-based line number. */
  line: number;
  depth: number;
  item: ListLine;
  task: TaskMarkerInfo | null;
  /** The item has deeper-indented lines below it (it can be folded). */
  hasChildren: boolean;
}

export interface ContinuationInfo {
  kind: 'cont';
  line: number;
  /** Depth of the owning item. */
  depth: number;
  /** Line of the owning item. */
  owner: number;
  /** Characters of leading whitespace belonging to the outline (hidden by the preview). */
  wsLength: number;
  /** The owner's subtree continues below this line (draw its guide). */
  ownerContinues: boolean;
}

export type OutlineLine = ItemInfo | ContinuationInfo;

const KEYWORD_RE = /^(TODO|LATER|DOING|NOW|IN-PROGRESS|DONE|CANCELED|CANCELLED|WAITING|WAIT)(?=[ \t]|$)/;

/** Task marker of a list item line (checkbox or Logseq keyword), offsets within the line. */
export function taskMarker(text: string, item: ListLine): TaskMarkerInfo | null {
  if (item.checkbox !== null) {
    const from = text.indexOf('[', item.markerTo);
    return { kind: 'checkbox', status: statusFromCheckbox(item.checkbox), marker: item.checkbox, from, to: from + 3, end: item.contentFrom };
  }
  const rest = text.slice(item.contentFrom);
  const m = KEYWORD_RE.exec(rest);
  if (!m) return null;
  const from = item.contentFrom;
  const to = from + m[1]!.length;
  let end = to;
  while (end < text.length && (text[end] === ' ' || text[end] === '\t')) end++;
  return { kind: 'keyword', status: LOGSEQ_KEYWORDS[m[1]!] ?? 'todo', marker: m[1]!, from, to, end };
}

interface StackEntry {
  cols: number;
  line: number;
  depth: number;
  text: string;
  item: ListLine;
}

/** How far back to look for the top of the outline containing the first line. */
const MAX_SCAN_BACK = 2000;

/**
 * Outline info for lines `fromLine..toLine` (1-based, inclusive). `isCode(n)` tells which lines
 * are inside fenced code / comments / frontmatter (never list items there).
 */
export function outlineLines(doc: Text, fromLine: number, toLine: number, isCode: (n: number) => boolean = () => false): Map<number, OutlineLine> {
  const out = new Map<number, OutlineLine>();
  fromLine = Math.max(1, fromLine);
  toLine = Math.min(doc.lines, toLine);
  if (fromLine > toLine) return out;

  // Start at the nearest line at column 0 above (the root of the outline we are in).
  let start = fromLine;
  let stack: StackEntry[] = [];
  for (let n = fromLine, steps = 0; n >= 1; n--, steps++) {
    if (steps > MAX_SCAN_BACK) {
      // Very long subtree: seed the stack with the ancestors of the first line instead.
      start = fromLine;
      stack = ancestorItems(doc, fromLine).map((a, depth) => {
        const text = doc.line(a).text;
        const item = parseListLine(text)!;
        return { cols: item.indentCols, line: a, depth, text, item };
      });
      break;
    }
    const text = doc.line(n).text;
    if (isBlank(text)) continue;
    start = n;
    if (indentColumns(text) === 0) break;
  }

  const ends = new Map<number, number>();
  const endOf = (line: number) => {
    let e = ends.get(line);
    if (e === undefined) ends.set(line, (e = itemEndLine(doc, line)));
    return e;
  };

  for (let n = start; n <= toLine; n++) {
    const text = doc.line(n).text;
    if (isBlank(text)) continue;
    const code = isCode(n);
    const item = code ? null : parseListLine(text);
    if (item) {
      while (stack.length && stack[stack.length - 1]!.cols >= item.indentCols) stack.pop();
      const depth = stack.length;
      stack.push({ cols: item.indentCols, line: n, depth, text, item });
      if (n >= fromLine) {
        out.set(n, { kind: 'item', line: n, depth, item, task: taskMarker(text, item), hasChildren: endOf(n) > n });
      }
      continue;
    }
    const cols = indentColumns(text);
    if (cols === 0) {
      stack = [];
      continue;
    }
    while (stack.length && stack[stack.length - 1]!.cols >= cols) stack.pop();
    const owner = stack[stack.length - 1];
    if (!owner) continue;
    if (n >= fromLine) {
      out.set(n, {
        kind: 'cont',
        line: n,
        depth: owner.depth,
        owner: owner.line,
        wsLength: structuralWhitespace(text, owner),
        ownerContinues: endOf(owner.line) > n,
      });
    }
  }
  return out;
}

/** Leading whitespace of a continuation line that belongs to the outline (up to the owner's text column). */
function structuralWhitespace(text: string, owner: StackEntry): number {
  const ws = /^[ \t]*/.exec(text)![0];
  const limit = ws.includes('\t') ? owner.cols + TAB_WIDTH : columnsOf(owner.text, owner.item.markerTo) + 1;
  let i = 0;
  while (i < ws.length && columnsOf(ws, i + 1) <= limit) i++;
  return i;
}
