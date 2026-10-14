import { describe, expect, it } from 'vitest';
import { diff3Merge, LCS } from 'node-diff3';
import { diff3Regions, diffComm, diffLines, type DiffHunk } from './diff';

/** Deterministic PRNG (mulberry32) so failures are reproducible. */
function rng(seed: number): () => number {
  let s = seed >>> 0;
  return () => {
    s = (s + 0x6d2b79f5) >>> 0;
    let t = s;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function randomLines(r: () => number, n: number, alphabet: readonly string[]): string[] {
  return Array.from({ length: n }, () => alphabet[Math.floor(r() * alphabet.length)]!);
}

/** Matched pairs implied by hunks (lines between hunks are equal). */
function matchesOf(hunks: readonly DiffHunk[], aLength: number): [number, number][] {
  const out: [number, number][] = [];
  let pa = 0;
  let pb = 0;
  for (const h of [...hunks, { aStart: aLength, aLength: 0, bStart: Number.NaN, bLength: 0 }]) {
    while (pa < h.aStart) out.push([pa++, pb++]);
    pa = h.aStart + h.aLength;
    pb = h.bStart + h.bLength;
  }
  return out;
}

/** A node of node-diff3's LCS candidate chain (its typings declare `chain: null`). */
interface LcsNode {
  buffer1index: number;
  chain: LcsNode | null;
}

function nodeDiff3LcsLength(a: string[], b: string[]): number {
  let n = 0;
  for (let c = (LCS(a, b) ?? null) as LcsNode | null; c && c.buffer1index !== -1; c = c.chain) n++;
  return n;
}

const ALPHABET = ['', '- a', '- b', '# H', 'ąę', 'żółw'];

describe('diffLines', () => {
  it('returns no hunks for equal input and one hunk for disjoint input', () => {
    expect(diffLines(['a', 'b'], ['a', 'b'])).toEqual([]);
    expect(diffLines([], [])).toEqual([]);
    expect(diffLines(['a'], ['b'])).toEqual([{ aStart: 0, aLength: 1, bStart: 0, bLength: 1 }]);
    expect(diffLines([], ['x', 'y'])).toEqual([{ aStart: 0, aLength: 0, bStart: 0, bLength: 2 }]);
    expect(diffLines(['x'], [])).toEqual([{ aStart: 0, aLength: 1, bStart: 0, bLength: 0 }]);
  });

  it('finds insertions, deletions and replacements in the middle', () => {
    expect(diffLines(['a', 'b', 'c'], ['a', 'X', 'b', 'c'])).toEqual([{ aStart: 1, aLength: 0, bStart: 1, bLength: 1 }]);
    expect(diffLines(['a', 'b', 'c'], ['a', 'c'])).toEqual([{ aStart: 1, aLength: 1, bStart: 1, bLength: 0 }]);
    expect(diffLines(['Zażółć', 'gęślą', 'jaźń'], ['Zażółć', 'GĘŚLĄ', 'jaźń'])).toEqual([{ aStart: 1, aLength: 1, bStart: 1, bLength: 1 }]);
  });

  it('treats special strings like any other line', () => {
    expect(diffLines(['__proto__', 'constructor'], ['__proto__', 'x', 'constructor'])).toEqual([{ aStart: 1, aLength: 0, bStart: 1, bLength: 1 }]);
  });

  it('produces a valid longest common subsequence on random input (same length as node-diff3)', () => {
    const r = rng(42);
    for (let round = 0; round < 300; round++) {
      const a = randomLines(r, Math.floor(r() * 40), ALPHABET);
      const b = randomLines(r, Math.floor(r() * 40), ALPHABET);
      const pairs = matchesOf(diffLines(a, b), a.length);
      for (let i = 0; i < pairs.length; i++) {
        const [x, y] = pairs[i]!;
        expect(a[x]).toBe(b[y]);
        if (i > 0) {
          expect(x).toBeGreaterThan(pairs[i - 1]![0]);
          expect(y).toBeGreaterThan(pairs[i - 1]![1]);
        }
      }
      expect(pairs.length).toBe(nodeDiff3LcsLength(a, b));
    }
  });

  it('stays correct (if not minimal) when the work budget runs out', () => {
    const r = rng(7);
    const a = randomLines(r, 300, ALPHABET);
    const b = randomLines(r, 300, ALPHABET);
    const pairs = matchesOf(diffLines(a, b, 50), a.length);
    for (const [x, y] of pairs) expect(a[x]).toBe(b[y]);
  });

  it('is fast on long notes with many identical (blank) lines', () => {
    const lines: string[] = [];
    for (let i = 0; i < 8000; i++) lines.push(i % 2 ? '' : `akapit ${i % 37}`);
    const changed = [...lines];
    for (let i = 100; i < 8000; i += 400) changed[i] = `zmiana ${i}`;
    const t = performance.now();
    const hunks = diffLines(lines, ['nowy początek', ...changed, 'koniec']);
    expect(performance.now() - t).toBeLessThan(2000);
    expect(hunks.length).toBe(22);
  });

  it('bounds the time for completely different large inputs', () => {
    const a = Array.from({ length: 20000 }, (_, i) => `a${i % 97}`);
    const b = Array.from({ length: 20000 }, (_, i) => `b${i % 89}`);
    const t = performance.now();
    expect(diffLines(a, b)).toEqual([{ aStart: 0, aLength: 20000, bStart: 0, bLength: 20000 }]);
    expect(performance.now() - t).toBeLessThan(5000);
  });
});

describe('diffComm', () => {
  it('splits into common and differing parts in order', () => {
    expect(diffComm(['a', 'b', 'c'], ['a', 'x', 'c', 'd'])).toEqual([{ common: ['a'] }, { a: ['b'], b: ['x'] }, { common: ['c'] }, { a: [], b: ['d'] }]);
    expect(diffComm([], [])).toEqual([]);
  });
});

/** Random base of unique lines and two derived versions (unique LCS, so results must equal node-diff3's). */
function derive(r: () => number, base: readonly string[], tag: string): string[] {
  const out: string[] = [];
  base.forEach((line, i) => {
    const p = r();
    if (p < 0.1) return; // delete
    if (p < 0.2) out.push(`${tag}-zmiana-${i}`);
    else out.push(line);
    if (r() < 0.1) out.push(`${tag}-nowa-${i}`);
  });
  if (r() < 0.3) out.unshift(`${tag}-na-początku`);
  return out;
}

describe('diff3Regions', () => {
  it('matches node-diff3 diff3Merge on hand-written cases', () => {
    const cases: [string[], string[], string[]][] = [
      [['a', 'B', 'c'], ['a', 'b', 'c'], ['a', 'b', 'C']],
      [['a', 'b', 'c', 'x'], ['a', 'b', 'c'], ['a', 'b', 'c', 'y']],
      [['A', 'b'], ['a', 'b'], ['a', 'B']],
      [['a', 'c'], ['a', 'b', 'c'], ['a', 'B', 'c']],
      [[], ['a'], ['a', 'b']],
      [['x', 'y'], [], ['y', 'z']],
      [['a', 'B', 'c'], ['a', 'b', 'c'], ['a', 'B', 'c']],
    ];
    for (const [a, o, b] of cases) expect(diff3Regions(a, o, b)).toEqual(diff3Merge(a, o, b, { excludeFalseConflicts: true }).map(strip));
  });

  it('matches node-diff3 diff3Merge on random edits of unique lines', () => {
    const r = rng(2026);
    for (let round = 0; round < 200; round++) {
      const base = Array.from({ length: Math.floor(r() * 30) }, (_, i) => `linia ${i} — źdźbło`);
      const a = derive(r, base, 'L');
      const b = derive(r, base, 'R');
      expect(diff3Regions(a, base, b)).toEqual(diff3Merge(a, base, b, { excludeFalseConflicts: true }).map(strip));
    }
  });

  it('takes one-sided changes and reports overlapping ones as conflicts', () => {
    expect(diff3Regions(['a', 'b'], ['a', 'b'], ['a', 'b', 'c'])).toEqual([{ ok: ['a', 'b', 'c'] }]);
    expect(diff3Regions(['L'], ['o'], ['R'])).toEqual([{ conflict: { a: ['L'], o: ['o'], b: ['R'] } }]);
  });
});

/** node-diff3 regions without the index fields. */
function strip(r: { ok?: string[]; conflict?: { a: string[]; o: string[]; b: string[] } }) {
  return r.ok ? { ok: r.ok } : { conflict: { a: r.conflict!.a, o: r.conflict!.o, b: r.conflict!.b } };
}
