/**
 * GFM tables:
 *
 * * formula cells (`=sum(above)`, `=[Cena]*[Ilość]`, SYNTAX §9) show their computed value; the
 *   cell gets `class="formula"` and the formula as `title` (failures: `#ERR` / `#CYCLE` with the
 *   class `formula-error` and the message in the title);
 * * column alignment uses the `align` attribute instead of inline `style` (the sanitizer drops
 *   all `style` attributes).
 */
import type { MarkdownIt, StateCore, Token } from 'markdown-it';
import { evaluateTable, type TableModel } from '@/core/calc';
import { getEnv } from '../env';
import { calcScope } from './calc';

type Cell = { open: Token; inline: Token | null };

/** Move markdown-it's `style="text-align:x"` into `align="x"`. */
function convertAlign(token: Token): void {
  const style = token.attrGet('style');
  if (style === null) return;
  const m = /text-align:\s*(left|center|right)/.exec(String(style));
  const attrs = (token.attrs ?? []).filter(([name]) => name !== 'style');
  token.attrs = attrs.length ? attrs : null;
  if (m) token.attrSet('align', m[1]!);
}

/** Cells of the table starting at `tokens[start]` (`table_open`); returns the end index too. */
function collectTable(tokens: readonly Token[], start: number): { header: Cell[]; rows: Cell[][]; end: number } {
  const header: Cell[] = [];
  const rows: Cell[][] = [];
  let inBody = false;
  let row: Cell[] | null = null;
  let k = start + 1;
  for (; k < tokens.length; k++) {
    const t = tokens[k]!;
    if (t.type === 'table_close') break;
    if (t.type === 'tbody_open') inBody = true;
    else if (t.type === 'tr_open' && inBody) rows.push((row = []));
    else if (t.type === 'th_open' || t.type === 'td_open') {
      convertAlign(t);
      const next = tokens[k + 1];
      const cell: Cell = { open: t, inline: next?.type === 'inline' ? next : null };
      if (t.type === 'th_open' && !inBody) header.push(cell);
      else row?.push(cell);
    }
  }
  return { header, rows, end: k };
}

const textOf = (c: Cell | undefined): string => c?.inline?.content.trim() ?? '';

/** Has the table any formula cell? */
function hasFormula(rows: readonly Cell[][]): boolean {
  return rows.some((r) => r.some((c) => /^=\s*\S/.test(textOf(c))));
}

function textToken(state: StateCore, content: string): Token {
  const t = new state.Token('text', '', 0);
  t.content = content;
  return t;
}

/** The `table_formulas` core rule (after inline parsing). */
export function tableFormulasRule(state: StateCore): void {
  const env = getEnv(state.env);
  const tokens = state.tokens;
  for (let i = 0; i < tokens.length; i++) {
    if (tokens[i]!.type !== 'table_open') continue;
    const { header, rows, end } = collectTable(tokens, i);
    const tableLine = tokens[i]!.map?.[0] ?? 0;
    i = end;
    if (!env || !hasFormula(rows)) continue;
    const model: TableModel = {
      header: header.map(textOf),
      aligns: header.map(() => null),
      rows: rows.map((r) => r.map(textOf)),
    };
    let result: ReturnType<typeof evaluateTable>;
    try {
      result = evaluateTable(model, env.locale, calcScope(env, tableLine));
    } catch {
      continue;
    }
    rows.forEach((row, r) => {
      row.forEach((cell, c) => {
        const value = result.values[r]?.[c];
        if (value === null || value === undefined) return;
        const error = result.errors[r]?.[c] ?? null;
        const formula = textOf(cell);
        cell.open.attrJoin('class', error ? 'formula formula-error' : 'formula');
        cell.open.attrSet('title', error ? `${formula} — ${error}` : formula);
        cell.open.attrSet('data-formula', formula);
        if (cell.inline) cell.inline.children = [textToken(state, value)];
      });
    });
  }
}

/** Install table post-processing. */
export function tablesPlugin(md: MarkdownIt): void {
  md.core.ruler.before('linkify', 'table_formulas', tableFormulasRule);
}
