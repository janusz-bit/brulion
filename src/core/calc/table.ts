/**
 * GFM tables: parsing, pretty serialisation and locating tables in a page. Parsing follows the
 * markdown-it table rule (used by the renderer) so both agree on what is a table: a header row
 * containing `|`, a delimiter row (`---`, `:--`, `--:`, `:-:`) with the same number of cells,
 * then body rows until a blank line or another block; `\|` escapes a pipe.
 */

/** Column alignment from the delimiter row. */
export type TableAlign = 'left' | 'center' | 'right' | null;

/** A parsed GFM table. Cell text is trimmed and unescaped (`\|` → `|`). */
export interface TableModel {
  header: string[];
  aligns: TableAlign[];
  rows: string[][];
}

/**
 * Where a table is in a document. `content.slice(from, to)` is the table text (parseable with
 * `parseTable`); to replace it, insert the new rows joined by `'\n' + indent`:
 * `content.slice(0, from) + serializeTable(m).split('\n').join('\n' + indent) + content.slice(to)`.
 */
export interface TableLocation {
  /** Offset of the first character of the header row (after indentation and any list marker). */
  from: number;
  /** Offset just past the last row (before its line break). */
  to: number;
  /** 0-based line of the header row. */
  startLine: number;
  /** 0-based line of the last row (the delimiter row for a table without body). */
  endLine: number;
  /** Leading whitespace of the rows after the header (`'  '` inside a list item). */
  indent: string;
}

/** markdown-it's `escapedSplit`: split on `|` not preceded by `\`, dropping that backslash. */
function escapedSplit(str: string): string[] {
  const result: string[] = [];
  let current = '';
  let lastPos = 0;
  let escaped = false;
  for (let pos = 0; pos < str.length; pos++) {
    const ch = str[pos];
    if (ch === '|') {
      if (!escaped) {
        result.push(current + str.slice(lastPos, pos));
        current = '';
        lastPos = pos + 1;
      } else {
        current += str.slice(lastPos, pos - 1);
        lastPos = pos;
      }
    }
    escaped = ch === '\\';
  }
  result.push(current + str.slice(lastPos));
  return result;
}

/** Cells of a table row: trimmed, outer pipes optional, `\|` unescaped. */
export function splitTableRow(line: string): string[] {
  const cells = escapedSplit(line.trim());
  if (cells.length && cells[0] === '') cells.shift();
  if (cells.length && cells[cells.length - 1] === '') cells.pop();
  return cells.map((c) => c.trim());
}

/** Alignments of a delimiter row, or null when the line is not one. */
export function parseDelimiterRow(line: string): TableAlign[] | null {
  const t = line.trim();
  if (t.length < 2) return null;
  const [first, second] = [t[0]!, t[1]!];
  if (!'|-:'.includes(first)) return null;
  if (!'|-:'.includes(second) && !/\s/.test(second)) return null;
  if (first === '-' && /\s/.test(second)) return null;
  if (!/^[|\-:\s]+$/.test(t)) return null;
  const columns = t.split('|');
  const aligns: TableAlign[] = [];
  for (let i = 0; i < columns.length; i++) {
    const c = columns[i]!.trim();
    if (!c) {
      if (i === 0 || i === columns.length - 1) continue;
      return null;
    }
    if (!/^:?-+:?$/.test(c)) return null;
    const left = c.startsWith(':');
    const right = c.endsWith(':');
    aligns.push(left && right ? 'center' : right ? 'right' : left ? 'left' : null);
  }
  return aligns.length ? aligns : null;
}

function splitLines(content: string): string[] {
  return content.split(/\r\n|\r|\n/);
}

/** Header + delimiter at `lines[i]`, `lines[i+1]`; returns the alignments when they form a table. */
function tableHead(header: string | undefined, delimiter: string | undefined): TableAlign[] | null {
  if (header === undefined || delimiter === undefined || !header.includes('|')) return null;
  const aligns = parseDelimiterRow(delimiter);
  if (!aligns) return null;
  return splitTableRow(header).length === aligns.length ? aligns : null;
}

function fitRow(cells: string[], n: number): string[] {
  const out = cells.slice(0, n);
  while (out.length < n) out.push('');
  return out;
}

/**
 * Parse a GFM table (leading blank lines are skipped; parsing stops at the first blank line).
 * Ragged rows are padded with empty cells / truncated to the header width. Null if the text
 * does not start with a table.
 */
export function parseTable(markdown: string): TableModel | null {
  const lines = splitLines(markdown);
  let h = 0;
  while (h < lines.length && lines[h]!.trim() === '') h++;
  const aligns = tableHead(lines[h], lines[h + 1]);
  if (!aligns) return null;
  const header = splitTableRow(lines[h]!);
  const rows: string[][] = [];
  for (let i = h + 2; i < lines.length; i++) {
    const line = lines[i]!;
    if (line.trim() === '') break;
    rows.push(fitRow(splitTableRow(line), aligns.length));
  }
  return { header, aligns, rows };
}

// ---------------------------------------------------------------------------
// Display width
// ---------------------------------------------------------------------------

const WIDE_RANGES: [number, number][] = [
  [0x1100, 0x115f],
  [0x2e80, 0x303e],
  [0x3041, 0x33ff],
  [0x3400, 0x4dbf],
  [0x4e00, 0x9fff],
  [0xa000, 0xa4cf],
  [0xac00, 0xd7a3],
  [0xf900, 0xfaff],
  [0xfe30, 0xfe4f],
  [0xff00, 0xff60],
  [0xffe0, 0xffe6],
  [0x1f300, 0x1f64f],
  [0x1f900, 0x1f9ff],
  [0x20000, 0x3fffd],
];

function isWide(cp: number): boolean {
  return WIDE_RANGES.some(([a, b]) => cp >= a && cp <= b);
}

const ZERO_WIDTH_RE = /^(?:\p{M}|[\u200b-\u200f]|\u2060|\ufeff)+$/u;
const ZWJ = '\u200d';
const EMOJI_RE = /\p{Emoji_Presentation}|\ufe0f/u;

function graphemeWidth(g: string): number {
  if (ZERO_WIDTH_RE.test(g)) return 0;
  if (EMOJI_RE.test(g)) return 2;
  const cp = g.codePointAt(0)!;
  if (cp < 0x20 || (cp >= 0x7f && cp < 0xa0)) return 0;
  return isWide(cp) ? 2 : 1;
}

let segmenter: Intl.Segmenter | null | undefined;

function graphemes(s: string): string[] {
  if (segmenter === undefined) {
    segmenter = typeof Intl !== 'undefined' && 'Segmenter' in Intl ? new Intl.Segmenter(undefined, { granularity: 'grapheme' }) : null;
  }
  if (segmenter) return Array.from(segmenter.segment(s), (x) => x.segment);
  // Fallback: attach combining marks / joiners / variation selectors to the previous char.
  const out: string[] = [];
  for (const ch of s) {
    if (out.length && (ZERO_WIDTH_RE.test(ch) || ch === ZWJ || out[out.length - 1]!.endsWith(ZWJ))) {
      out[out.length - 1] += ch;
    } else out.push(ch);
  }
  return out;
}

/** Approximate monospace display width (CJK and emoji count as 2, combining marks as 0). */
export function displayWidth(s: string): number {
  if (/^[\x20-\x7e]*$/.test(s)) return s.length;
  let w = 0;
  for (const g of graphemes(s.normalize('NFC'))) w += graphemeWidth(g);
  return w;
}

// ---------------------------------------------------------------------------
// Serialisation
// ---------------------------------------------------------------------------

function escapeCell(text: string): string {
  return text.replace(/\r\n|\r|\n/g, '<br>').replace(/\|/g, '\\|').trim();
}

function pad(text: string, width: number, align: TableAlign): string {
  const diff = Math.max(0, width - displayWidth(text));
  if (align === 'right') return ' '.repeat(diff) + text;
  if (align === 'center') {
    const left = Math.floor(diff / 2);
    return ' '.repeat(left) + text + ' '.repeat(diff - left);
  }
  return text + ' '.repeat(diff);
}

function delimiterCell(width: number, align: TableAlign): string {
  if (align === 'left') return `:${'-'.repeat(width - 1)}`;
  if (align === 'right') return `${'-'.repeat(width - 1)}:`;
  if (align === 'center') return `:${'-'.repeat(width - 2)}:`;
  return '-'.repeat(width);
}

/**
 * Serialise a table as pretty GFM: outer pipes, columns padded to equal display width
 * (at least 3), alignment colons kept, pipes in cells escaped, line breaks in cells → `<br>`.
 * Rows longer than the header add columns (with an empty header cell). Lines are joined by `\n`.
 */
export function serializeTable(model: TableModel): string {
  // Loops instead of Math.max(...rows): spreading huge tables overflows the call stack.
  let cols = model.header.length;
  for (const r of model.rows) cols = Math.max(cols, r.length);
  if (cols === 0) return '';
  const header = fitRow(model.header.map(escapeCell), cols);
  const rows = model.rows.map((r) => fitRow(r.map(escapeCell), cols));
  const aligns = Array.from({ length: cols }, (_, c) => model.aligns[c] ?? null);
  const widths = aligns.map((_, c) => {
    let w = Math.max(3, displayWidth(header[c]!));
    for (const r of rows) w = Math.max(w, displayWidth(r[c]!));
    return w;
  });
  const line = (cells: string[]) => `| ${cells.join(' | ')} |`;
  return [
    line(header.map((h, c) => pad(h, widths[c]!, aligns[c]!))),
    line(widths.map((w, c) => delimiterCell(w, aligns[c]!))),
    ...rows.map((r) => line(r.map((cell, c) => pad(cell, widths[c]!, aligns[c]!)))),
  ].join('\n');
}

// ---------------------------------------------------------------------------
// Locating tables
// ---------------------------------------------------------------------------

interface LineInfo {
  text: string;
  start: number;
  end: number;
}

function linesWithOffsets(content: string): LineInfo[] {
  const out: LineInfo[] = [];
  const re = /\r\n|\r|\n/g;
  let start = 0;
  let m: RegExpExecArray | null;
  while ((m = re.exec(content))) {
    out.push({ text: content.slice(start, m.index), start, end: m.index });
    start = m.index + m[0].length;
  }
  out.push({ text: content.slice(start), start, end: content.length });
  return out;
}

const FENCE_RE = /^\s*(?:(?:[-*+]|\d+[.)])\s+)?(`{3,}|~{3,})(.*)$/;

/** Column width of a line prefix (tabs advance to the next multiple of 4). */
function columnWidth(prefix: string): number {
  let w = 0;
  for (const ch of prefix) w += ch === '\t' ? 4 - (w % 4) : 1;
  return w;
}

function leadingWhitespace(line: string): string {
  return /^[ \t]*/.exec(line)![0];
}

function indentWidth(line: string): number {
  return columnWidth(leadingWhitespace(line));
}

/** A list item line: marker prefix (with indentation and spacing) and its content. */
const LIST_ITEM_RE = /^([ \t]*(?:[-*+]|\d{1,9}[.)])[ \t]+)(\S.*)$/;

/** Header of a table starting at `text` (a plain or list-item line), or null. */
function tableStart(text: string, next: string): { offset: number; base: number } | null {
  const item = LIST_ITEM_RE.exec(text);
  if (item) {
    // Logseq / Markdown list item whose content starts with the table: `- | a | b |`.
    const base = columnWidth(item[1]!);
    const delimIndent = indentWidth(next);
    if (!tableHead(item[2]!, next) || delimIndent < base || delimIndent - base >= 4) return null;
    return { offset: item[1]!.length, base };
  }
  if (isTerminator(text) || !tableHead(text, next)) return null;
  const base = indentWidth(text);
  if (Math.abs(indentWidth(next) - base) >= 4) return null;
  return { offset: leadingWhitespace(text).length, base };
}

/** Lines that start another block and therefore end a table (markdown-it terminators). */
function isTerminator(line: string): boolean {
  const t = line.trimStart();
  return (
    /^(`{3,}|~{3,})/.test(t) ||
    t.startsWith('>') ||
    /^#{1,6}(\s|$)/.test(t) ||
    /^([-*+]|\d{1,9}[.)])(\s|$)/.test(t) ||
    /^([-*_])(\s*\1){2,}\s*$/.test(t) ||
    /^<[a-zA-Z/!?]/.test(t)
  );
}

/**
 * Locate GFM tables outside fenced code blocks and frontmatter. Tables may be indented inside
 * list items or start on a list item line (Logseq: `- | a | b |`); offsets are character
 * offsets into `content`. Tables inside blockquotes are not reported.
 */
export function findTables(content: string): TableLocation[] {
  const lines = linesWithOffsets(content);
  const found: TableLocation[] = [];
  let i = 0;
  if (lines[0]?.text.trim() === '---') {
    const close = lines.findIndex((l, k) => k > 0 && /^(---|\.\.\.)\s*$/.test(l.text));
    if (close > 0) i = close + 1;
  }
  let fence: { char: string; length: number } | null = null;
  for (; i < lines.length; i++) {
    const text = lines[i]!.text;
    if (fence) {
      const m = /^\s*(`{3,}|~{3,})\s*$/.exec(text);
      if (m && m[1]![0] === fence.char && m[1]!.length >= fence.length) fence = null;
      continue;
    }
    const f = FENCE_RE.exec(text);
    if (f && !(f[1]![0] === '`' && f[2]!.includes('`'))) {
      fence = { char: f[1]![0]!, length: f[1]!.length };
      continue;
    }
    const next = lines[i + 1];
    const start = next ? tableStart(text, next.text) : null;
    if (!next || !start) continue;
    const { base } = start;
    let end = i + 1;
    for (let j = i + 2; j < lines.length; j++) {
      const row = lines[j]!.text;
      const w = indentWidth(row);
      if (row.trim() === '' || isTerminator(row) || w < base || w - base >= 4) break;
      end = j;
    }
    found.push({
      from: lines[i]!.start + start.offset,
      to: lines[end]!.end,
      startLine: i,
      endLine: end,
      indent: leadingWhitespace(next.text),
    });
    i = end;
  }
  return found;
}
