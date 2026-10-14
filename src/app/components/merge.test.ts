import { describe, expect, it } from 'vitest';
import { merge3, splitText } from '@/core/sync';
import { buildMerged, conflictIndexes, hunksFromDiff3, manualSeed, resolvedLines, unresolvedCount, type MergeHunk } from './merge';

const HUNKS: MergeHunk[] = [
  { kind: 'ok', lines: ['# Zakupy', '- mleko'] },
  { kind: 'conflict', base: ['- chleb'], local: ['- chleb żytni'], remote: ['- chleb razowy'] },
  { kind: 'ok', lines: ['- masło'] },
  { kind: 'conflict', base: [], local: ['- jajka'], remote: [] },
];

describe('merge helpers', () => {
  it('finds conflicts and counts unresolved ones', () => {
    expect(conflictIndexes(HUNKS)).toEqual([1, 3]);
    expect(unresolvedCount(HUNKS, {})).toBe(2);
    expect(unresolvedCount(HUNKS, { 1: { pick: 'local' } })).toBe(1);
  });

  it('returns null until every conflict is resolved', () => {
    expect(buildMerged(HUNKS, {})).toBeNull();
    expect(buildMerged(HUNKS, { 1: { pick: 'local' } })).toBeNull();
  });

  it('builds the text from per-hunk choices', () => {
    expect(buildMerged(HUNKS, { 1: { pick: 'local' }, 3: { pick: 'remote' } })).toBe('# Zakupy\n- mleko\n- chleb żytni\n- masło\n');
    expect(buildMerged(HUNKS, { 1: { pick: 'remote' }, 3: { pick: 'local' } })).toBe(
      '# Zakupy\n- mleko\n- chleb razowy\n- masło\n- jajka\n',
    );
    expect(buildMerged(HUNKS, { 1: { pick: 'both' }, 3: { pick: 'both' } })).toBe(
      '# Zakupy\n- mleko\n- chleb żytni\n- chleb razowy\n- masło\n- jajka\n',
    );
  });

  it('supports manual text, CRLF and no final newline', () => {
    const res = { 1: { pick: 'manual' as const, text: '- chleb (dowolny)\n- bułki\n' }, 3: { pick: 'manual' as const, text: '' } };
    expect(buildMerged(HUNKS, res, { eol: '\r\n', finalNewline: false })).toBe(
      '# Zakupy\r\n- mleko\r\n- chleb (dowolny)\r\n- bułki\r\n- masło',
    );
  });

  it('returns an empty string when everything resolves to nothing', () => {
    expect(buildMerged([{ kind: 'conflict', base: ['a'], local: [], remote: [] }], { 0: { pick: 'local' } })).toBe('');
  });

  it('seeds manual editing from the current choice or both versions', () => {
    const h = HUNKS[1] as Extract<MergeHunk, { kind: 'conflict' }>;
    expect(manualSeed(h, undefined)).toBe('- chleb żytni\n- chleb razowy');
    expect(manualSeed(h, { pick: 'remote' })).toBe('- chleb razowy');
    expect(manualSeed(h, { pick: 'manual', text: 'x' })).toBe('x');
    expect(resolvedLines(h, { pick: 'manual', text: 'a\r\nb' })).toEqual(['a', 'b']);
  });

  it('converts node-diff3 diff3Merge regions', () => {
    const hunks = hunksFromDiff3([
      { ok: ['a'] },
      { ok: ['b'] },
      { conflict: { a: ['mine'], o: ['base'], b: ['theirs'] } },
      { ok: ['c'] },
    ]);
    expect(hunks).toEqual([
      { kind: 'ok', lines: ['a', 'b'] },
      { kind: 'conflict', base: ['base'], local: ['mine'], remote: ['theirs'] },
      { kind: 'ok', lines: ['c'] },
    ]);
  });
});

describe('MergeView text with the core three-way merge', () => {
  // Same hunk shape as @/core/sync's merge3; the shell passes eol / final newline of the file.
  const pick = (hunks: readonly MergeHunk[], p: 'local' | 'remote' | 'both') =>
    Object.fromEntries(conflictIndexes(hunks).map((i) => [i, { pick: p }]));

  const base = '# Plan\n- siłownia 18:00\n- zakupy\n- dentysta\n';
  const local = '# Plan\n- siłownia 19:00\n- zakupy\n- dentysta\n- kino\n';
  const remote = '# Plan\n- siłownia 20:00\n- zakupy: chleb\n- dentysta\n';

  it('choosing "mine" everywhere gives merge3 text (which keeps local in conflicts)', () => {
    const m = merge3(base, local, remote);
    expect(m.clean).toBe(false);
    const { eol, finalNewline } = splitText(local);
    expect(buildMerged(m.hunks, pick(m.hunks, 'local'), { eol: eol ?? '\n', finalNewline })).toBe(m.text);
  });

  it('builds remote / both choices around auto-merged lines, with CRLF', () => {
    const crlf = (s: string) => s.replace(/\n/g, '\r\n');
    const m = merge3(crlf(base), crlf(local), crlf(remote));
    const { eol, finalNewline } = splitText(crlf(local));
    expect(eol).toBe('\r\n');
    const text = buildMerged(m.hunks, pick(m.hunks, 'remote'), { eol: eol!, finalNewline });
    // Every line ends with CRLF; non-conflicting changes from both sides are kept.
    expect(text).not.toMatch(/[^\r]\n/);
    expect(text?.split('\r\n')).toContain('- kino');
    expect(text?.split('\r\n')).not.toContain('- siłownia 19:00');
    const both = buildMerged(m.hunks, pick(m.hunks, 'both'), { eol: eol!, finalNewline })!;
    for (const line of ['- siłownia 19:00', '- kino']) expect(both.split('\r\n')).toContain(line);
  });

  it('keeps a missing final newline and blank lines from manual edits', () => {
    const m = merge3('a\nb', 'a\nB1', 'a\nB2');
    const { eol, finalNewline } = splitText('a\nB1');
    expect(finalNewline).toBe(false);
    const i = conflictIndexes(m.hunks)[0]!;
    expect(buildMerged(m.hunks, { [i]: { pick: 'manual', text: 'B1\n\nB2\n' } }, { eol: eol ?? '\n', finalNewline })).toBe(
      'a\nB1\n\nB2',
    );
  });
});
