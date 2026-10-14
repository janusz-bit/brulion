/**
 * Line diff (Myers' O((N+M)·D) algorithm with linear-space bisection, common prefix/suffix
 * trimming and a work budget) and the diff3 region split built on it.
 *
 * node-diff3's LCS degrades to roughly cubic time on texts with many identical lines (blank
 * lines between paragraphs): a 4000-line note took over a minute on a Raspberry Pi. This
 * implementation stays fast for typical edits and is bounded for pathological inputs: when the
 * budget runs out the remaining range is reported as fully changed, which can only turn a
 * clean merge into a conflict (never lose text).
 */

/** A run of differing lines between `a` and `b` (`a[aStart, aStart+aLength)` -> `b[...]`). */
export interface DiffHunk {
  aStart: number;
  aLength: number;
  bStart: number;
  bLength: number;
}

/** One region of a three-way merge (same shape as node-diff3's `diff3Merge`). */
export type Diff3Region = { ok: string[]; conflict?: undefined } | { ok?: undefined; conflict: { a: string[]; o: string[]; b: string[] } };

/** Default work budget per diff (inner-loop steps); roughly 0.4 s on a Raspberry Pi 4. */
export const DIFF_BUDGET = 8_000_000;

interface DiffContext {
  a: Int32Array;
  b: Int32Array;
  /** Matched indexes in `a` / `b`, ascending. */
  ma: number[];
  mb: number[];
  budget: number;
}

/** Map lines to integer ids (shared map so equal lines get equal ids across sequences). */
function intern(lines: readonly string[], ids: Map<string, number>): Int32Array {
  const out = new Int32Array(lines.length);
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i]!;
    let id = ids.get(line);
    if (id === undefined) {
      id = ids.size;
      ids.set(line, id);
    }
    out[i] = id;
  }
  return out;
}

/**
 * Find a split point on an optimal edit path of `a[aLo,aHi)` / `b[bLo,bHi)` by running Myers'
 * search from both ends until the paths overlap (the "middle snake"). Returns absolute
 * indexes, or null when the budget is exhausted (treat the range as fully changed).
 */
function bisect(ctx: DiffContext, aLo: number, aHi: number, bLo: number, bHi: number): [number, number] | null {
  const { a, b } = ctx;
  const n = aHi - aLo;
  const m = bHi - bLo;
  const maxD = Math.ceil((n + m) / 2);
  const offset = maxD;
  const size = 2 * maxD + 2;
  const v1 = new Int32Array(size).fill(-1);
  const v2 = new Int32Array(size).fill(-1);
  v1[offset + 1] = 0;
  v2[offset + 1] = 0;
  const delta = n - m;
  const front = delta % 2 !== 0;
  let k1start = 0;
  let k1end = 0;
  let k2start = 0;
  let k2end = 0;
  for (let d = 0; d < maxD; d++) {
    if (ctx.budget <= 0) return null;
    // Forward search.
    for (let k1 = -d + k1start; k1 <= d - k1end; k1 += 2) {
      const k1o = offset + k1;
      let x1 = k1 === -d || (k1 !== d && v1[k1o - 1]! < v1[k1o + 1]!) ? v1[k1o + 1]! : v1[k1o - 1]! + 1;
      let y1 = x1 - k1;
      const snakeStart = x1;
      while (x1 < n && y1 < m && a[aLo + x1] === b[bLo + y1]) {
        x1++;
        y1++;
      }
      ctx.budget -= 1 + x1 - snakeStart;
      v1[k1o] = x1;
      if (x1 > n) k1end += 2;
      else if (y1 > m) k1start += 2;
      else if (front) {
        const k2o = offset + delta - k1;
        if (k2o >= 0 && k2o < size && v2[k2o] !== -1 && x1 >= n - v2[k2o]!) return [aLo + x1, bLo + y1];
      }
    }
    // Reverse search.
    for (let k2 = -d + k2start; k2 <= d - k2end; k2 += 2) {
      const k2o = offset + k2;
      let x2 = k2 === -d || (k2 !== d && v2[k2o - 1]! < v2[k2o + 1]!) ? v2[k2o + 1]! : v2[k2o - 1]! + 1;
      let y2 = x2 - k2;
      const snakeStart = x2;
      while (x2 < n && y2 < m && a[aHi - x2 - 1] === b[bHi - y2 - 1]) {
        x2++;
        y2++;
      }
      ctx.budget -= 1 + x2 - snakeStart;
      v2[k2o] = x2;
      if (x2 > n) k2end += 2;
      else if (y2 > m) k2start += 2;
      else if (!front) {
        const k1o = offset + delta - k2;
        if (k1o >= 0 && k1o < size && v1[k1o] !== -1) {
          const x1 = v1[k1o]!;
          const y1 = offset + x1 - k1o;
          if (x1 >= n - x2) return [aLo + x1, bLo + y1];
        }
      }
    }
  }
  return null;
}

/** Collect the matching lines of `a[aLo,aHi)` and `b[bLo,bHi)` into `ctx.ma` / `ctx.mb`. */
function diffRange(ctx: DiffContext, aLo: number, aHi: number, bLo: number, bHi: number): void {
  const { a, b } = ctx;
  while (aLo < aHi && bLo < bHi && a[aLo] === b[bLo]) {
    ctx.ma.push(aLo++);
    ctx.mb.push(bLo++);
  }
  let suffix = 0;
  while (aLo < aHi - suffix && bLo < bHi - suffix && a[aHi - 1 - suffix] === b[bHi - 1 - suffix]) suffix++;
  const aEnd = aHi - suffix;
  const bEnd = bHi - suffix;
  if (aLo < aEnd && bLo < bEnd) {
    const split = bisect(ctx, aLo, aEnd, bLo, bEnd);
    // A split at either corner would not shrink the problem: treat as fully changed.
    if (split && !(split[0] === aLo && split[1] === bLo) && !(split[0] === aEnd && split[1] === bEnd)) {
      diffRange(ctx, aLo, split[0], bLo, split[1]);
      diffRange(ctx, split[0], aEnd, split[1], bEnd);
    }
  }
  for (let i = 0; i < suffix; i++) {
    ctx.ma.push(aEnd + i);
    ctx.mb.push(bEnd + i);
  }
}

/** Matching line index pairs (a longest common subsequence, or close to one) of two id arrays. */
function matchIds(a: Int32Array, b: Int32Array, budget: number): { ma: number[]; mb: number[] } {
  const ctx: DiffContext = { a, b, ma: [], mb: [], budget };
  diffRange(ctx, 0, a.length, 0, b.length);
  return { ma: ctx.ma, mb: ctx.mb };
}

function hunksFromMatches(ma: readonly number[], mb: readonly number[], aLength: number, bLength: number): DiffHunk[] {
  const out: DiffHunk[] = [];
  let pa = 0;
  let pb = 0;
  for (let i = 0; i <= ma.length; i++) {
    const ia = i < ma.length ? ma[i]! : aLength;
    const ib = i < mb.length ? mb[i]! : bLength;
    if (ia > pa || ib > pb) out.push({ aStart: pa, aLength: ia - pa, bStart: pb, bLength: ib - pb });
    pa = ia + 1;
    pb = ib + 1;
  }
  return out;
}

/**
 * Differences between two line arrays as hunks, in order. Lines between hunks are equal.
 * `budget` bounds the work (default `DIFF_BUDGET`); beyond it ranges are reported as changed.
 */
export function diffLines(a: readonly string[], b: readonly string[], budget = DIFF_BUDGET): DiffHunk[] {
  const ids = new Map<string, number>();
  const ia = intern(a, ids);
  const ib = intern(b, ids);
  const { ma, mb } = matchIds(ia, ib, budget);
  return hunksFromMatches(ma, mb, a.length, b.length);
}

/** One part of a two-way comparison: common lines, or lines only in `a` / only in `b`. */
export type CommPart = { common: string[] } | { common?: undefined; a: string[]; b: string[] };

/** Two-way comparison in document order (like node-diff3's `diffComm`). */
export function diffComm(a: readonly string[], b: readonly string[], budget = DIFF_BUDGET): CommPart[] {
  const out: CommPart[] = [];
  let pa = 0;
  for (const h of diffLines(a, b, budget)) {
    if (h.aStart > pa) out.push({ common: a.slice(pa, h.aStart) });
    out.push({ a: a.slice(h.aStart, h.aStart + h.aLength), b: b.slice(h.bStart, h.bStart + h.bLength) });
    pa = h.aStart + h.aLength;
  }
  if (pa < a.length) out.push({ common: a.slice(pa) });
  return out;
}

interface SideHunk {
  side: 'a' | 'b';
  oStart: number;
  oLength: number;
  sStart: number;
  sLength: number;
}

function sameLines(x: readonly string[], y: readonly string[]): boolean {
  if (x.length !== y.length) return false;
  for (let i = 0; i < x.length; i++) if (x[i] !== y[i]) return false;
  return true;
}

/**
 * Three-way merge regions of `a` (local) and `b` (remote) against the common ancestor `o`
 * (same semantics as node-diff3's `diff3Merge(a, o, b, { excludeFalseConflicts: true })`):
 * changes of one side are taken, overlapping or adjacent changes of both sides form a
 * conflict region unless both sides made the same change.
 */
export function diff3Regions(a: readonly string[], o: readonly string[], b: readonly string[], budget = DIFF_BUDGET): Diff3Region[] {
  const ids = new Map<string, number>();
  const io = intern(o, ids);
  const ia = intern(a, ids);
  const ib = intern(b, ids);
  const side = (s: 'a' | 'b', ids2: Int32Array, len: number): SideHunk[] => {
    const { ma, mb } = matchIds(io, ids2, budget);
    return hunksFromMatches(ma, mb, o.length, len).map((h) => ({ side: s, oStart: h.aStart, oLength: h.aLength, sStart: h.bStart, sLength: h.bLength }));
  };
  // Stable sort: for equal starts, local hunks come first (like node-diff3).
  const hunks = [...side('a', ia, a.length), ...side('b', ib, b.length)].sort((x, y) => x.oStart - y.oStart);

  const regions: Diff3Region[] = [];
  let okBuffer: string[] = [];
  const pushOk = (lines: readonly string[]): void => {
    for (const l of lines) okBuffer.push(l);
  };
  const flushOk = (): void => {
    if (okBuffer.length) regions.push({ ok: okBuffer });
    okBuffer = [];
  };
  let current = 0;
  // Running (side index - base index) offsets after the hunks processed so far.
  let dA = 0;
  let dB = 0;
  let i = 0;
  while (i < hunks.length) {
    const first = hunks[i]!;
    const regionStart = first.oStart;
    let regionEnd = first.oStart + first.oLength;
    let j = i + 1;
    while (j < hunks.length && hunks[j]!.oStart <= regionEnd) {
      regionEnd = Math.max(regionEnd, hunks[j]!.oStart + hunks[j]!.oLength);
      j++;
    }
    if (regionStart > current) pushOk(o.slice(current, regionStart));
    const aStart = regionStart + dA;
    const bStart = regionStart + dB;
    for (let k = i; k < j; k++) {
      const h = hunks[k]!;
      if (h.side === 'a') dA += h.sLength - h.oLength;
      else dB += h.sLength - h.oLength;
    }
    if (j - i === 1) {
      const src = first.side === 'a' ? a : b;
      pushOk(src.slice(first.sStart, first.sStart + first.sLength));
    } else {
      const aContent = a.slice(aStart, regionEnd + dA);
      const bContent = b.slice(bStart, regionEnd + dB);
      if (sameLines(aContent, bContent)) pushOk(aContent);
      else {
        flushOk();
        regions.push({ conflict: { a: aContent, o: o.slice(regionStart, regionEnd), b: bContent } });
      }
    }
    current = regionEnd;
    i = j;
  }
  if (current < o.length) pushOk(o.slice(current));
  flushOk();
  return regions;
}
