import { describe, expect, it } from 'vitest';
import { parseTable } from '@/core/calc';
import { applyTableAction, tableCellSpans, tableIndent } from './tables';

const TABLE = ['| Pozycja | Ilość | Cena |', '| --- | --: | --: |', '| Płytki | 6 | 89 |', '| Klej |  | =A1*2 |'].join('\n');

function cellText(src: string, r: number, c: number): string {
  const s = tableCellSpans(src)[r]![c]!;
  return src.slice(s.from, s.to);
}

describe('tableCellSpans', () => {
  it('maps every cell to its source text (header row 0, delimiter skipped)', () => {
    const spans = tableCellSpans(TABLE);
    expect(spans).toHaveLength(3);
    expect(spans.map((r) => r.length)).toEqual([3, 3, 3]);
    expect(cellText(TABLE, 0, 0)).toBe('Pozycja');
    expect(cellText(TABLE, 0, 2)).toBe('Cena');
    expect(cellText(TABLE, 1, 0)).toBe('Płytki');
    expect(cellText(TABLE, 2, 2)).toBe('=A1*2');
  });

  it('places empty cells inside their pipes', () => {
    const s = tableCellSpans(TABLE)[2]![1]!;
    expect(s.from).toBe(s.to);
    const line = TABLE.split('\n')[3]!;
    const lineStart = TABLE.lastIndexOf('\n') + 1;
    expect(line.slice(0, s.from - lineStart)).toBe('| Klej | ');
  });

  it('works without outer pipes, with escaped pipes and indented rows', () => {
    const src = 'a | b\n--- | ---\n  x \\| y | z';
    expect(cellText(src, 0, 1)).toBe('b');
    expect(cellText(src, 1, 0)).toBe('x \\| y');
    expect(cellText(src, 1, 1)).toBe('z');
    expect(tableIndent(src)).toBe('');
    expect(tableIndent('| a |\n\t| --- |\n\t| b |')).toBe('\t');
  });
});

describe('applyTableAction', () => {
  it('adds a row and returns the cursor in its first cell', () => {
    const res = applyTableAction(TABLE, 'addRow')!;
    const model = parseTable(res.text)!;
    expect(model.rows).toHaveLength(3);
    expect(model.rows[2]).toEqual(['', '', '']);
    const lastLineStart = res.text.lastIndexOf('\n') + 1;
    expect(res.cursor).toBeGreaterThan(lastLineStart);
    expect(res.text.slice(lastLineStart, res.cursor!)).toBe('| ');
  });

  it('adds a column (empty header) and puts the cursor there', () => {
    const res = applyTableAction(TABLE, 'addColumn')!;
    const model = parseTable(res.text)!;
    expect(model.header).toEqual(['Pozycja', 'Ilość', 'Cena', '']);
    expect(model.aligns).toEqual([null, 'right', 'right', null]);
    expect(model.rows.every((r) => r.length === 4)).toBe(true);
    const firstLine = res.text.split('\n')[0]!;
    expect(res.cursor).toBeLessThan(firstLine.length);
    expect(firstLine.slice(0, res.cursor!).trimEnd().endsWith('| Cena |') || firstLine.slice(0, res.cursor!).includes('Cena')).toBe(true);
  });

  it('aligns (pretty-prints) and keeps formulas, Polish text and indentation', () => {
    const src = '| a | ąę |\n\t|---|---|\n\t| 1 | =A1*2 |';
    const res = applyTableAction(src, 'align')!;
    expect(res.cursor).toBeNull();
    const lines = res.text.split('\n');
    expect(lines[0]).toBe('| a   | ąę    |');
    expect(lines[1]).toBe('\t| --- | ----- |');
    expect(lines[2]).toBe('\t| 1   | =A1*2 |');
  });

  it('returns null for text that is not a table', () => {
    expect(applyTableAction('nie tabela', 'addRow')).toBeNull();
  });
});
