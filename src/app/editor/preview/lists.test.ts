import { Text } from '@codemirror/state';
import { describe, expect, it } from 'vitest';
import { outlineLines, taskMarker, type ItemInfo, type ContinuationInfo } from './lists';
import { parseListLine } from '../outliner';

function doc(src: string): Text {
  return Text.of(src.split('\n'));
}

function items(src: string, from = 1, to = Infinity, isCode?: (n: number) => boolean) {
  const d = doc(src);
  return outlineLines(d, from, Math.min(to, d.lines), isCode);
}

describe('outlineLines', () => {
  const TABS = ['- Demontaż', '\t- [x] zmywarka ✅ 2026-10-10', '\t- [ ] blaty', '\t\t- wymiary', '- Instalacje', '\tdopisek do instalacji', 'Akapit', '- nowa lista'].join('\n');

  it('computes depth from tab indentation', () => {
    const m = items(TABS);
    expect([1, 2, 3, 4, 5, 8].map((n) => (m.get(n) as ItemInfo).depth)).toEqual([0, 1, 1, 2, 0, 0]);
    expect(m.has(7)).toBe(false);
  });

  it('marks items with children (foldable)', () => {
    const m = items(TABS);
    expect((m.get(1) as ItemInfo).hasChildren).toBe(true);
    expect((m.get(2) as ItemInfo).hasChildren).toBe(false);
    expect((m.get(3) as ItemInfo).hasChildren).toBe(true);
    expect((m.get(5) as ItemInfo).hasChildren).toBe(true);
  });

  it('finds continuation lines with their owner and structural whitespace', () => {
    const c = items(TABS).get(6) as ContinuationInfo;
    expect(c).toMatchObject({ kind: 'cont', owner: 5, depth: 0, wsLength: 1, ownerContinues: false });
  });

  it('works for space indentation (child past the parent marker)', () => {
    const m = items(['- a', '  - b', '    - c', '  - d', '    text of d'].join('\n'));
    expect([1, 2, 3, 4].map((n) => (m.get(n) as ItemInfo).depth)).toEqual([0, 1, 2, 1]);
    expect(m.get(5)).toMatchObject({ kind: 'cont', owner: 4, depth: 1, wsLength: 4 });
  });

  it('seeds the depth from lines above the range', () => {
    const m = items(TABS, 4, 4);
    expect((m.get(4) as ItemInfo).depth).toBe(2);
    expect(m.size).toBe(1);
  });

  it('never treats code lines as items, but as continuation of the owner', () => {
    const src = ['- przykład', '\t```yaml', '\t- nie lista', '\t```', '- dalej'].join('\n');
    const m = items(src, 1, Infinity, (n) => n >= 2 && n <= 4);
    expect(m.get(3)).toMatchObject({ kind: 'cont', owner: 1, wsLength: 1, ownerContinues: true });
    expect((m.get(5) as ItemInfo).depth).toBe(0);
  });

  it('keeps ordered items and their numbers', () => {
    const m = items('1. pierwszy\n2. drugi\n\t1. pod');
    expect((m.get(2) as ItemInfo).item.number).toBe(2);
    expect((m.get(3) as ItemInfo).depth).toBe(1);
  });

  it('blank lines do not break the outline', () => {
    const m = items('- a\n\n\t- b');
    expect((m.get(3) as ItemInfo).depth).toBe(1);
  });
});

describe('taskMarker', () => {
  const marker = (line: string) => taskMarker(line, parseListLine(line)!);

  it('reads all checkbox states', () => {
    expect(marker('- [ ] a')).toMatchObject({ kind: 'checkbox', status: 'todo', from: 2, to: 5, end: 6 });
    expect(marker('- [/] a')?.status).toBe('doing');
    expect(marker('- [x] a')?.status).toBe('done');
    expect(marker('- [X] a')?.status).toBe('done');
    expect(marker('- [-] a')?.status).toBe('cancelled');
    expect(marker('\t- [>] a')).toMatchObject({ status: 'deferred', from: 3 });
  });

  it('reads Logseq keywords', () => {
    expect(marker('- TODO zadzwonić')).toMatchObject({ kind: 'keyword', status: 'todo', marker: 'TODO', from: 2, to: 6, end: 7 });
    expect(marker('- DOING pisać')?.status).toBe('doing');
    expect(marker('- NOW pisać')?.status).toBe('doing');
    expect(marker('- LATER x')?.status).toBe('todo');
    expect(marker('- DONE x')?.status).toBe('done');
    expect(marker('- CANCELED x')?.status).toBe('cancelled');
    expect(marker('- WAITING na Anię')?.status).toBe('deferred');
  });

  it('ignores plain items and lower-case words', () => {
    expect(marker('- todo: lista')).toBeNull();
    expect(marker('- TODOS')).toBeNull();
    expect(marker('- zwykły punkt')).toBeNull();
  });
});
