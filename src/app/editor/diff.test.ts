import { ChangeSet, EditorSelection, EditorState } from '@codemirror/state';
import { describe, expect, it } from 'vitest';
import { diffChanges, minimalChange } from './diff';

function apply(a: string, b: string): string {
  return ChangeSet.of(diffChanges(a, b), a.length).apply(EditorState.create({ doc: a }).doc).toString();
}

describe('diffChanges', () => {
  it('returns nothing for equal texts', () => {
    expect(diffChanges('a\nb', 'a\nb')).toEqual([]);
  });

  it('keeps edits at the top and the bottom apart', () => {
    const a = 'linia 1\nlinia 2\nlinia 3\nlinia 4\nlinia 5';
    const b = 'linia 1 zmieniona\nlinia 2\nlinia 3\nlinia 4\nlinia 5 też';
    const changes = diffChanges(a, b);
    expect(changes).toEqual([
      { from: 7, to: 7, insert: ' zmieniona' },
      { from: a.length, to: a.length, insert: ' też' },
    ]);
    // A cursor on line 3 stays on line 3.
    const state = EditorState.create({ doc: a, selection: EditorSelection.cursor(a.indexOf('linia 3') + 2) });
    const next = state.update({ changes }).state;
    expect(next.doc.toString()).toBe(b);
    expect(next.sliceDoc(next.selection.main.head - 2, next.selection.main.head + 5)).toBe('linia 3');
  });

  it('handles inserted and deleted lines, missing final newlines and Polish text', () => {
    const cases: [string, string][] = [
      ['a\nb\nc\n', 'a\nx\nb\nc\n'],
      ['a\nb\nc', 'a\nc'],
      ['', 'zażółć\ngęślą'],
      ['jaźń', ''],
      ['- [ ] a\n- [ ] b\n- [ ] c', '- [x] a ✅ 2026-10-14\n- [ ] b\n- [ ] c\n- [ ] d'],
      ['x\ny\nz', 'z\ny\nx'],
      ['😀a\nb', '😁a\nb\n😀'],
    ];
    for (const [a, b] of cases) expect(apply(a, b), JSON.stringify([a, b])).toBe(b);
  });

  it('random edits round-trip', () => {
    let seed = 7;
    const rnd = (n: number) => {
      seed = (seed * 1103515245 + 12345) % 2147483648;
      return seed % n;
    };
    const words = ['- a', '\t- b', 'ąę', '', 'x y', '1. z'];
    for (let k = 0; k < 200; k++) {
      const lines = Array.from({ length: rnd(8) }, () => words[rnd(words.length)]!);
      const a = lines.join('\n');
      const edited = [...lines];
      for (let e = rnd(4); e >= 0; e--) {
        const at = rnd(edited.length + 1);
        if (rnd(2)) edited.splice(at, 1);
        else edited.splice(at, 0, words[rnd(words.length)]!);
      }
      const b = edited.join('\n');
      expect(apply(a, b)).toBe(b);
    }
  });

  it('falls back to one change for huge rewrites', () => {
    const a = Array.from({ length: 1500 }, (_, i) => `a${i}`).join('\n');
    const b = Array.from({ length: 1500 }, (_, i) => `b${i}`).join('\n');
    expect(diffChanges(a, b)).toHaveLength(1);
    expect(apply(a, b)).toBe(b);
  });

  it('minimalChange never splits a surrogate pair', () => {
    expect(minimalChange('😀', '😁')).toEqual({ from: 0, to: 2, insert: '😁' });
  });
});
