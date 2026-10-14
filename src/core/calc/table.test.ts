import { describe, expect, it } from 'vitest';
import { displayWidth, findTables, parseDelimiterRow, parseTable, serializeTable, splitTableRow, type TableModel } from './table';

describe('splitTableRow', () => {
  it.each<[string, string[]]>([
    ['| a | b |', ['a', 'b']],
    ['a | b', ['a', 'b']],
    ['|a|b', ['a', 'b']],
    ['| a \\| b | c |', ['a | b', 'c']],
    ['||  x |', ['', 'x']],
    ['| zażółć | gęślą |', ['zażółć', 'gęślą']],
    ['  | indented |  ', ['indented']],
  ])('%j', (line, cells) => {
    expect(splitTableRow(line)).toEqual(cells);
  });
});

describe('parseDelimiterRow', () => {
  it.each<[string, ReturnType<typeof parseDelimiterRow>]>([
    ['|---|---|', [null, null]],
    ['| :-- | --: | :-: | - |', ['left', 'right', 'center', null]],
    ['---|---', [null, null]],
    [':---', ['left']],
    ['| - |', [null]],
    ['- foo', null],
    ['- | -', null],
    ['|---|x|', null],
    ['| --- | | --- |', null],
    ['', null],
    ['|', null],
    ['-', null],
  ])('%j', (line, aligns) => {
    expect(parseDelimiterRow(line)).toEqual(aligns);
  });
});

describe('parseTable', () => {
  it('parses header, alignment and rows', () => {
    const md = '| Produkt | Cena | Ilość |\n|:--|--:|:-:|\n| Jabłka | 3,50 zł | 4 |\n| Gruszki | 5 zł | 2 |';
    expect(parseTable(md)).toEqual({
      header: ['Produkt', 'Cena', 'Ilość'],
      aligns: ['left', 'right', 'center'],
      rows: [
        ['Jabłka', '3,50 zł', '4'],
        ['Gruszki', '5 zł', '2'],
      ],
    });
  });

  it('pads short rows and truncates long rows', () => {
    const t = parseTable('a | b | c\n--|--|--\n1\n1 | 2 | 3 | 4')!;
    expect(t.rows).toEqual([
      ['1', '', ''],
      ['1', '2', '3'],
    ]);
  });

  it('unescapes pipes and keeps empty cells', () => {
    expect(parseTable('| x | y |\n|---|---|\n| a \\| b | |')!.rows).toEqual([['a | b', '']]);
  });

  it('accepts a table without body, CRLF and leading blank lines', () => {
    expect(parseTable('\n\n| a |\r\n| --- |\r\n')).toEqual({ header: ['a'], aligns: [null], rows: [] });
  });

  it('stops at the first blank line', () => {
    expect(parseTable('| a |\n|---|\n| 1 |\n\n| 2 |')!.rows).toEqual([['1']]);
  });

  it.each([
    ['no pipe in header', 'a\n---'],
    ['cell count mismatch', '| a | b |\n|---|\n| 1 | 2 |'],
    ['no delimiter', '| a | b |\n| 1 | 2 |'],
    ['empty', ''],
    ['paragraph', 'Zwykły tekst'],
  ])('rejects %s', (_name, md) => {
    expect(parseTable(md)).toBeNull();
  });
});

describe('displayWidth', () => {
  it.each<[string, number]>([
    ['abc', 3],
    ['zażółć', 6],
    ['ilość', 5],
    ['日本語', 6],
    ['☕', 2],
    ['⏰ 9:00', 7],
    ['👨‍👩‍👧', 2],
    ['🇵🇱', 2],
    ['©', 1],
    ['', 0],
  ])('%j → %d', (s, w) => {
    expect(displayWidth(s)).toBe(w);
  });
});

describe('serializeTable', () => {
  it('aligns columns and keeps alignment markers', () => {
    const model: TableModel = {
      header: ['Produkt', 'Cena', 'Ilość', 'X'],
      aligns: ['left', 'right', 'center', null],
      rows: [
        ['Jabłka', '3,50 zł', '4', ''],
        ['Kawa ☕', '20 zł', '10', 'x'],
      ],
    };
    expect(serializeTable(model)).toBe(
      [
        '| Produkt |    Cena | Ilość | X   |',
        '| :------ | ------: | :---: | --- |',
        '| Jabłka  | 3,50 zł |   4   |     |',
        '| Kawa ☕ |   20 zł |  10   | x   |',
      ].join('\n'),
    );
  });

  it('escapes pipes and line breaks in cells', () => {
    const out = serializeTable({ header: ['a|b'], aligns: [null], rows: [['x\ny']] });
    expect(out).toBe('| a\\|b   |\n| ------ |\n| x<br>y |');
  });

  it('extends the header for longer rows and handles empty tables', () => {
    expect(serializeTable({ header: ['a'], aligns: [], rows: [['1', '2']] })).toBe('| a   |     |\n| --- | --- |\n| 1   | 2   |');
    expect(serializeTable({ header: [], aligns: [], rows: [] })).toBe('');
  });

  it('round-trips through parseTable', () => {
    const model: TableModel = {
      header: ['Zadanie', 'Koszt | netto', '日本'],
      aligns: [null, 'right', 'center'],
      rows: [
        ['Malowanie', '1 200 zł', '=B1*2'],
        ['Żółć', '', '✅'],
      ],
    };
    expect(parseTable(serializeTable(model))).toEqual(model);
    expect(serializeTable(parseTable(serializeTable(model))!)).toBe(serializeTable(model));
  });
});

describe('findTables', () => {
  it('finds tables with offsets and lines', () => {
    const content = 'Tekst\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\npo';
    const [t] = findTables(content);
    expect(t).toEqual({ from: 7, to: 36, startLine: 2, endLine: 4, indent: '' });
    expect(content.slice(t!.from, t!.to)).toBe('| a | b |\n|---|---|\n| 1 | 2 |');
  });

  it('computes offsets with CRLF line endings', () => {
    const content = 'x\r\n| a |\r\n| - |\r\n| 1 |\r\n\r\nend';
    const [t] = findTables(content);
    expect(content.slice(t!.from, t!.to)).toBe('| a |\r\n| - |\r\n| 1 |');
    expect([t!.startLine, t!.endLine]).toEqual([1, 3]);
  });

  it('skips fenced code and frontmatter', () => {
    const content = '---\na: | b |\n---\n```\n| a | b |\n|---|---|\n```\n~~~md\n| c |\n|---|\n~~~\n| real |\n|------|';
    const tables = findTables(content);
    expect(tables).toHaveLength(1);
    expect(tables[0]!.startLine).toBe(11);
  });

  it('finds indented tables inside list items', () => {
    const content = '- lista\n  | x | y |\n  |---|---|\n  | 1 | 2 |\n- dalej';
    const [t] = findTables(content);
    expect(t).toMatchObject({ startLine: 1, endLine: 3, indent: '  ' });
  });

  it('ends tables at blank lines and other blocks, but continues on pipe-less rows', () => {
    const content = '| a |\n|---|\n| 1 |\ntekst\n# Nagłówek\n| b |\n|---|\n- item';
    expect(findTables(content).map((t) => [t.startLine, t.endLine])).toEqual([
      [0, 3],
      [5, 6],
    ]);
  });

  it('finds several tables and tables without body', () => {
    const content = '| a |\n|---|\n\n| b | c |\n|:-|-:|\n| 1 | 2 |';
    expect(findTables(content).map((t) => [t.startLine, t.endLine])).toEqual([
      [0, 1],
      [3, 5],
    ]);
  });

  it('ignores setext headings and lone pipes', () => {
    expect(findTables('Tytuł | x\n---')).toEqual([]);
    expect(findTables('a | b\n')).toEqual([]);
    expect(findTables('')).toEqual([]);
  });
});

describe('findTables — review regressions', () => {
  it('finds Logseq-style tables that start on a list item line', () => {
    const content = '- zakupy\n- | Produkt | Cena |\n  |---|---|\n  | Chleb | 4 zł |\n- dalej';
    const [t] = findTables(content);
    expect(t).toMatchObject({ startLine: 1, endLine: 3, indent: '  ' });
    expect(content.slice(t!.from, t!.to)).toBe('| Produkt | Cena |\n  |---|---|\n  | Chleb | 4 zł |');
    expect(parseTable(content.slice(t!.from, t!.to))!.rows).toEqual([['Chleb', '4 zł']]);
  });

  it('finds tables in nested, tab-indented list items', () => {
    const content = '- rodzic\n\t- | a | b |\n\t  | - | - |\n\t  | 1 | 2 |';
    const [t] = findTables(content);
    expect(t).toMatchObject({ startLine: 1, endLine: 3, indent: '\t  ' });
    expect(content.slice(t!.from, t!.to)).toBe('| a | b |\n\t  | - | - |\n\t  | 1 | 2 |');
  });

  it('points `from` at the table text, so it can be replaced with `indent` re-applied', () => {
    const content = '- lista\n  | x | y |\n  |---|---|\n  | 1 | 2 |';
    const [t] = findTables(content);
    expect(content.slice(t!.from, t!.to)).toBe('| x | y |\n  |---|---|\n  | 1 | 2 |');
    const pretty = serializeTable(parseTable(content.slice(t!.from, t!.to))!).split('\n').join(`\n${t!.indent}`);
    expect(content.slice(0, t!.from) + pretty + content.slice(t!.to)).toBe('- lista\n  | x   | y   |\n  | --- | --- |\n  | 1   | 2   |');
  });

  it('does not take list items without a table for tables', () => {
    expect(findTables('- a | b\n- c | d')).toEqual([]);
    expect(findTables('- | a |\n- |---|')).toEqual([]);
    expect(findTables('1. | a | b |\n   |---|---|\n   | 1 | 2 |')).toHaveLength(1);
  });
});

describe('large tables — review regressions', () => {
  it('serialises tables with very many rows', () => {
    const rows = Array.from({ length: 300_000 }, (_, i) => [String(i)]);
    const text = serializeTable({ header: ['n'], aligns: [null], rows });
    expect(text.split('\n')).toHaveLength(300_002);
    expect(text.split('\n')[2]).toBe('| 0      |');
  });
});
