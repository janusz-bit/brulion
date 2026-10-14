import { describe, expect, it } from 'vitest';
import { isTableStart, matchListMarker, nodeAtLine, parseStructure, preOrder, type Node } from './structure';

/** Compact outline: kind@line-endLine(own) with children. */
function outline(src: string): unknown[] {
  const map = (n: Node): unknown => {
    const label = `${n.kind}@${n.line}-${n.endLine}${n.ownEndLine !== n.endLine ? `(${n.ownEndLine})` : ''}:d${n.depth}`;
    return n.children.length ? [label, n.children.map(map)] : label;
  };
  return parseStructure(src).roots.map(map);
}

describe('list items and outline', () => {
  it('nests by indentation with spaces', () => {
    const src = '- a\n  - b\n    - c\n  - d\n- e';
    expect(outline(src)).toEqual([
      ['listItem@0-3(0):d0', [['listItem@1-2(1):d1', ['listItem@2-2:d2']], 'listItem@3-3:d1']],
      'listItem@4-4:d0',
    ]);
  });

  it('nests by tabs (tab = one level) and tolerates mixed indentation', () => {
    expect(outline('- a\n\t- b\n\t\t- c\n- d')).toEqual([['listItem@0-2(0):d0', [['listItem@1-2(1):d1', ['listItem@2-2:d2']]]], 'listItem@3-3:d0']);
    expect(outline('- a\n  - b\n\t- c\n - d')).toEqual([
      ['listItem@0-3(0):d0', [['listItem@1-2(1):d1', ['listItem@2-2:d2']], 'listItem@3-3:d1']],
    ]);
  });

  it('treats a child as nested once indented past the parent marker', () => {
    expect(outline('- a\n - b')).toEqual([['listItem@0-1(0):d0', ['listItem@1-1:d1']]]);
    expect(outline('1. a\n2. b\n   1) c\n+ d\n* e')).toEqual([
      'listItem@0-0:d0',
      ['listItem@1-2(1):d0', ['listItem@2-2:d1']],
      'listItem@3-3:d0',
      'listItem@4-4:d0',
    ]);
  });

  it('attaches continuation lines and lazy lines to the right item', () => {
    const src = '- a\n  more a\n  - b\nlazy b\n  back in a\n- c';
    const st = parseStructure(src);
    const a = st.roots[0]!;
    expect(a.own.map((l) => l.view.line)).toEqual([0, 1, 4]);
    expect(a.children[0]!.own.map((l) => l.view.line)).toEqual([2, 3]);
    expect(a.ownEndLine).toBe(4);
    expect(a.endLine).toBe(4);
  });

  it('keeps blank lines inside items only when content follows', () => {
    const st = parseStructure('- a\n\n  para\n\n- b\n\n\ntext');
    expect(st.roots.map((n) => `${n.kind}@${n.line}-${n.endLine}`)).toEqual(['listItem@0-2', 'listItem@4-4', 'paragraph@7-7']);
    expect(st.roots[0]!.own.map((l) => l.role)).toEqual(['text', 'blank', 'text']);
  });

  it('ends lists at unindented blocks', () => {
    expect(outline('- a\n# H\n- b\n```\ncode\n```\n- c\n> q')).toEqual([
      'listItem@0-0:d0',
      'heading@1-1:d0',
      'listItem@2-2:d0',
      'code@3-5:d0',
      'listItem@6-6:d0',
      'quote@7-7:d0',
    ]);
  });

  it('keeps fenced code inside list items (not parsed), closed or by dedent', () => {
    const src = '- item\n  ```js\n  - [ ] not a task\n\n  ```\n  after\n- next';
    const st = parseStructure(src);
    const item = st.roots[0]!;
    expect(item.own.map((l) => l.role)).toEqual(['text', 'fence', 'code', 'code', 'fence', 'text']);
    expect(item.children).toEqual([]);
    expect(st.roots).toHaveLength(2);
    const unclosed = parseStructure('- a\n  ```\n  code\n- b');
    expect(unclosed.roots.map((n) => n.line)).toEqual([0, 3]);
  });

  it('handles an item whose first line opens a fence', () => {
    const st = parseStructure('- ```\n  - x\n  ```\n- y');
    expect(st.roots[0]!.own.map((l) => l.role)).toEqual(['fence', 'code', 'fence']);
    expect(st.roots).toHaveLength(2);
  });

  it('does not confuse thematic breaks with list items', () => {
    expect(outline('- - -\n* * *\n- x')).toEqual(['hr@0-0:d0', 'hr@1-1:d0', 'listItem@2-2:d0']);
  });

  it('matchListMarker details', () => {
    expect(matchListMarker('  - [ ] x')).toMatchObject({ marker: '-', markerEnd: 3, contentStart: 4, contentCol: 4 });
    expect(matchListMarker('10) x')).toMatchObject({ marker: '10)', contentStart: 4 });
    expect(matchListMarker('-')).toMatchObject({ contentStart: 1, contentCol: 2 });
    expect(matchListMarker('-x')).toBeNull();
    expect(matchListMarker('1.5 kg')).toBeNull();
  });
});

describe('leaf blocks', () => {
  it('splits paragraphs at blank lines and interrupting blocks', () => {
    expect(outline('a\nb\n\nc\n- item')).toEqual(['paragraph@0-1:d0', 'paragraph@3-3:d0', 'listItem@4-4:d0']);
    // An ordered item not starting at 1 does not interrupt a paragraph (CommonMark).
    expect(outline('W roku\n2024. było')).toEqual(['paragraph@0-1:d0']);
  });

  it('parses ATX and setext headings', () => {
    const st = parseStructure('# One\n## Two ##\nSetext\n===\nAlso\n---\n#nottag');
    expect(st.roots.map((n) => [n.kind, n.headingLevel, n.line, n.endLine])).toEqual([
      ['heading', 1, 0, 0],
      ['heading', 2, 1, 1],
      ['heading', 1, 2, 3],
      ['heading', 2, 4, 5],
      ['paragraph', undefined, 6, 6],
    ]);
  });

  it('parses fenced code with language, ~~~ and unclosed fences', () => {
    const st = parseStructure('```Calc\nx = 1\n```\n~~~query {.x}\ntype:tasks\n~~~\n```\nopen\n\n');
    expect(st.roots.map((n) => [n.kind, n.lang, n.line, n.endLine])).toEqual([
      ['code', 'calc', 0, 2],
      ['code', 'query', 3, 5],
      ['code', undefined, 6, 7],
    ]);
  });

  it('parses GFM tables and ends them at a line without pipes', () => {
    expect(outline('| a | b |\n|---|:-:|\n| 1 | 2 |\nafter')).toEqual(['table@0-2:d0', 'paragraph@3-3:d0']);
    expect(outline('a | b\n--- | ---\n1 | 2')).toEqual(['table@0-2:d0']);
    expect(outline('text\n| a | b |\n|---|---|')).toEqual(['paragraph@0-0:d0', 'table@1-2:d0']);
    expect(isTableStart('| a | b |', '|---|')).toBe(false);
  });

  it('parses blockquotes with lazy continuation', () => {
    expect(outline('> a\n> b\nlazy\n\n> c')).toEqual(['quote@0-2:d0', 'quote@4-4:d0']);
  });

  it('parses callouts as containers with child blocks', () => {
    const st = parseStructure('> [!todo]- Zakupy\n> - [ ] mleko\n>   - [ ] chleb\n> \n> > [!note] inner\n> > text\n\nafter');
    const callout = st.roots[0]!;
    expect(callout).toMatchObject({ kind: 'callout', calloutType: 'todo', calloutFold: '-', line: 0, endLine: 5, ownEndLine: 0 });
    expect(callout.children.map((c) => [c.kind, c.line, c.depth])).toEqual([
      ['listItem', 1, 1],
      ['callout', 4, 1],
    ]);
    expect(callout.children[0]!.children[0]).toMatchObject({ kind: 'listItem', line: 2, depth: 2 });
    expect(callout.children[1]!.children[0]).toMatchObject({ kind: 'paragraph', line: 5, depth: 2 });
    expect(st.roots[1]).toMatchObject({ kind: 'paragraph', line: 7 });
    // Without a blank line, a following line is a lazy continuation (CommonMark).
    expect(parseStructure('> [!note] t\n> body\nlazy').roots[0]!.endLine).toBe(2);
    // Offsets inside callouts point past the "> " prefix.
    const item = callout.children[0]!;
    expect(item.own[0]!.view.text).toBe('- [ ] mleko');
    expect(item.own[0]!.view.offset).toBe(st.lines.starts[1]! + 2);
  });

  it('parses html blocks', () => {
    expect(outline('<div>\n[[x]]\n</div>\n\n<!-- c\n\nstill -->\ntext\n\n<span>inline</span> text')).toEqual([
      'html@0-2:d0',
      'html@4-6:d0',
      'paragraph@7-7:d0',
      'paragraph@9-9:d0',
    ]);
  });

  it('attaches a standalone ^id line to the preceding table / code block', () => {
    const st = parseStructure('| a | b |\n|---|---|\n| 1 | 2 |\n^tbl\n\n```\nx\n```\n\n^code1\n\n- item\n\n^para');
    expect(st.roots.map((n) => [n.kind, n.trailingId, n.endLine])).toEqual([
      ['table', 'tbl', 3],
      ['code', 'code1', 9],
      ['listItem', undefined, 11],
      ['paragraph', undefined, 13],
    ]);
  });
});

describe('frontmatter and page properties', () => {
  it('creates frontmatter blocks for YAML and leading key:: lines', () => {
    const st = parseStructure('---\na: 1\n---\n\ntitle:: X\ntags:: a\n\n- first');
    expect(st.roots.map((n) => [n.kind, n.line, n.endLine])).toEqual([
      ['frontmatter', 0, 2],
      ['frontmatter', 4, 5],
      ['listItem', 7, 7],
    ]);
  });

  it('only treats key:: lines at the very top as page properties', () => {
    const st = parseStructure('- a\nkey:: v');
    expect(st.roots.map((n) => n.kind)).toEqual(['listItem']);
  });
});

describe('comments and helpers', () => {
  it('ignores structure inside %% comments', () => {
    expect(outline('a\n\n%%\n- [ ] hidden\n%%\n\nb')).toEqual(['paragraph@0-0:d0', 'paragraph@6-6:d0']);
  });

  it('nodeAtLine finds the innermost block', () => {
    const st = parseStructure('- a\n  - b\n  cont a\n- c');
    expect(nodeAtLine(st.roots, 1)!.line).toBe(1);
    expect(nodeAtLine(st.roots, 2)!.line).toBe(0);
    expect(nodeAtLine(st.roots, 3)!.line).toBe(3);
    expect(nodeAtLine(st.roots, 9)).toBeNull();
  });

  it('preOrder visits parents before children', () => {
    const st = parseStructure('- a\n  - b\n- c');
    const seen: number[] = [];
    preOrder(st.roots, (n) => seen.push(n.line));
    expect(seen).toEqual([0, 1, 2]);
  });

  it('handles very deep outlines without recursion limits', () => {
    const src = Array.from({ length: 3000 }, (_, i) => ' '.repeat(i) + '- x').join('\n');
    const st = parseStructure(src);
    expect(st.roots).toHaveLength(1);
    expect(st.roots[0]!.endLine).toBe(2999);
  });
});
