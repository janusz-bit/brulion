import { describe, expect, it } from 'vitest';
import type { Locale } from '../types';
import { evaluateCalc } from './calc';
import { NBSP } from './numbers';
import { parseTable } from './table';
import { cellName, columnIndex, columnName, CYCLE, ERR, evaluateTable } from './tableEval';

function evaluate(md: string, locale: Locale = 'pl', scope?: Record<string, unknown>) {
  const model = parseTable(md);
  if (!model) throw new Error('not a table');
  return evaluateTable(model, locale, scope);
}

const SHOP = [
  '| Produkt | Cena | Ilość | Wartość |',
  '|:--|--:|:-:|--:|',
  '| Jabłka | 3,50 zł | 4 | =[Cena]*[Ilość] |',
  '| Gruszki | 5 zł | 2 | =B2*C2 |',
  '| Kawa | 20 zł | 1 | =[cena] * [ILOŚĆ] + 10% |',
  '| Suma | =sum(above) | =count(above) | =sum(D1:D3) |',
].join('\n');

describe('column helpers', () => {
  it.each<[string, number]>([
    ['A', 0],
    ['Z', 25],
    ['AA', 26],
    ['AZ', 51],
    ['BA', 52],
  ])('%s ↔ %d', (name, index) => {
    expect(columnIndex(name)).toBe(index);
    expect(columnName(index)).toBe(name);
  });

  it('names cells', () => {
    expect(cellName(0, 1)).toBe('B1');
    expect(cellName(9, 27)).toBe('AB10');
  });
});

describe('evaluateTable — references', () => {
  it('evaluates header, cell, range and direction references', () => {
    const { values, errors } = evaluate(SHOP);
    expect(values).toEqual([
      [null, null, null, '14 zł'],
      [null, null, null, '10 zł'],
      [null, null, null, '22 zł'],
      [null, '28,50 zł', '3', '46 zł'],
    ]);
    expect(errors.flat().every((e) => e === null)).toBe(true);
  });

  it('supports left/right/below and Polish direction words', () => {
    const md = '| a | b | c | d |\n|---|---|---|---|\n| 1 | 2 | 3 | =sum(left) |\n| =sum(right) | 5 | 6 | =sum(powyżej) |\n| =sum(below) | x | | |\n| 7 | | | =count(poniżej) |';
    expect(evaluate(md).values).toEqual([
      [null, null, null, '6'],
      ['17', null, null, '6'],
      ['7', null, null, null],
      [null, null, null, '0'],
    ]);
  });

  it('supports whole-column ranges (excluding the formula cell itself)', () => {
    const md = '| x | y |\n|---|---|\n| 1 | 10 |\n| 2 | 20 |\n| =sum(A:A) | =sum(A:B) |';
    expect(evaluate(md).values[2]).toEqual(['3', '36']);
  });

  it('clamps ranges to the table and reverses inverted ranges', () => {
    const md = '| x | y |\n|---|---|\n| 1 | =sum(A1:A99) |\n| 2 | =sum(A2:A1) |';
    expect(evaluate(md).values.map((r) => r[1])).toEqual(['3', '3']);
  });

  it('resolves formulas that reference later formulas', () => {
    const md = '| a | b |\n|---|---|\n| =B1 * 2 | =B2 + 1 |\n| 5 | =A2 * 10 |';
    expect(evaluate(md).values).toEqual([
      ['102', '51'],
      [null, '50'],
    ]);
  });

  it('matches headers case-insensitively, ignoring emphasis and diacritic composition', () => {
    const md = '| **Ilość** | Cena netto | Wartość |\n|---|---|---|\n| 2 | 50 | =[ilos\u0301c\u0301] * [CENA NETTO] |';
    expect(evaluate(md).values[0]![2]).toBe('100');
  });

  it('keeps matrix literals that are not headers', () => {
    const md = '| a |\n|---|\n| =sum([1, 2, 3]) |';
    expect(evaluate(md).values[0]![0]).toBe('6');
  });
});

describe('evaluateTable — values', () => {
  it('reads numbers with the locale, currencies and percentages', () => {
    const md = '| kwota | vat | brutto |\n|---|---|---|\n| 1 234,50 zł | 23% | =[kwota] + [vat] |\n| **100** | 8 % | =A2 * (1 + B2) |';
    expect(evaluate(md).values.map((r) => r[2])).toEqual([`1518,44 zł`, '108']);
  });

  it('reads English numbers', () => {
    const md = '| a | b |\n|---|---|\n| 1,500.5 | =A1 * 2 |';
    expect(evaluate(md, 'en').values[0]![1]).toBe('3,001');
  });

  it('treats empty cells as 0 and skips text in ranges', () => {
    const md = '| a | b | c |\n|---|---|---|\n| 5 | | =[a] + [b] |\n| tekst | 2 | =sum(A1:B2) |\n| =count(A1:B2) | =avg(A1:B2) | |';
    expect(evaluate(md).values).toEqual([
      [null, null, '5'],
      [null, null, '7'],
      ['2', '3,5', null],
    ]);
  });

  it('labels currencies only when they agree', () => {
    const md = '| a |\n|---|\n| 10 zł |\n| 5 EUR |\n| =sum(above) |\n| =A1 * 2 |';
    expect(evaluate(md).values.map((r) => r[0])).toEqual([null, null, '15', '20 zł']);
  });

  it('formats large results with the locale', () => {
    const md = '| a |\n|---|\n| 12 000 |\n| =A1 * 2 |';
    expect(evaluate(md).values[1]![0]).toBe(`24${NBSP}000`);
    expect(evaluate(md, 'en').values[1]![0]).toBe('24,000');
  });

  it('leaves plain cells, escaped formulas and lone "=" alone', () => {
    const md = '| a | b | c |\n|---|---|---|\n| 1 | \\=A1 | = |';
    expect(evaluate(md).values).toEqual([[null, null, null]]);
  });
});

describe('evaluateTable — functions', () => {
  it.each<[string, string]>([
    ['=sum(A1:A3)', '6'],
    ['=SUM(A1:A3)', '6'],
    ['=suma(A1:A3; 4)', '10'],
    ['=avg(A1:A3)', '2'],
    ['=average(A1:A3)', '2'],
    ['=średnia(A1:A3)', '2'],
    ['=mean(A1:A3)', '2'],
    ['=min(A1:A3)', '1'],
    ['=max(A1:A3, 10)', '10'],
    ['=count(A1:A3)', '3'],
    ['=ile(A1:A3)', '3'],
    ['=round(A1 / 3, 2)', '0,33'],
    ['=zaokr(2,345; 1)', '2,3'],
    ['=abs(A1 - A3)', '2'],
    ['=if(A3 > 2, "duże", "małe")', 'duże'],
    ['=jeżeli(A1 > 2; 1; 0)', '0'],
    ['=sqrt(A3^2)', '3'],
    ['=1,5 * 2', '3'],
    ['=A1 + 10%', '1,1'],
    ['=sum(A1:A3) # komentarz', '6'],
  ])('%s → %s', (formula, expected) => {
    const md = `| a | b |\n|---|---|\n| 1 | |\n| 2 | |\n| 3 | ${formula.replace(/\|/g, '\\|')} |`;
    expect(evaluate(md).values[2]![1]).toBe(expected);
  });

  it('uses variables from the page scope', () => {
    const { scope } = evaluateCalc('vat = 23%\nkurs = 4,30');
    const md = '| netto | brutto | eur |\n|---|---|---|\n| 100 | =[netto] + vat | =A1 / kurs |';
    expect(evaluate(md, 'pl', scope).values[0]).toEqual([null, '123', '23,25581395']);
  });
});

describe('evaluateTable — errors and cycles', () => {
  it('detects cycles', () => {
    const md = '| a | b | c |\n|---|---|---|\n| =B1 + 1 | =A1 * 2 | =C1 |\n| =A2 | 1 | =A1 + 1 |';
    const { values, errors } = evaluate(md);
    expect(values).toEqual([
      [CYCLE, CYCLE, CYCLE],
      [CYCLE, null, ERR],
    ]);
    expect(errors[0]![0]).toBe('Cykliczne odwołanie');
    expect(errors[1]![2]).toBe('Błąd w komórce A1');
  });

  it('treats a header reference to the own column as a cycle', () => {
    const md = '| a |\n|---|\n| =[a] + 1 |';
    expect(evaluate(md).values[0]![0]).toBe(CYCLE);
  });

  it('reports errors per cell', () => {
    const md = '| a | b |\n|---|---|\n| tekst | =A1 * 2 |\n| =A99 | =nope + 1 |\n| =1 + | =B1 + 1 |\n| =avg(C1:C3) | =Z1 |';
    const { values, errors } = evaluate(md);
    expect(values).toEqual([
      [null, ERR],
      [ERR, ERR],
      [ERR, ERR],
      [ERR, ERR],
    ]);
    expect(errors).toEqual([
      [null, 'Nie można zamienić „tekst” na liczbę'],
      ['Odwołanie poza tabelą: A99', 'Nieznana zmienna: nope'],
      ['Niedokończone wyrażenie', 'Błąd w komórce B1'],
      ['Brak wartości liczbowych', 'Odwołanie poza tabelą: Z1'],
    ]);
  });

  it('reports errors in English', () => {
    const md = '| a |\n|---|\n| =A1 |\n| =A9 |';
    expect(evaluate(md, 'en').errors.map((r) => r[0])).toEqual(['Circular reference', 'Reference outside the table: A9']);
  });

  it('blocks dangerous functions in formulas', () => {
    const md = '| a |\n|---|\n| =import({x: 1}) |\n| =evaluate("1") |\n| =zeros(1000, 1000) |';
    const { values, errors } = evaluate(md);
    expect(values.map((r) => r[0])).toEqual([ERR, ERR, ERR]);
    expect(errors[0]![0]).toMatch(/wyłączona/);
    expect(errors[2]![0]).toMatch(/za duży/);
  });

  it('handles ragged and empty models', () => {
    expect(evaluateTable({ header: ['a'], aligns: [null], rows: [] })).toEqual({ values: [], errors: [] });
    const r = evaluateTable({ header: ['a'], aligns: [null], rows: [['1', '=A1 + 1']] });
    expect(r.values).toEqual([[null, '2']]);
  });
});

describe('evaluateTable — review regressions', () => {
  it('reads `?:` as a conditional, not as a range', () => {
    const r = evaluate('| a | b | c |\n|---|---|---|\n| 1 | 2 | =A1 > 0 ? A1 : B1 |\n| -1 | 2 | =A2 > 0 ? A2 : B2 |');
    expect(r.values.map((row) => row[2])).toEqual(['1', '2']);
    expect(evaluate('| a | b |\n|---|---|\n| 1 | =A1 > 0 ? sum(A1:A1) : 0 |').values[0]![1]).toBe('1');
    expect(evaluate('| a | b | c |\n|---|---|---|\n| 1 | 7 | =A1 ? (A1 > 0 ? A1 : 0) : B1 |').values[0]![2]).toBe('1');
    expect(evaluate('| a | b | c |\n|---|---|---|\n| 0 | 7 | =A1 ? A1 : B1 |').values[0]![2]).toBe('7');
  });

  it('accepts long row numbers in ranges (clamped to the table)', () => {
    expect(evaluate('| a | b |\n|---|---|\n| 1 | =sum(A1:A9999999) |\n| 2 | |').values[0]![1]).toBe('3');
  });
});

describe('evaluateTable — large tables', () => {
  it('handles very many rows without exceeding the call stack', () => {
    const rows = Array.from({ length: 300_000 }, (_, i) => [String(i)]);
    rows[0] = ['=1 + 1'];
    const r = evaluateTable({ header: ['n'], aligns: [null], rows });
    expect(r.values[0]![0]).toBe('2');
    expect(r.values).toHaveLength(300_000);
  });
});

describe('evaluateTable — long reference chains', () => {
  it('evaluates a chain of 40 000 dependent formulas in order', () => {
    const rows = [['1']];
    for (let i = 1; i < 40_000; i++) rows.push([`=A${i} + 1`]);
    const r = evaluateTable({ header: ['n'], aligns: [null], rows });
    expect(r.values[39_999]![0]).toBe(`40${NBSP}000`);
    expect(r.errors.flat().filter(Boolean)).toEqual([]);
  }, 60_000);

  it('still detects cycles and independent cells', () => {
    const r = evaluate('| a | b |\n|---|---|\n| =B1 | =A1 |\n| =A3 + 1 | 5 |\n| =B2 | =B1 |');
    expect(r.values).toEqual([
      [CYCLE, CYCLE],
      ['6', null],
      ['5', ERR], // refers to a cycle without being part of it
    ]);
  });
});

describe('evaluateTable — headers with brackets', () => {
  it('references headers that contain parentheses', () => {
    const r = evaluate('| Cena (zł) | Ilość [szt] | Wartość |\n|---|---|---|\n| 12,50 | 4 | =[Cena (zł)] * [Ilość [szt]] |');
    expect(r.values[0]![2]).toBe('50');
  });

  it('keeps matrix literals that do not name a header', () => {
    expect(evaluate('| a | b |\n|---|---|\n| 1 | =sum([1, (2)]) |').values[0]![1]).toBe('3');
  });
});

describe('evaluateTable — mean is avg (spec: avg/average/mean)', () => {
  it('flattens ranges and skips text like avg', () => {
    const md = '| a | b | c |\n|---|---|---|\n| 1 | 4 | =mean(A1:A2, B1) |\n| 3 | x | =mean(A2, B2) |';
    const r = evaluate(md);
    expect(r.values[0]![2]).toBe('2,666666667');
    expect(r.values[1]![2]).toBe('3');
  });
});

describe('evaluateTable — work limits on huge tables', { timeout: 120_000 }, () => {
  it('computes early rows and reports later ones as too complex, without throwing', () => {
    const rows = Array.from({ length: 5000 }, (_, i) => [String(i), `=sum(A1:A${i + 1})`]);
    const r = evaluateTable({ header: ['n', 'suma'], aligns: [null, null], rows });
    expect(r.values[99]![1]).toBe('4950');
    expect(r.values[4999]![1]).toBe(ERR);
    expect(r.errors[4999]![1]).toBe('Obliczenie jest zbyt złożone');
  });

  it('handles dense dependency chains in both directions without storing the graph', () => {
    for (const dir of ['above', 'below']) {
      const rows = Array.from({ length: 8000 }, (_, i) => [String(i), `=sum(${dir}) + 1`]);
      const r = evaluateTable({ header: ['n', 'x'], aligns: [null, null], rows });
      const ok = r.values.filter((row) => row[1] !== ERR).length;
      expect(ok, dir).toBeGreaterThan(1000);
      expect(ok, dir).toBeLessThan(8000);
    }
  });
});
