/**
 * Line-level diff (Myers O((N+M)·D) with trimmed common prefix/suffix) plus helpers for the
 * DiffView: side-by-side row pairing, collapsing unchanged runs and intra-line word diffs.
 *
 * Size guard: the edit script is computed only up to `maxEdits` differences (memory grows
 * with the square of that). Beyond it the differing middle part is shown as "all removed,
 * all added" and `truncated` is set.
 */

export type DiffKind = 'equal' | 'add' | 'del';

export interface DiffLine {
  kind: DiffKind;
  text: string;
  /** 1-based line number in the left (old) text; for 'equal' and 'del'. */
  oldNo?: number;
  /** 1-based line number in the right (new) text; for 'equal' and 'add'. */
  newNo?: number;
}

export interface DiffResult {
  lines: DiffLine[];
  added: number;
  removed: number;
  /** The size guard kicked in (simplified result). */
  truncated: boolean;
}

export interface DiffOptions {
  /** Maximum number of differences to resolve exactly (default 2000). */
  maxEdits?: number;
}

/** Split text into lines (\n, \r\n or \r); a final newline does not create an empty last line. */
export function splitLines(s: string): string[] {
  if (s === '') return [];
  const parts = s.split(/\r\n|\n|\r/);
  if (parts[parts.length - 1] === '') parts.pop();
  return parts;
}

/** Edit operation over two sequences: [kind, index in a (or -1), index in b (or -1)]. */
type Op = [DiffKind, number, number];

/**
 * Myers' shortest edit script between two integer sequences. Returns null when more than
 * `maxD` edits would be needed.
 */
function myers(a: Int32Array, b: Int32Array, maxD: number): Op[] | null {
  const n = a.length;
  const m = b.length;
  const max = n + m;
  if (max === 0) return [];
  const offset = max + 1;
  const v = new Int32Array(2 * max + 3);
  const trace: Int32Array[] = [];
  const limit = Math.min(max, maxD);
  for (let d = 0; d <= limit; d++) {
    trace.push(v.slice(offset - d, offset + d + 1));
    for (let k = -d; k <= d; k += 2) {
      let x: number;
      if (k === -d || (k !== d && v[offset + k - 1]! < v[offset + k + 1]!)) x = v[offset + k + 1]!;
      else x = v[offset + k - 1]! + 1;
      let y = x - k;
      while (x < n && y < m && a[x] === b[y]) {
        x++;
        y++;
      }
      v[offset + k] = x;
      if (x >= n && y >= m) return backtrack(trace, d, n, m);
    }
  }
  return null;
}

function backtrack(trace: Int32Array[], dEnd: number, n: number, m: number): Op[] {
  const ops: Op[] = [];
  let x = n;
  let y = m;
  for (let d = dEnd; d > 0; d--) {
    const saved = trace[d]!; // v as it was before step d: index k + d for k in [-d, d]
    const get = (k: number) => saved[k + d]!;
    const k = x - y;
    const prevK = k === -d || (k !== d && get(k - 1) < get(k + 1)) ? k + 1 : k - 1;
    const prevX = get(prevK);
    const prevY = prevX - prevK;
    while (x > prevX && y > prevY) {
      ops.push(['equal', x - 1, y - 1]);
      x--;
      y--;
    }
    if (x === prevX) ops.push(['add', -1, y - 1]);
    else ops.push(['del', x - 1, -1]);
    x = prevX;
    y = prevY;
  }
  while (x > 0 && y > 0) {
    ops.push(['equal', x - 1, y - 1]);
    x--;
    y--;
  }
  return ops.reverse();
}

/** Map strings to small integers so comparisons are cheap. */
function intern(a: readonly string[], b: readonly string[]): [Int32Array, Int32Array] {
  const ids = new Map<string, number>();
  const conv = (arr: readonly string[]) => {
    const out = new Int32Array(arr.length);
    for (let i = 0; i < arr.length; i++) {
      const s = arr[i]!;
      let id = ids.get(s);
      if (id === undefined) {
        id = ids.size;
        ids.set(s, id);
      }
      out[i] = id;
    }
    return out;
  };
  return [conv(a), conv(b)];
}

/**
 * Diff two token sequences. Common prefix/suffix are matched first; the rest goes through
 * Myers (or, past `maxEdits`, is reported as one delete block + one insert block).
 */
export function diffSequences(
  a: readonly string[],
  b: readonly string[],
  maxEdits = 2000,
): { ops: Op[]; truncated: boolean } {
  let start = 0;
  while (start < a.length && start < b.length && a[start] === b[start]) start++;
  let endA = a.length;
  let endB = b.length;
  while (endA > start && endB > start && a[endA - 1] === b[endB - 1]) {
    endA--;
    endB--;
  }
  const ops: Op[] = [];
  for (let i = 0; i < start; i++) ops.push(['equal', i, i]);
  const midA = a.slice(start, endA);
  const midB = b.slice(start, endB);
  let truncated = false;
  if (midA.length > 0 || midB.length > 0) {
    const [ia, ib] = intern(midA, midB);
    const mid = midA.length === 0 || midB.length === 0 ? null : myers(ia, ib, maxEdits);
    if (mid) {
      for (const [kind, i, j] of mid) ops.push([kind, i < 0 ? -1 : i + start, j < 0 ? -1 : j + start]);
    } else {
      // Pure insertion/deletion (exact), or too many edits (simplified).
      truncated = midA.length > 0 && midB.length > 0;
      for (let i = start; i < endA; i++) ops.push(['del', i, -1]);
      for (let j = start; j < endB; j++) ops.push(['add', -1, j]);
    }
  }
  for (let i = endA, j = endB; i < a.length; i++, j++) ops.push(['equal', i, j]);
  return { ops, truncated };
}

/** Line diff of two texts. Within a change block, deletions come before additions. */
export function diffLines(left: string, right: string, opts: DiffOptions = {}): DiffResult {
  const a = splitLines(left);
  const b = splitLines(right);
  const { ops, truncated } = diffSequences(a, b, opts.maxEdits ?? 2000);
  const lines: DiffLine[] = [];
  let added = 0;
  let removed = 0;
  // Reorder each change block as dels then adds (Myers may interleave them).
  let pendingDel: DiffLine[] = [];
  let pendingAdd: DiffLine[] = [];
  const flush = () => {
    lines.push(...pendingDel, ...pendingAdd);
    pendingDel = [];
    pendingAdd = [];
  };
  for (const [kind, i, j] of ops) {
    if (kind === 'equal') {
      flush();
      lines.push({ kind, text: a[i]!, oldNo: i + 1, newNo: j + 1 });
    } else if (kind === 'del') {
      removed++;
      pendingDel.push({ kind, text: a[i]!, oldNo: i + 1 });
    } else {
      added++;
      pendingAdd.push({ kind, text: b[j]!, newNo: j + 1 });
    }
  }
  flush();
  return { lines, added, removed, truncated };
}

// ---------------------------------------------------------------------------------------------
// Side-by-side rows
// ---------------------------------------------------------------------------------------------

export interface SplitRow {
  left?: DiffLine;
  right?: DiffLine;
}

/** Pair deletions with additions of the same change block for side-by-side display. */
export function toSplitRows(lines: readonly DiffLine[]): SplitRow[] {
  const rows: SplitRow[] = [];
  let i = 0;
  while (i < lines.length) {
    const line = lines[i]!;
    if (line.kind === 'equal') {
      rows.push({ left: line, right: line });
      i++;
      continue;
    }
    const dels: DiffLine[] = [];
    const adds: DiffLine[] = [];
    while (i < lines.length && lines[i]!.kind !== 'equal') {
      const l = lines[i]!;
      (l.kind === 'del' ? dels : adds).push(l);
      i++;
    }
    const n = Math.max(dels.length, adds.length);
    for (let k = 0; k < n; k++) rows.push({ left: dels[k], right: adds[k] });
  }
  return rows;
}

// ---------------------------------------------------------------------------------------------
// Collapsing unchanged runs
// ---------------------------------------------------------------------------------------------

export type Collapsed<T> = { type: 'item'; item: T; index: number } | { type: 'gap'; count: number; start: number };

/**
 * Hide long runs of unchanged items, keeping `context` items next to every change.
 * Runs shorter than or equal to 2·context+1 are kept. `expanded` lists gap starts to show in full.
 */
export function collapseUnchanged<T>(
  items: readonly T[],
  isUnchanged: (item: T) => boolean,
  context = 3,
  expanded: ReadonlySet<number> = new Set(),
): Collapsed<T>[] {
  const out: Collapsed<T>[] = [];
  let i = 0;
  while (i < items.length) {
    if (!isUnchanged(items[i]!)) {
      out.push({ type: 'item', item: items[i]!, index: i });
      i++;
      continue;
    }
    let j = i;
    while (j < items.length && isUnchanged(items[j]!)) j++;
    const atStart = i === 0;
    const atEnd = j === items.length;
    const keepBefore = atStart ? 0 : context;
    const keepAfter = atEnd ? 0 : context;
    const hidden = j - i - keepBefore - keepAfter;
    const gapStart = i + keepBefore;
    if (hidden > 1 && !expanded.has(gapStart)) {
      for (let k = i; k < gapStart; k++) out.push({ type: 'item', item: items[k]!, index: k });
      out.push({ type: 'gap', count: hidden, start: gapStart });
      for (let k = j - keepAfter; k < j; k++) out.push({ type: 'item', item: items[k]!, index: k });
    } else {
      for (let k = i; k < j; k++) out.push({ type: 'item', item: items[k]!, index: k });
    }
    i = j;
  }
  return out;
}

// ---------------------------------------------------------------------------------------------
// Word diff inside a changed line pair
// ---------------------------------------------------------------------------------------------

export interface Segment {
  text: string;
  changed: boolean;
}

const TOKEN_RE = /\s+|[\p{L}\p{N}_]+|[^\s\p{L}\p{N}_]/gu;

export function tokenize(s: string): string[] {
  return s.match(TOKEN_RE) ?? [];
}

function pushSeg(segs: Segment[], text: string, changed: boolean): void {
  const last = segs[segs.length - 1];
  if (last && last.changed === changed) last.text += text;
  else segs.push({ text, changed });
}

/**
 * Word-level highlight for a deleted/added line pair. Returns null when the lines are too
 * long or too different for the highlight to help (then the whole line is marked).
 */
export function diffWords(a: string, b: string, maxTokens = 400): { left: Segment[]; right: Segment[] } | null {
  const ta = tokenize(a);
  const tb = tokenize(b);
  if (ta.length > maxTokens || tb.length > maxTokens) return null;
  const { ops, truncated } = diffSequences(ta, tb, 200);
  if (truncated) return null;
  const left: Segment[] = [];
  const right: Segment[] = [];
  let same = 0;
  for (const [kind, i, j] of ops) {
    if (kind === 'equal') {
      pushSeg(left, ta[i]!, false);
      pushSeg(right, tb[j]!, false);
      if (ta[i]!.trim()) same += ta[i]!.length;
    } else if (kind === 'del') pushSeg(left, ta[i]!, true);
    else pushSeg(right, tb[j]!, true);
  }
  // Barely anything in common: highlighting fragments is noise.
  const longest = Math.max(a.replace(/\s+/g, '').length, b.replace(/\s+/g, '').length);
  if (longest > 0 && same / longest < 0.25) return null;
  return { left, right };
}
