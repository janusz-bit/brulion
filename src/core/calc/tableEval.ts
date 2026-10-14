/**
 * Table formulas (SYNTAX.md §9): any body cell starting with `=` is evaluated.
 *
 * References: `A1` (column letter, data-row number; row 1 = first row under the header),
 * ranges `A1:B3`, whole columns `B:B` / `A:C`, `[Header]` (cell of the current row in the
 * column with that header, case-insensitive), `above` / `below` / `left` / `right` (numeric
 * cells in that direction; Polish `powyżej` / `poniżej`). Formulas may reference other formulas
 * (evaluated in dependency order); cycles give `#CYCLE`, errors `#ERR`.
 */
import type { Locale } from '../types';
import { getEngine, MAX_EXPRESSION_LENGTH, withBudget } from './engine';
import { charge, elementWork } from './limits';
import { evaluateTokens, type TempValue } from './expression';
import { describeError, formatValue } from './format';
import { endsOperand, lex, matchBrackets, nextSignificant, prevSignificant, type Token } from './lexer';
import { CalcError, calcMessage } from './messages';
import { CalcScope, type CalcValue } from './model';
import { TEMP_PREFIX } from './names';
import { parseLocaleNumber, stripInlineMarkup, type CurrencyInfo } from './numbers';
import type { TableModel } from './table';

/** Rendered value shown for a cell whose formula failed. */
export const ERR = '#ERR';
/** Rendered value shown for a cell that is part of a reference cycle. */
export const CYCLE = '#CYCLE';

/** Result of `evaluateTable`, indexed like `model.rows` (`[row][column]`). */
export interface TableEvaluation {
  /** Formatted result for formula cells, null for other cells. */
  values: (string | null)[][];
  /** Error message for failed formula cells, null otherwise. */
  errors: (string | null)[][];
}

type RefSpec =
  | { kind: 'cell'; r: number; c: number; text: string }
  | { kind: 'range'; r1: number; c1: number; r2: number; c2: number; text: string }
  | { kind: 'header'; c: number; text: string }
  | { kind: 'direction'; dir: 'above' | 'below' | 'left' | 'right'; text: string };

interface Formula {
  r: number;
  c: number;
  tokens: Token[];
  /** temp name → reference */
  refs: Map<string, RefSpec>;
  error?: unknown;
}

const DIRECTION_WORDS: ReadonlyMap<string, 'above' | 'below' | 'left' | 'right'> = new Map([
  ['above', 'above'],
  ['below', 'below'],
  ['left', 'left'],
  ['right', 'right'],
  ['powyżej', 'above'],
  ['poniżej', 'below'],
]);

/** "A" → 0, "Z" → 25, "AA" → 26. */
export function columnIndex(letters: string): number {
  let n = 0;
  for (const ch of letters.toUpperCase()) n = n * 26 + (ch.charCodeAt(0) - 64);
  return n - 1;
}

/** 0 → "A", 26 → "AA". */
export function columnName(index: number): string {
  let s = '';
  let n = index + 1;
  while (n > 0) {
    const rem = (n - 1) % 26;
    s = String.fromCharCode(65 + rem) + s;
    n = Math.floor((n - 1) / 26);
  }
  return s;
}

/** Spreadsheet name of a body cell: row 0, column 1 → "B1". */
export function cellName(r: number, c: number): string {
  return `${columnName(c)}${r + 1}`;
}

function normaliseHeader(text: string): string {
  return stripInlineMarkup(text).normalize('NFC').replace(/\s+/g, ' ').trim().toLowerCase();
}

const CELL_RE = /^([A-Z]{1,3})([1-9]\d{0,8})$/;
const COLUMN_RE = /^([A-Z]{1,3})$/;

/** Replace references in a formula's tokens by temporaries. */
function substituteRefs(tokens: Token[], headers: string[]): { tokens: Token[]; refs: Map<string, RefSpec> } {
  const refs = new Map<string, RefSpec>();
  const out: Token[] = [];
  const pairs = matchBrackets(tokens);
  const temp = (spec: RefSpec, pos: number) => {
    const name = `${TEMP_PREFIX}ref${refs.size}`;
    refs.set(name, spec);
    out.push({ kind: 'ident', text: spec.text, pos, out: name, raw: true, ref: name });
  };
  // Open `?` of conditional expressions per bracket depth: their `:` is not a range.
  const pendingTernary: number[] = [0];
  for (let i = 0; i < tokens.length; i++) {
    const t = tokens[i]!;
    const depth = pendingTernary.length - 1;
    if (t.kind === 'open') pendingTernary.push(0);
    else if (t.kind === 'close' && depth > 0) pendingTernary.pop();
    else if (t.kind === 'op' && t.text === '?') pendingTernary[depth]!++;
    else if (t.kind === 'op' && t.text === ':' && pendingTernary[depth]! > 0) pendingTernary[depth]!--;
    // [Header]
    if (t.kind === 'open' && t.text === '[' && !endsOperand(tokens[prevSignificant(tokens, i - 1)])) {
      const close = pairs.get(i) ?? -1;
      if (close > i) {
        // Headers may contain brackets themselves (`[Cena (zł)]`); a matrix literal is only
        // taken for a header reference when its text names a column.
        const name = normaliseHeader(tokens.slice(i + 1, close).map((x) => x.text).join(''));
        const c = name ? headers.indexOf(name) : -1;
        if (c >= 0) {
          temp({ kind: 'header', c, text: tokens.slice(i, close + 1).map((x) => x.text).join('') }, t.pos);
          i = close;
          pendingTernary.pop(); // the skipped `]` closes the frame pushed for `[`
          continue;
        }
      }
    }
    if (t.kind === 'ident' && !t.raw) {
      const next = tokens[nextSignificant(tokens, i + 1)];
      const isCall = next?.kind === 'open' && next.text === '(';
      const colonIdx = nextSignificant(tokens, i + 1);
      const rangeColon = next?.kind === 'op' && next.text === ':' && pendingTernary[depth] === 0;
      const afterColon = rangeColon ? nextSignificant(tokens, colonIdx + 1) : -1;
      const other = afterColon >= 0 ? tokens[afterColon] : undefined;
      const cell = CELL_RE.exec(t.text);
      const column = COLUMN_RE.exec(t.text);
      if (cell && other?.kind === 'ident' && CELL_RE.test(other.text)) {
        const end = CELL_RE.exec(other.text)!;
        temp(
          {
            kind: 'range',
            c1: columnIndex(cell[1]!),
            r1: Number(cell[2]) - 1,
            c2: columnIndex(end[1]!),
            r2: Number(end[2]) - 1,
            text: `${t.text}:${other.text}`,
          },
          t.pos,
        );
        i = afterColon;
        continue;
      }
      if (column && other?.kind === 'ident' && COLUMN_RE.test(other.text)) {
        temp(
          { kind: 'range', c1: columnIndex(column[1]!), r1: 0, c2: columnIndex(other.text), r2: Infinity, text: `${t.text}:${other.text}` },
          t.pos,
        );
        i = afterColon;
        continue;
      }
      if (cell && !isCall) {
        temp({ kind: 'cell', c: columnIndex(cell[1]!), r: Number(cell[2]) - 1, text: t.text }, t.pos);
        continue;
      }
      const dir = DIRECTION_WORDS.get(t.text.toLowerCase());
      if (dir && !isCall) {
        temp({ kind: 'direction', dir, text: t.text }, t.pos);
        continue;
      }
    }
    out.push(t);
  }
  return { tokens: out, refs };
}

/** The table's cells, trimmed once, with formula flags and plain values parsed at most once. */
class Grid {
  readonly rows: number;
  private readonly texts: string[];
  private readonly formula: Uint8Array;
  /** Rows of formula cells per column, ascending. */
  private readonly formulaRows: number[][];
  private readonly plain = new Map<number, CalcValue | null>();

  constructor(
    model: TableModel,
    readonly cols: number,
    private readonly locale: Locale,
  ) {
    this.rows = model.rows.length;
    this.texts = Array.from<string>({ length: this.rows * cols });
    this.formula = new Uint8Array(this.rows * cols);
    this.formulaRows = Array.from({ length: cols }, () => []);
    for (let r = 0; r < this.rows; r++) {
      const row = model.rows[r]!;
      for (let c = 0; c < cols; c++) {
        const text = (row[c] ?? '').trim();
        this.texts[r * cols + c] = text;
        if (isFormula(text)) {
          this.formula[r * cols + c] = 1;
          this.formulaRows[c]!.push(r);
        }
      }
    }
  }

  /** Keys of the formula cells inside an area (without scanning plain cells). */
  *formulasIn(a: Area): Generator<number> {
    for (let c = Math.max(0, a.c1); c <= Math.min(this.cols - 1, a.c2); c++) {
      const rows = this.formulaRows[c]!;
      // First formula row >= r1 (binary search).
      let lo = 0;
      let hi = rows.length;
      while (lo < hi) {
        const mid = (lo + hi) >> 1;
        if (rows[mid]! < a.r1) lo = mid + 1;
        else hi = mid;
      }
      for (let i = lo; i < rows.length && rows[i]! <= a.r2; i++) {
        const k = this.key(rows[i]!, c);
        if (k !== a.skip) yield k;
      }
    }
  }

  key(r: number, c: number): number {
    return r * this.cols + c;
  }

  text(r: number, c: number): string {
    return this.texts[this.key(r, c)] ?? '';
  }

  isFormula(r: number, c: number): boolean {
    return this.formula[this.key(r, c)] === 1;
  }

  /** Value of a plain (non-formula) cell; empty → null, text → string. */
  plainValue(r: number, c: number): CalcValue | null {
    const k = this.key(r, c);
    let v = this.plain.get(k);
    if (v === undefined) {
      v = plainCellValue(this.text(r, c), this.locale);
      this.plain.set(k, v);
    }
    return v;
  }
}

/** Rectangle of cells a reference covers; `skip` is a cell left out (the formula's own cell). */
interface Area {
  r1: number;
  r2: number;
  c1: number;
  c2: number;
  skip: number;
}

/** Cells covered by a reference (ranges and directions are clamped to the table). */
function areaOf(spec: RefSpec, r: number, c: number, grid: Grid): Area {
  const none = -1;
  switch (spec.kind) {
    case 'cell':
      if (spec.r < 0 || spec.r >= grid.rows || spec.c < 0 || spec.c >= grid.cols) {
        throw new CalcError('calc.error.refOutside', { ref: spec.text });
      }
      return { r1: spec.r, r2: spec.r, c1: spec.c, c2: spec.c, skip: none };
    case 'header':
      return { r1: r, r2: r, c1: spec.c, c2: spec.c, skip: none };
    case 'range':
      return {
        r1: Math.max(0, Math.min(spec.r1, spec.r2)),
        r2: Math.min(grid.rows - 1, Math.max(spec.r1, spec.r2)),
        c1: Math.max(0, Math.min(spec.c1, spec.c2)),
        c2: Math.min(grid.cols - 1, Math.max(spec.c1, spec.c2)),
        skip: grid.key(r, c),
      };
    case 'direction':
      if (spec.dir === 'above') return { r1: 0, r2: r - 1, c1: c, c2: c, skip: none };
      if (spec.dir === 'below') return { r1: r + 1, r2: grid.rows - 1, c1: c, c2: c, skip: none };
      if (spec.dir === 'left') return { r1: r, r2: r, c1: 0, c2: c - 1, skip: none };
      return { r1: r, r2: r, c1: c + 1, c2: grid.cols - 1, skip: none };
  }
}

function areaSize(a: Area): number {
  return Math.max(0, a.r2 - a.r1 + 1) * Math.max(0, a.c2 - a.c1 + 1);
}

/** Single-cell references yield the cell's value; ranges and directions an array of numbers. */
function isSingle(spec: RefSpec): boolean {
  return spec.kind === 'cell' || spec.kind === 'header';
}

/**
 * Formula cells a formula depends on, enumerated lazily: dependency graphs of running totals
 * (`=sum(above)` in every row) are quadratic, so they are never stored as a whole, and every
 * edge is metered like a touched element (cheap, but there may be millions).
 */
function* formulaDeps(f: Formula, grid: Grid): Generator<number> {
  const edgeWork = elementWork(1);
  for (const spec of f.refs.values()) {
    for (const k of grid.formulasIn(areaOf(spec, f.r, f.c, grid))) {
      charge(edgeWork);
      yield k;
    }
  }
}

/**
 * Tarjan's SCC. `onComponent(nodes, cyclic)` is called for every strongly connected component as
 * soon as it is complete, in dependency-first order; `cyclic` is true for components with more
 * than one node or a node that depends on itself. Iterative with lazily enumerated edges, so long
 * reference chains cannot overflow the call stack and big dependency graphs are never
 * materialised. When enumerating the edges of a node throws, `onError` is told and the node is
 * treated as having no further edges.
 */
function stronglyConnected(
  nodes: Iterable<number>,
  edges: (n: number) => Iterator<number>,
  onError: (n: number, err: unknown) => void,
  onComponent: (nodes: number[], cyclic: boolean) => void,
): void {
  let index = 0;
  const idx = new Map<number, number>();
  const low = new Map<number, number>();
  const onStack = new Set<number>();
  const stack: number[] = [];
  const selfLoops = new Set<number>();
  const work: { v: number; edges: Iterator<number> }[] = [];
  const open = (v: number) => {
    idx.set(v, index);
    low.set(v, index);
    index++;
    stack.push(v);
    onStack.add(v);
    work.push({ v, edges: edges(v) });
  };
  for (const root of nodes) {
    if (idx.has(root)) continue;
    open(root);
    while (work.length) {
      const frame = work[work.length - 1]!;
      let step: IteratorResult<number>;
      try {
        step = frame.edges.next();
      } catch (err) {
        onError(frame.v, err);
        step = { done: true, value: undefined };
      }
      if (!step.done) {
        const w = step.value;
        if (w === frame.v) selfLoops.add(w);
        if (!idx.has(w)) open(w);
        else if (onStack.has(w)) low.set(frame.v, Math.min(low.get(frame.v)!, idx.get(w)!));
        continue;
      }
      work.pop();
      const v = frame.v;
      if (low.get(v) === idx.get(v)) {
        const comp: number[] = [];
        let w: number;
        do {
          w = stack.pop()!;
          onStack.delete(w);
          comp.push(w);
        } while (w !== v);
        onComponent(comp, comp.length > 1 || selfLoops.has(v));
      }
      const parent = work[work.length - 1];
      if (parent) low.set(parent.v, Math.min(low.get(parent.v)!, low.get(v)!));
    }
  }
}

type CellState = { kind: 'ok'; value: CalcValue } | { kind: 'error' } | { kind: 'cycle' };

function isFormula(text: string): boolean {
  return text.startsWith('=') && text.slice(1).trim() !== '';
}

/**
 * Evaluate the formula cells of a table. `values[r][c]` is the formatted result of a formula
 * cell (`#ERR` / `#CYCLE` on failure) and null for other cells; `errors[r][c]` holds the error
 * message. Plain cells are read as numbers when referenced (`12,50 zł`, `1 234`, `23%`); text
 * is skipped by ranges and directions. `scope` (e.g. from `pageScope`) provides variables.
 * Work is bounded (very big tables of range formulas end in "too complex" errors). Never throws.
 */
export function evaluateTable(model: TableModel, locale: Locale = 'pl', scope?: Record<string, unknown>): TableEvaluation {
  return withBudget(() => evaluateTableModel(model, locale, scope));
}

function evaluateTableModel(model: TableModel, locale: Locale, scope: Record<string, unknown> | undefined): TableEvaluation {
  let cols = model.header.length;
  for (const r of model.rows) cols = Math.max(cols, r.length);
  const grid = new Grid(model, cols, locale);
  const headers = Array.from({ length: cols }, (_, c) => normaliseHeader(model.header[c] ?? ''));
  const values: (string | null)[][] = model.rows.map(() => Array<string | null>(cols).fill(null));
  const errors: (string | null)[][] = model.rows.map(() => Array<string | null>(cols).fill(null));

  const formulas = new Map<number, Formula>();
  for (let r = 0; r < grid.rows; r++) {
    for (let c = 0; c < cols; c++) {
      if (!grid.isFormula(r, c)) continue;
      const text = grid.text(r, c);
      const f: Formula = { r, c, tokens: [], refs: new Map() };
      try {
        if (text.length > MAX_EXPRESSION_LENGTH) throw new CalcError('calc.error.tooLong');
        const sub = substituteRefs(lex(text.slice(1)).tokens, headers);
        f.tokens = sub.tokens;
        f.refs = sub.refs;
        for (const spec of sub.refs.values()) areaOf(spec, r, c, grid); // reports references outside the table
      } catch (err) {
        f.error = err;
      }
      formulas.set(grid.key(r, c), f);
    }
  }

  const states = new Map<number, CellState>();
  const baseScope = CalcScope.fromRecord(scope);
  const evaluateComponent = (comp: number[], cyclic: boolean): void => {
    for (const k of comp) {
      const f = formulas.get(k)!;
      if (cyclic) {
        states.set(k, { kind: 'cycle' });
        values[f.r]![f.c] = CYCLE;
        errors[f.r]![f.c] = calcMessage(locale, 'calc.error.cycle');
        continue;
      }
      try {
        const value = evaluateFormula(f, grid, states, baseScope, locale);
        states.set(k, { kind: 'ok', value });
        values[f.r]![f.c] = formatValue(value.value, value.meta, locale) ?? '';
      } catch (err) {
        states.set(k, { kind: 'error' });
        values[f.r]![f.c] = ERR;
        errors[f.r]![f.c] = describeError(err, locale);
      }
    }
  };
  const noEdges: Iterator<number> = [][Symbol.iterator]();
  // Components are evaluated as soon as they are complete (dependencies first), so a huge table
  // that exhausts the work budget still gets its first formulas computed.
  stronglyConnected(
    formulas.keys(),
    (n) => {
      const f = formulas.get(n);
      return f && !f.error ? formulaDeps(f, grid) : noEdges;
    },
    (n, err) => {
      const f = formulas.get(n);
      if (f && !f.error) f.error = err;
    },
    evaluateComponent,
  );
  return { values, errors };
}

/** Value of a plain (non-formula) cell; empty → null, text → string. */
function plainCellValue(text: string, locale: Locale): CalcValue | null {
  if (text === '') return null;
  const parsed = parseLocaleNumber(text, locale);
  if (!parsed) return { value: stripInlineMarkup(text), meta: {} };
  return { value: parsed.value, meta: { ...(parsed.percent ? { percent: true } : {}), ...(parsed.currency ? { currency: parsed.currency } : {}) } };
}

function isNumericCell(v: CalcValue): boolean {
  const math = getEngine().math;
  if (typeof v.value === 'number') return true;
  return math.isUnit(v.value) && (v.value as unknown as { value: unknown }).value !== null;
}

function evaluateFormula(f: Formula, grid: Grid, states: Map<number, CellState>, baseScope: CalcScope, locale: Locale): CalcValue {
  if (f.error) throw f.error;
  const cellValue = (r: number, c: number): CalcValue | null => {
    if (!grid.isFormula(r, c)) return grid.plainValue(r, c);
    const state = states.get(grid.key(r, c));
    if (state?.kind === 'ok') return state.value;
    throw new CalcError('calc.error.refError', { cell: cellName(r, c) });
  };
  const temps = new Map<string, TempValue>();
  for (const [name, spec] of f.refs) {
    const a = areaOf(spec, f.r, f.c, grid);
    if (isSingle(spec)) {
      temps.set(name, cellValue(a.r1, a.c1) ?? { value: 0, meta: {} });
      continue;
    }
    // Reading cached cell values is cheaper than a collection operation.
    charge(elementWork(areaSize(a)) / 2);
    const items: unknown[] = [];
    const currencies: CurrencyInfo[] = [];
    for (let r = a.r1; r <= a.r2; r++) {
      for (let c = a.c1; c <= a.c2; c++) {
        if (grid.key(r, c) === a.skip) continue;
        const v = cellValue(r, c);
        if (!v || !isNumericCell(v)) continue;
        items.push(v.value);
        if (v.meta.currency) currencies.push(v.meta.currency);
      }
    }
    temps.set(name, { value: items, meta: {}, currencies });
  }
  return evaluateTokens(f.tokens, { locale, scope: baseScope, temps });
}
