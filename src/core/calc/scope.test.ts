import { describe, expect, it } from 'vitest';
import { evaluateInline } from './calc';
import { CALC_META } from './model';
import { findCalcBlocks, pageScope } from './scope';

const PAGE = [
  '---', // 0
  'title: Budżet domowy', // 1
  'vat: 23%', // 2
  'kurs-eur: 4,30', // 3
  'tags: [dom]', // 4
  '---', // 5
  'cena:: 100 zł', // 6
  '- ilość:: 3', // 7
  '```calc', // 8
  'razem = cena * ilość', // 9
  '```', // 10
  'Tekst `= razem + vat`', // 11
  '```js', // 12
  'x:: 5', // 13
  '```', // 14
  '```calc', // 15
  'po = 1', // 16
  '```', // 17
].join('\n');

describe('pageScope', () => {
  it('collects frontmatter, properties and calc blocks', () => {
    const scope = pageScope(PAGE);
    expect(scope.vat).toBe(0.23);
    expect(scope.kurs_eur).toBe(4.3);
    expect(scope.cena).toBe(100);
    expect(scope['ilość']).toBe(3);
    expect(scope.razem).toBe(300);
    expect(scope.po).toBe(1);
    expect(scope).not.toHaveProperty('title');
    expect(scope).not.toHaveProperty('tags');
    expect(scope).not.toHaveProperty('x');
  });

  it('feeds inline calculations, keeping percent and currency', () => {
    const scope = pageScope(PAGE, 11);
    expect(evaluateInline('razem + vat', scope)).toEqual({ value: '369 zł' });
    expect(evaluateInline('kurs_eur * 10', scope)).toEqual({ value: '43' });
  });

  it('only includes calc blocks that end before uptoLine', () => {
    expect(pageScope(PAGE, 11)).not.toHaveProperty('po');
    expect(pageScope(PAGE, 10)).not.toHaveProperty('razem');
    expect(pageScope(PAGE, 11)).toHaveProperty('razem', 300);
    expect(pageScope(PAGE, 7)).not.toHaveProperty('ilość');
    expect(pageScope(PAGE, 0)).toHaveProperty('vat', 0.23);
  });

  it('attaches metadata under CALC_META only when needed', () => {
    const scope = pageScope(PAGE) as Record<string | symbol, unknown>;
    expect(scope[CALC_META]).toMatchObject({ vat: { percent: true }, cena: { currency: { code: 'PLN' } } });
    expect(Object.getOwnPropertySymbols(pageScope('a:: 1'))).toEqual([]);
  });

  it('evaluates blocks in document order, later definitions win', () => {
    const page = 'a:: 1\n```calc\nb = a + 1\na = 10\n```\n```calc\nc = a + b\n```';
    expect(pageScope(page)).toMatchObject({ a: 10, b: 2, c: 12 });
  });

  it('handles unclosed fences', () => {
    expect(pageScope('```calc\na = 1')).toHaveProperty('a', 1);
    expect(pageScope('```calc\na = 1', 1)).not.toHaveProperty('a');
  });

  it('supports Logseq list-item fences, tildes and upper-case info strings', () => {
    expect(pageScope('- ```calc\n  a = 2\n  ```')).toHaveProperty('a', 2);
    expect(pageScope('~~~calc\nb = 3\n~~~')).toHaveProperty('b', 3);
    expect(pageScope('```Calc title\nc = 4\n```')).toHaveProperty('c', 4);
    expect(pageScope('````calc\nd = 5\n```\nd = 6\n````')).toHaveProperty('d', 6);
  });

  it('ignores calc-like content in other fences and broken lines', () => {
    const page = '```python\nx = 1\n```\n```calc\ny = 1\nbad +\nz = y + 1\n```';
    const scope = pageScope(page);
    expect(scope).not.toHaveProperty('x');
    expect(scope).toMatchObject({ y: 1, z: 2 });
  });

  it('accepts property values with decimal commas, currencies and quotes', () => {
    const scope = pageScope('---\nkwota: "1 234,50 zł"\nliczba: 7\nnazwa: abc\n---\nstawka-godzinowa:: 85,5\nopis:: tekst');
    expect(scope).toMatchObject({ kwota: 1234.5, liczba: 7, stawka_godzinowa: 85.5 });
    expect(scope).not.toHaveProperty('nazwa');
    expect(scope).not.toHaveProperty('opis');
  });

  it('respects the locale', () => {
    expect(pageScope('x:: 1,500', undefined, 'en')).toHaveProperty('x', 1500);
    expect(pageScope('x:: 1,500', undefined, 'pl')).toHaveProperty('x', 1.5);
  });

  it('treats an unterminated frontmatter as body text', () => {
    expect(pageScope('---\na: 1\nb:: 2')).toMatchObject({ b: 2 });
    expect(pageScope('---\na: 1\nb:: 2')).not.toHaveProperty('a');
  });

  it('never throws', () => {
    for (const page of ['', '---', '```calc', '```calc\n```', 'a:: ', '::', '- ```calc\n  1 +\n  ```']) {
      expect(() => pageScope(page)).not.toThrow();
    }
  });
});

describe('findCalcBlocks', () => {
  it('locates calc blocks with their content', () => {
    expect(findCalcBlocks(PAGE)).toEqual([
      { startLine: 8, endLine: 10, contentStartLine: 9, source: 'razem = cena * ilość', closed: true },
      { startLine: 15, endLine: 17, contentStartLine: 16, source: 'po = 1', closed: true },
    ]);
  });

  it('removes the fence indentation from content', () => {
    expect(findCalcBlocks('- ```calc\n  a = 1\n    b = 2\n  ```')[0]!.source).toBe('a = 1\n  b = 2');
  });

  it('reports unclosed blocks', () => {
    expect(findCalcBlocks('x\n```calc\n1 + 1\n')).toEqual([
      { startLine: 1, endLine: 3, contentStartLine: 2, source: '1 + 1\n', closed: false },
    ]);
  });

  it('ignores inline backticks and other languages', () => {
    expect(findCalcBlocks('```calc` nope\n```js\n```calc\n```')).toEqual([]);
  });
});

describe('pageScope caching', () => {
  it('returns equal but independent objects for repeated calls', () => {
    const page = '```calc\nx = 2 zł\n```';
    const a = pageScope(page);
    a.x = 99;
    const b = pageScope(page);
    expect(b.x).toBe(2);
    expect(evaluateInline('x * 2', b).value).toBe('4 zł');
    expect(pageScope(page, 0)).not.toHaveProperty('x');
    expect(pageScope(page, undefined, 'en')).toHaveProperty('x', 2);
  });
});

describe('pageScope — review regressions', () => {
  it('makes property keys available lower-cased (SYNTAX §4), keeping the written form', () => {
    const scope = pageScope('Cena:: 5\nVAT-Stawka:: 23%');
    expect(scope).toMatchObject({ Cena: 5, cena: 5, VAT_Stawka: 0.23, vat_stawka: 0.23 });
    expect(evaluateInline('cena * (1 + vat_stawka)', scope).value).toBe('6,15');
    expect(pageScope('cena:: 1\nCena:: 2')).toMatchObject({ cena: 1, Cena: 2 });
  });

  it('gives the same results for every uptoLine as a fresh evaluation would', () => {
    const page = ['a:: 1', '```calc', 'b = a + 1', '```', 'c:: 3', '```calc', 'a = 10', 'd = a + b', '```', '```calc', 'e = 5'].join('\n');
    expect(pageScope(page, 0)).toEqual({});
    expect(pageScope(page, 1)).toEqual({ a: 1 });
    expect(pageScope(page, 3)).toEqual({ a: 1 });
    expect(pageScope(page, 4)).toEqual({ a: 1, b: 2, ans: 2 });
    expect(pageScope(page, 5)).toEqual({ a: 1, b: 2, ans: 2, c: 3 });
    expect(pageScope(page, 9)).toEqual({ a: 10, b: 2, ans: 12, c: 3, d: 12 });
    expect(pageScope(page, 100)).toEqual({ a: 10, b: 2, ans: 12, c: 3, d: 12 });
    expect(pageScope(page)).toEqual({ a: 10, b: 2, ans: 5, c: 3, d: 12, e: 5 });
  });

  it('evaluates a page once for many inline positions', () => {
    const parts: string[] = [];
    for (let b = 0; b < 40; b++) parts.push('```calc', ...Array.from({ length: 8 }, (_, k) => `v${b}_${k} = ${k} * 2 + ${b}`), '```', 'tekst `= v0_1`');
    const page = parts.join('\n');
    const lines = page.split('\n').length;
    const t0 = performance.now();
    pageScope(page, 5);
    const first = performance.now() - t0;
    const t1 = performance.now();
    for (let line = 0; line < lines; line++) pageScope(page, line);
    const all = performance.now() - t1;
    // ~400 positions must cost far less than re-evaluating the page for each of them.
    expect(all).toBeLessThan(first * 20 + 200);
    expect(pageScope(page, lines)).toMatchObject({ v39_7: 53, v0_1: 2 });
  });

  it('returns independent metadata objects', () => {
    const page = 'vat:: 23%';
    const a = pageScope(page) as Record<string | symbol, Record<string, unknown>>;
    delete a[CALC_META]!.vat;
    expect(evaluateInline('100 + vat', pageScope(page)).value).toBe('123');
  });
});

describe('pageScope — many properties', () => {
  it('stays fast with thousands of numeric properties', () => {
    const page = Array.from({ length: 5000 }, (_, i) => `- blok ${i}\n  wartosc_${i}:: ${i}`).join('\n');
    const t0 = performance.now();
    const scope = pageScope(page);
    expect(scope.wartosc_4999).toBe(4999);
    expect(pageScope(page, 3)).toEqual({ wartosc_0: 0 });
    expect(performance.now() - t0).toBeLessThan(5000);
  });
});
