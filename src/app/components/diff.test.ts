import { describe, expect, it } from 'vitest';
import {
  collapseUnchanged,
  diffLines,
  diffSequences,
  diffWords,
  splitLines,
  toSplitRows,
  tokenize,
  type DiffLine,
} from './diff';

/** Classic DP LCS length, the reference for optimality. */
function lcsLength(a: readonly string[], b: readonly string[]): number {
  const dp: number[][] = Array.from({ length: a.length + 1 }, () => Array.from({ length: b.length + 1 }, () => 0));
  for (let i = 1; i <= a.length; i++)
    for (let j = 1; j <= b.length; j++)
      dp[i]![j] = a[i - 1] === b[j - 1] ? dp[i - 1]![j - 1]! + 1 : Math.max(dp[i - 1]![j]!, dp[i]![j - 1]!);
  return dp[a.length]![b.length]!;
}

function rebuild(lines: readonly DiffLine[]): { left: string[]; right: string[] } {
  return {
    left: lines.filter((l) => l.kind !== 'add').map((l) => l.text),
    right: lines.filter((l) => l.kind !== 'del').map((l) => l.text),
  };
}

/** Deterministic PRNG (mulberry32). */
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

describe('splitLines', () => {
  it('handles empty text, trailing newline and mixed line endings', () => {
    expect(splitLines('')).toEqual([]);
    expect(splitLines('a')).toEqual(['a']);
    expect(splitLines('a\n')).toEqual(['a']);
    expect(splitLines('a\r\nb\rc\n\n')).toEqual(['a', 'b', 'c', '']);
    expect(splitLines('\n')).toEqual(['']);
  });
});

describe('diffLines', () => {
  it('reports no changes for equal texts', () => {
    const r = diffLines('jeden\ndwa\ntrzy', 'jeden\ndwa\ntrzy');
    expect(r.added).toBe(0);
    expect(r.removed).toBe(0);
    expect(r.truncated).toBe(false);
    expect(r.lines.every((l) => l.kind === 'equal')).toBe(true);
    expect(r.lines.map((l) => [l.oldNo, l.newNo])).toEqual([
      [1, 1],
      [2, 2],
      [3, 3],
    ]);
  });

  it('finds a changed line with Polish text and numbers lines', () => {
    const r = diffLines('# Zakupy\n- mleko\n- chleb\n- masło', '# Zakupy\n- mleko\n- chleb żytni\n- masło\n- jajka');
    expect(r.added).toBe(2);
    expect(r.removed).toBe(1);
    expect(r.lines).toEqual([
      { kind: 'equal', text: '# Zakupy', oldNo: 1, newNo: 1 },
      { kind: 'equal', text: '- mleko', oldNo: 2, newNo: 2 },
      { kind: 'del', text: '- chleb', oldNo: 3 },
      { kind: 'add', text: '- chleb żytni', newNo: 3 },
      { kind: 'equal', text: '- masło', oldNo: 4, newNo: 4 },
      { kind: 'add', text: '- jajka', newNo: 5 },
    ]);
  });

  it('handles empty sides', () => {
    expect(diffLines('', 'a\nb').lines.map((l) => l.kind)).toEqual(['add', 'add']);
    expect(diffLines('a\nb', '').lines.map((l) => l.kind)).toEqual(['del', 'del']);
    expect(diffLines('', '').lines).toEqual([]);
  });

  it('ignores line-ending differences only at the very end', () => {
    expect(diffLines('a\nb\n', 'a\nb').added).toBe(0);
    expect(diffLines('a\r\nb', 'a\nb').added).toBe(0);
  });

  it('produces minimal edit scripts (matches DP LCS) and reconstructs both texts', () => {
    const rand = rng(42);
    const alphabet = ['a', 'b', 'c', 'ą', 'ż', ''];
    for (let round = 0; round < 300; round++) {
      const la = Math.floor(rand() * 14);
      const lb = Math.floor(rand() * 14);
      const a = Array.from({ length: la }, () => alphabet[Math.floor(rand() * alphabet.length)]!);
      const b = Array.from({ length: lb }, () => alphabet[Math.floor(rand() * alphabet.length)]!);
      // Join as text: an empty last line would vanish, so compare on the split result.
      const left = a.join('\n');
      const right = b.join('\n');
      const r = diffLines(left, right);
      const sa = splitLines(left);
      const sb = splitLines(right);
      const rebuilt = rebuild(r.lines);
      expect(rebuilt.left).toEqual(sa);
      expect(rebuilt.right).toEqual(sb);
      const equal = r.lines.filter((l) => l.kind === 'equal').length;
      expect(equal).toBe(lcsLength(sa, sb));
      expect(r.added).toBe(sb.length - equal);
      expect(r.removed).toBe(sa.length - equal);
    }
  });

  it('keeps line numbers consistent', () => {
    const r = diffLines('x\na\nb\nc\ny', 'a\nB\nc\nz\ny');
    let old = 0;
    let neu = 0;
    for (const l of r.lines) {
      if (l.kind !== 'add') expect(l.oldNo).toBe(++old);
      if (l.kind !== 'del') expect(l.newNo).toBe(++neu);
    }
  });

  it('falls back to a simplified diff past the size guard', () => {
    const a = Array.from({ length: 300 }, (_, i) => `a${i}`).join('\n');
    const b = Array.from({ length: 300 }, (_, i) => `b${i}`).join('\n');
    const r = diffLines(`start\n${a}\nend`, `start\n${b}\nend`, { maxEdits: 50 });
    expect(r.truncated).toBe(true);
    expect(r.lines[0]).toEqual({ kind: 'equal', text: 'start', oldNo: 1, newNo: 1 });
    expect(r.lines[r.lines.length - 1]).toEqual({ kind: 'equal', text: 'end', oldNo: 302, newNo: 302 });
    expect(r.removed).toBe(300);
    expect(r.added).toBe(300);
  });

  it('is fast enough for a few thousand lines', () => {
    const base = Array.from({ length: 4000 }, (_, i) => `Linia ${i} — zażółć gęślą jaźń`);
    const changed = base.map((l, i) => (i % 97 === 0 ? `${l} (zmienione)` : l));
    changed.splice(1500, 0, 'nowa linia', 'i jeszcze jedna');
    const t0 = performance.now();
    const r = diffLines(base.join('\n'), changed.join('\n'));
    const ms = performance.now() - t0;
    expect(r.truncated).toBe(false);
    expect(r.added).toBe(Math.ceil(4000 / 97) + 2);
    expect(r.removed).toBe(Math.ceil(4000 / 97));
    expect(ms).toBeLessThan(3000);
  });
});

describe('diffSequences', () => {
  it('treats pure insertions as exact (not truncated)', () => {
    const r = diffSequences(['a', 'b'], ['a', 'x', 'y', 'b'], 0);
    expect(r.truncated).toBe(false);
    expect(r.ops.map((o) => o[0])).toEqual(['equal', 'add', 'add', 'equal']);
  });
});

describe('toSplitRows', () => {
  it('pairs deletions with additions inside a change block', () => {
    const r = diffLines('a\nb\nc\nd', 'a\nB\nC\nX\nd');
    const rows = toSplitRows(r.lines);
    expect(rows.map((row) => [row.left?.text ?? null, row.right?.text ?? null])).toEqual([
      ['a', 'a'],
      ['b', 'B'],
      ['c', 'C'],
      [null, 'X'],
      ['d', 'd'],
    ]);
  });
});

describe('collapseUnchanged', () => {
  const items = 'eeeeeeeeeeXeeeeeeeeeeeYeeeeeeeeee'.split('');
  const isEq = (c: string) => c === 'e';

  it('keeps context around changes and hides the rest', () => {
    const out = collapseUnchanged(items, isEq, 2);
    const shape = out.map((o) => (o.type === 'gap' ? `[${o.count}]` : o.item)).join('');
    expect(shape).toBe('[8]eeXee[7]eeYee[8]');
  });

  it('does not create a gap for a single hidden item', () => {
    const out = collapseUnchanged('XeeeeeY'.split(''), isEq, 2);
    expect(out.every((o) => o.type === 'item')).toBe(true);
  });

  it('expands gaps on request', () => {
    const first = collapseUnchanged(items, isEq, 2);
    const gap = first.find((o) => o.type === 'gap');
    expect(gap?.type).toBe('gap');
    const start = gap && gap.type === 'gap' ? gap.start : -1;
    const out = collapseUnchanged(items, isEq, 2, new Set([start]));
    expect(out.filter((o) => o.type === 'gap')).toHaveLength(2);
  });

  it('returns everything for an all-equal list without changes as one gap', () => {
    const out = collapseUnchanged(['e', 'e', 'e', 'e'], isEq, 1);
    expect(out).toEqual([{ type: 'gap', count: 4, start: 0 }]);
  });
});

describe('diffWords', () => {
  it('tokenizes Polish words, spaces and punctuation', () => {
    expect(tokenize('Zażółć, gęślą!')).toEqual(['Zażółć', ',', ' ', 'gęślą', '!']);
  });

  it('highlights only the changed words', () => {
    const r = diffWords('- [ ] Kupić mleko jutro', '- [x] Kupić mleko dziś');
    expect(r).not.toBeNull();
    expect(r!.left.filter((s) => s.changed).map((s) => s.text)).toEqual([' ', 'jutro']);
    expect(r!.right.filter((s) => s.changed).map((s) => s.text)).toEqual(['x', 'dziś']);
    expect(r!.left.map((s) => s.text).join('')).toBe('- [ ] Kupić mleko jutro');
    expect(r!.right.map((s) => s.text).join('')).toBe('- [x] Kupić mleko dziś');
  });

  it('gives up on unrelated lines', () => {
    expect(diffWords('zupełnie coś innego', 'Lorem ipsum dolor sit amet')).toBeNull();
  });
});
