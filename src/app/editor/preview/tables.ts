/**
 * GFM tables in the live preview: a rendered table (formula results from @/core/calc, column
 * alignment, inline Markdown in cells) replacing the source while the selection is outside.
 * Clicking a cell puts the cursor into that cell's source; a toolbar adds rows / columns and
 * pretty-prints the source (serializeTable). Pure helpers are tested in tables.test.ts.
 */
import type { EditorView } from '@codemirror/view';
import { evaluateTable, pageScope, parseTable, serializeTable, type TableModel } from '@/core/calc';
import type { Locale } from '@/core/types';
import { editorContext } from '../context';
import { h, icon, type PreviewIcon } from './dom';
import { renderContextFor } from './embeds';
import { renderInlineSafe } from './render';
import { PreviewWidget } from './widgets';

export interface CellSpan {
  /** Offsets relative to the table source start. */
  from: number;
  to: number;
}

/**
 * Source positions of every cell's content (trimmed), per row: row 0 = header, rows 1… =
 * body rows (the delimiter row is skipped). Empty cells get an empty span inside the cell.
 * Offsets are relative to the start of `source` (the header row's first character).
 */
export function tableCellSpans(source: string): CellSpan[][] {
  const rows: CellSpan[][] = [];
  let offset = 0;
  const lines = source.split('\n');
  lines.forEach((line, i) => {
    const lineStart = offset;
    offset += line.length + 1;
    if (i === 1) return; // delimiter row
    if (!line.trim()) return;
    const cells: CellSpan[] = [];
    // Cell boundaries: unescaped pipes.
    const pipes: number[] = [];
    for (let p = 0; p < line.length; p++) {
      if (line[p] === '\\') {
        p++;
        continue;
      }
      if (line[p] === '|') pipes.push(p);
    }
    const lead = /^[ \t]*/.exec(line)![0].length;
    const trailEnd = line.trimEnd().length;
    const bounds: [number, number][] = [];
    let start = pipes[0] === lead ? lead + 1 : lead;
    for (const p of pipes) {
      if (p < start) continue;
      bounds.push([start, p]);
      start = p + 1;
    }
    if (start < trailEnd) bounds.push([start, trailEnd]);
    for (const [a, b] of bounds) {
      let f = a;
      let t = b;
      while (f < t && /\s/.test(line[f]!)) f++;
      while (t > f && /\s/.test(line[t - 1]!)) t--;
      if (f === t) {
        // Empty cell: just after the opening pipe's space (inside the cell).
        const mid = Math.min(a + 1, b);
        cells.push({ from: lineStart + mid, to: lineStart + mid });
      } else cells.push({ from: lineStart + f, to: lineStart + t });
    }
    rows.push(cells);
  });
  return rows;
}

/** Indentation of the rows after the first (tables inside list items). */
export function tableIndent(source: string): string {
  const second = source.split('\n')[1];
  return second ? /^[ \t]*/.exec(second)![0] : '';
}

export type TableAction = 'addRow' | 'addColumn' | 'align';

/**
 * Apply a toolbar action to a table source. Returns the new source and, for added rows /
 * columns, the offset (relative to the new source) of the first new cell.
 */
export function applyTableAction(source: string, action: TableAction): { text: string; cursor: number | null } | null {
  const model = parseTable(source);
  if (!model) return null;
  const cols = Math.max(model.header.length, ...model.rows.map((r) => r.length));
  const next: TableModel = { header: [...model.header], aligns: [...model.aligns], rows: model.rows.map((r) => [...r]) };
  if (action === 'addRow') next.rows.push(Array.from({ length: cols }, () => ''));
  if (action === 'addColumn') {
    next.header.push('');
    next.aligns.push(null);
    for (const r of next.rows) {
      while (r.length < cols) r.push('');
      r.push('');
    }
  }
  const indent = tableIndent(source);
  const text = serializeTable(next).split('\n').join(`\n${indent}`);
  if (action === 'align') return { text, cursor: null };
  const spans = tableCellSpans(text);
  if (action === 'addRow') {
    const last = spans[spans.length - 1];
    return { text, cursor: last?.[0]?.from ?? null };
  }
  const header = spans[0];
  return { text, cursor: header?.[header.length - 1]?.from ?? null };
}

export interface TableLabels {
  addRow: string;
  addColumn: string;
  align: string;
  formula: string;
  table: string;
}

/** Rendered GFM table (block widget). */
export class TableWidget extends PreviewWidget {
  constructor(
    readonly source: string,
    /** 0-based line of the header (page scope for formulas). */
    readonly line: number,
    readonly locale: Locale,
    readonly labels: TableLabels,
    readonly editable: boolean,
  ) {
    super();
  }

  override eq(o: TableWidget): boolean {
    return o.source === this.source && o.locale === this.locale && o.editable === this.editable && o.labels.addRow === this.labels.addRow;
  }

  override get estimatedHeight(): number {
    return (this.source.split('\n').length - 1) * 34 + 44;
  }

  toDOM(view: EditorView): HTMLElement {
    const el = h('div', { class: 'b-lp-table', 'data-lp-root': '', 'data-lp': 'table' });
    const model = parseTable(this.source);
    if (!model) {
      el.textContent = this.source;
      return el;
    }
    let scope: Record<string, unknown> | undefined;
    try {
      scope = pageScope(view.state.doc.toString(), this.line, this.locale);
    } catch {
      scope = undefined;
    }
    const evaluation = evaluateTable(model, this.locale, scope);
    const rc = renderContextFor(view.state.facet(editorContext), null);
    const inline = (text: string) => renderInlineSafe(text, rc);
    const cols = Math.max(model.header.length, ...model.rows.map((r) => r.length));
    const align = (c: number) => model.aligns[c] ?? null;

    const table = h('table', { class: 'b-lp-table__grid', 'aria-label': this.labels.table });
    const thead = h('thead');
    const hr = h('tr');
    for (let c = 0; c < cols; c++) {
      const th = h('th', { 'data-lp': 'cell', 'data-r': 0, 'data-c': c, scope: 'col' });
      const a = align(c);
      if (a) th.style.textAlign = a;
      th.innerHTML = inline(model.header[c] ?? '');
      hr.appendChild(th);
    }
    thead.appendChild(hr);
    table.appendChild(thead);
    const tbody = h('tbody');
    model.rows.forEach((row, r) => {
      const tr = h('tr');
      for (let c = 0; c < cols; c++) {
        const text = row[c] ?? '';
        const value = evaluation.values[r]?.[c] ?? null;
        const error = evaluation.errors[r]?.[c] ?? null;
        const td = h('td', { 'data-lp': 'cell', 'data-r': r + 1, 'data-c': c });
        const a = align(c);
        if (value !== null) {
          td.classList.add('is-formula');
          if (error) td.classList.add('is-error');
          td.title = error ? `${text} — ${error}` : `${this.labels.formula}: ${text}`;
          td.textContent = value;
          td.style.textAlign = a ?? 'right';
        } else {
          td.innerHTML = inline(text);
          if (a) td.style.textAlign = a;
          else if (/^[-+]?[\d\s.,]+(?:\s?(?:zł|PLN|€|EUR|\$|USD|%))?$/.test(text.trim()) && text.trim()) td.classList.add('is-number');
        }
        tr.appendChild(td);
      }
      tbody.appendChild(tr);
    });
    table.appendChild(tbody);
    el.appendChild(h('div', { class: 'b-lp-table__scroll' }, table));

    if (this.editable) {
      const tool = (action: string, label: string, name: PreviewIcon) =>
        h('button', { type: 'button', class: 'b-lp-table__tool', 'data-lp': action, title: label, tabindex: '-1' }, icon(name, 'b-lp-icon'), h('span', {}, label));
      el.appendChild(
        h(
          'div',
          { class: 'b-lp-table__tools', role: 'toolbar', 'aria-label': this.labels.table },
          tool('table-add-row', this.labels.addRow, 'addRow'),
          tool('table-add-column', this.labels.addColumn, 'addColumn'),
          tool('table-align', this.labels.align, 'align'),
        ),
      );
    }
    return el;
  }
}
