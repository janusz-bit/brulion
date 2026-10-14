/** Small text-diff helpers for applying content edits to an editor with minimal changes. */

export interface TextChange {
  from: number;
  to: number;
  insert: string;
}

function isHighSurrogate(code: number): boolean {
  return code >= 0xd800 && code <= 0xdbff;
}
function isLowSurrogate(code: number): boolean {
  return code >= 0xdc00 && code <= 0xdfff;
}

/**
 * The single change turning `a` into `b`: common prefix and suffix are kept (never splitting a
 * surrogate pair). Null when the texts are equal.
 */
export function minimalChange(a: string, b: string): TextChange | null {
  if (a === b) return null;
  const min = Math.min(a.length, b.length);
  let start = 0;
  while (start < min && a.charCodeAt(start) === b.charCodeAt(start)) start++;
  if (start > 0 && isHighSurrogate(a.charCodeAt(start - 1))) start--;
  let endA = a.length;
  let endB = b.length;
  while (endA > start && endB > start && a.charCodeAt(endA - 1) === b.charCodeAt(endB - 1)) {
    endA--;
    endB--;
  }
  if (endA < a.length && isLowSurrogate(a.charCodeAt(endA))) {
    endA++;
    endB++;
  }
  return { from: start, to: endA, insert: b.slice(start, endB) };
}

/** Dominant line ending of a text. */
export function detectEol(text: string): '\n' | '\r\n' {
  let crlf = 0;
  let lf = 0;
  for (let i = text.indexOf('\n'); i >= 0; i = text.indexOf('\n', i + 1)) {
    if (i > 0 && text.charCodeAt(i - 1) === 13) crlf++;
    else lf++;
  }
  return crlf > lf ? '\r\n' : '\n';
}

/** Normalise all line endings to '\n' (what CodeMirror documents use). */
export function normalizeEol(text: string): string {
  return text.indexOf('\r') < 0 ? text : text.replace(/\r\n?/g, '\n');
}

/** Convert '\n' line endings to `eol`. */
export function withEol(text: string, eol: '\n' | '\r\n'): string {
  return eol === '\n' ? text : text.replace(/\n/g, eol);
}

/** Lines of `text` with their line breaks attached (joining them gives `text` back). */
function linesWithBreaks(text: string): string[] {
  const out: string[] = [];
  let start = 0;
  for (let i = text.indexOf('\n'); i >= 0; i = text.indexOf('\n', i + 1)) {
    out.push(text.slice(start, i + 1));
    start = i + 1;
  }
  if (start < text.length) out.push(text.slice(start));
  return out;
}

/** Largest middle section (lines × lines) diffed line by line; beyond it one change is used. */
const MAX_DIFF_CELLS = 1_000_000;

/**
 * Changes turning `a` into `b`, one per changed region of lines (each trimmed to the characters
 * that differ), all relative to `a` and sorted. Unlike {@link minimalChange}, edits at the top
 * and the bottom of a note stay separate, so a cursor between them keeps its place.
 */
export function diffChanges(a: string, b: string): TextChange[] {
  if (a === b) return [];
  const la = linesWithBreaks(a);
  const lb = linesWithBreaks(b);
  // Common leading/trailing lines.
  let pre = 0;
  while (pre < la.length && pre < lb.length && la[pre] === lb[pre]) pre++;
  let sufA = la.length;
  let sufB = lb.length;
  while (sufA > pre && sufB > pre && la[sufA - 1] === lb[sufB - 1]) {
    sufA--;
    sufB--;
  }
  const n = sufA - pre;
  const m = sufB - pre;
  let offset = 0;
  for (let i = 0; i < pre; i++) offset += la[i]!.length;
  const hunk = (aFrom: number, aTo: number, bFrom: number, bTo: number, at: number): TextChange | null => {
    const c = minimalChange(la.slice(aFrom, aTo).join(''), lb.slice(bFrom, bTo).join(''));
    return c && { from: at + c.from, to: at + c.to, insert: c.insert };
  };
  if (n === 0 || m === 0 || (n + 1) * (m + 1) > MAX_DIFF_CELLS) {
    const c = hunk(pre, sufA, pre, sufB, offset);
    return c ? [c] : [];
  }
  // LCS table over the middle lines: lcs[i][j] = LCS of la[pre+i..sufA) and lb[pre+j..sufB).
  const w = m + 1;
  const lcs = new Uint32Array((n + 1) * w);
  for (let i = n - 1; i >= 0; i--) {
    for (let j = m - 1; j >= 0; j--) {
      lcs[i * w + j] =
        la[pre + i] === lb[pre + j] ? lcs[(i + 1) * w + j + 1]! + 1 : Math.max(lcs[(i + 1) * w + j]!, lcs[i * w + j + 1]!);
    }
  }
  const out: TextChange[] = [];
  let i = 0;
  let j = 0;
  let pos = offset;
  while (i < n || j < m) {
    if (i < n && j < m && la[pre + i] === lb[pre + j]) {
      pos += la[pre + i]!.length;
      i++;
      j++;
      continue;
    }
    const i0 = i;
    const j0 = j;
    const start = pos;
    while ((i < n || j < m) && !(i < n && j < m && la[pre + i] === lb[pre + j])) {
      if (j >= m || (i < n && lcs[(i + 1) * w + j]! >= lcs[i * w + j + 1]!)) {
        pos += la[pre + i]!.length;
        i++;
      } else j++;
    }
    const c = hunk(pre + i0, pre + i, pre + j0, pre + j, start);
    if (c) out.push(c);
  }
  return out;
}
