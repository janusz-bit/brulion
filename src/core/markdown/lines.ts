/**
 * Line utilities shared by the parser and the edit helpers.
 *
 * Lines are split like CodeMirror does (`\r\n`, `\r` and `\n` all end a line) so that
 * 0-based line numbers produced by the parser match the editor's line numbers.
 */

/** A document split into lines. All arrays have the same length (at least 1). */
export interface Lines {
  /** Line texts without line terminators. */
  texts: string[];
  /** Document offset of each line's first character. */
  starts: number[];
  /** Line terminator following each line (`''` for the last line). */
  eols: string[];
}

/** Split `content` into lines, remembering every line terminator. Linear time. */
export function splitLines(content: string): Lines {
  const texts: string[] = [];
  const starts: number[] = [];
  const eols: string[] = [];
  let start = 0;
  const n = content.length;
  for (let i = 0; i < n; i++) {
    const c = content.charCodeAt(i);
    if (c !== 10 && c !== 13) continue;
    texts.push(content.slice(start, i));
    starts.push(start);
    if (c === 13 && content.charCodeAt(i + 1) === 10) {
      eols.push('\r\n');
      i++;
    } else {
      eols.push(c === 13 ? '\r' : '\n');
    }
    start = i + 1;
  }
  texts.push(content.slice(start));
  starts.push(start);
  eols.push('');
  return { texts, starts, eols };
}

/** Join lines back into a document (inverse of {@link splitLines}). */
export function joinLines(lines: Lines): string {
  let out = '';
  for (let i = 0; i < lines.texts.length; i++) out += lines.texts[i]! + lines.eols[i]!;
  return out;
}

/**
 * Line terminator to use for a line inserted next to line `i`: the line's own terminator,
 * otherwise the first terminator used in the document, otherwise `\n`.
 */
export function eolFor(lines: Lines, i: number): string {
  const own = lines.eols[i];
  if (own) return own;
  for (const e of lines.eols) if (e) return e;
  return '\n';
}

/** Tab stop width used to compute visual columns (CommonMark uses 4). */
export const TAB_WIDTH = 4;

/** Leading whitespace of a line. */
export interface Indent {
  /** Visual column of the first non-whitespace character (tabs advance to the next tab stop). */
  col: number;
  /** Number of leading whitespace characters. */
  chars: number;
}

/** Measure the leading whitespace (spaces and tabs) of `s`. */
export function indentOf(s: string): Indent {
  let col = 0;
  let i = 0;
  for (; i < s.length; i++) {
    const c = s.charCodeAt(i);
    if (c === 32) col++;
    else if (c === 9) col += TAB_WIDTH - (col % TAB_WIDTH);
    else break;
  }
  return { col, chars: i };
}

/**
 * Number of leading whitespace characters to drop to remove `cols` visual columns of
 * indentation (used to dedent continuation lines of list items). A tab that crosses the
 * target column is removed as a whole.
 */
export function charsForColumns(s: string, cols: number): number {
  let col = 0;
  let i = 0;
  for (; i < s.length && col < cols; i++) {
    const c = s.charCodeAt(i);
    if (c === 32) col++;
    else if (c === 9) col += TAB_WIDTH - (col % TAB_WIDTH);
    else break;
  }
  return i;
}

/** True when the string contains only spaces and tabs (or nothing). */
export function isBlank(s: string): boolean {
  for (let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i);
    if (c !== 32 && c !== 9) return false;
  }
  return true;
}

/** Replace every character except line breaks by a space (keeps UTF-16 length and offsets). */
export function blankOut(s: string): string {
  return s.replace(/[^\r\n]/g, ' ');
}
