/**
 * Line-based three-way merge (diff3) for text notes, with a "union" rule for pure additions:
 * when both sides inserted lines at the same place (typical: two devices appending to the
 * same journal page), both insertions are kept instead of reporting a conflict. Also a
 * key-level three-way merge for JSON objects (vault config).
 */
import { diff3Regions, diffComm } from './diff';

/** One region of a merge result. */
export type MergeHunk = { kind: 'ok'; lines: string[] } | { kind: 'conflict'; base: string[]; local: string[]; remote: string[] };

/** Result of `merge3`. */
export interface MergeResult {
  /** Merged text; unresolved conflict regions contain the local side. */
  text: string;
  /** True when every region merged automatically. */
  clean: boolean;
  /** Regions in document order (adjacent 'ok' regions are joined). */
  hunks: MergeHunk[];
}

/** A text split into lines plus its line-ending style. */
export interface SplitText {
  lines: string[];
  /** Dominant line ending, null when the text has no line break. */
  eol: '\n' | '\r\n' | null;
  /** Whether the text ends with a line break. */
  finalNewline: boolean;
}

/** Split text into lines (CRLF and LF both accepted; line endings removed). */
export function splitText(text: string): SplitText {
  if (text === '') return { lines: [], eol: null, finalNewline: false };
  const lf = text.split('\n').length - 1;
  const crlf = text.split('\r\n').length - 1;
  const eol = lf === 0 ? null : crlf * 2 >= lf ? '\r\n' : '\n';
  const normalized = text.replace(/\r\n/g, '\n');
  const finalNewline = normalized.endsWith('\n');
  const body = finalNewline ? normalized.slice(0, -1) : normalized;
  return { lines: body.split('\n'), eol, finalNewline };
}

/** Join lines back into text. */
export function joinLines(lines: readonly string[], eol: string, finalNewline: boolean): string {
  if (lines.length === 0) return '';
  return lines.join(eol) + (finalNewline ? eol : '');
}

function sameLines(a: readonly string[], b: readonly string[]): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

/**
 * Union of two insertions at the same place: common lines (LCS) once, local lines first,
 * then remote lines that are not already present among the local ones.
 */
export function unionLines(local: readonly string[], remote: readonly string[]): string[] {
  const parts = diffComm(local, remote);
  // Local lines outside the common subsequence: a remote line equal to one of them is a duplicate.
  const localOnly = new Map<string, number>();
  for (const part of parts) {
    if (part.common) continue;
    for (const line of part.a) localOnly.set(line, (localOnly.get(line) ?? 0) + 1);
  }
  const out: string[] = [];
  for (const part of parts) {
    if (part.common) {
      for (const line of part.common) out.push(line);
      continue;
    }
    for (const line of part.a) out.push(line);
    for (const line of part.b) {
      const n = localOnly.get(line) ?? 0;
      if (n > 0) localOnly.set(line, n - 1);
      else out.push(line);
    }
  }
  return out;
}

/** Resolve one diff3 conflict region, or return null when it is a real conflict. */
function resolveRegion(base: string[], local: string[], remote: string[]): string[] | null {
  if (sameLines(local, remote)) return local;
  if (sameLines(local, base)) return remote;
  if (sameLines(remote, base)) return local;
  if (base.length === 0) return unionLines(local, remote);
  return null;
}

function pushOk(hunks: MergeHunk[], lines: readonly string[]): void {
  if (lines.length === 0) return;
  const last = hunks[hunks.length - 1];
  // Loops instead of push(...spread): huge notes would overflow the argument limit.
  if (last?.kind === 'ok') for (const l of lines) last.lines.push(l);
  else hunks.push({ kind: 'ok', lines: lines.slice() });
}

/**
 * Three-way merge of `local` and `remote`, both derived from `base`, line by line.
 *
 * - Changes on one side only are taken.
 * - Regions both sides changed identically are taken once.
 * - Regions where both sides only inserted lines at the same place are unioned (local lines,
 *   then remote lines not already present) — e.g. journal appends on two devices.
 * - Anything else is a conflict: `clean` is false and `text` contains the local side there.
 *
 * Line endings: CRLF/LF differences never conflict; the result uses local's style (falling
 * back to remote, then base). The final newline is merged three-way.
 */
export function merge3(base: string, local: string, remote: string): MergeResult {
  if (local === remote || base === remote) return { text: local, clean: true, hunks: okHunks(local) };
  if (base === local) return { text: remote, clean: true, hunks: okHunks(remote) };

  const o = splitText(base);
  const a = splitText(local);
  const b = splitText(remote);
  const hunks: MergeHunk[] = [];
  let clean = true;
  for (const region of diff3Regions(a.lines, o.lines, b.lines)) {
    if (region.ok) {
      pushOk(hunks, region.ok);
      continue;
    }
    const c = region.conflict;
    if (!c) continue;
    const resolved = resolveRegion(c.o, c.a, c.b);
    if (resolved) pushOk(hunks, resolved);
    else {
      clean = false;
      hunks.push({ kind: 'conflict', base: [...c.o], local: [...c.a], remote: [...c.b] });
    }
  }
  const lines: string[] = [];
  for (const h of hunks) for (const l of h.kind === 'ok' ? h.lines : h.local) lines.push(l);
  const eol = a.eol ?? b.eol ?? o.eol ?? '\n';
  return { text: joinLines(lines, eol, mergeFinalNewline(o, a, b)), clean, hunks };
}

/** Three-way merge of the "ends with a line break" flag; an empty side has no opinion. */
function mergeFinalNewline(o: SplitText, a: SplitText, b: SplitText): boolean {
  if (a.lines.length === 0) return b.finalNewline;
  if (b.lines.length === 0) return a.finalNewline;
  return a.finalNewline === o.finalNewline ? b.finalNewline : a.finalNewline;
}

function okHunks(text: string): MergeHunk[] {
  const { lines } = splitText(text);
  return lines.length ? [{ kind: 'ok', lines }] : [];
}

// ---------------------------------------------------------------------------
// JSON objects (vault config)
// ---------------------------------------------------------------------------

function parseObject(text: string): Record<string, unknown> | null {
  if (text.trim() === '') return {};
  try {
    const v: unknown = JSON.parse(text.replace(/^\uFEFF/, ''));
    return typeof v === 'object' && v !== null && !Array.isArray(v) ? (v as Record<string, unknown>) : null;
  } catch {
    return null;
  }
}

function deepEqual(a: unknown, b: unknown): boolean {
  return JSON.stringify(a) === JSON.stringify(b);
}

/**
 * Key-level three-way merge of JSON objects: a key changed on one side only takes that
 * change (including deletion); a key changed on both sides keeps the local value. Returns
 * null when local or remote is not a JSON object. Output: 2-space JSON with a final newline.
 */
export function mergeJson3(base: string, local: string, remote: string): string | null {
  const l = parseObject(local);
  const r = parseObject(remote);
  if (!l || !r) return null;
  const o = parseObject(base) ?? {};
  const out: Record<string, unknown> = {};
  const keys = [...Object.keys(l), ...Object.keys(r).filter((k) => !Object.hasOwn(l, k))];
  for (const k of keys) {
    const inL = Object.hasOwn(l, k);
    const inR = Object.hasOwn(r, k);
    const inO = Object.hasOwn(o, k);
    const localUnchanged = inL === inO && (!inL || deepEqual(l[k], o[k]));
    const pickRemote = localUnchanged;
    const has = pickRemote ? inR : inL;
    if (!has) continue;
    // defineProperty: a "__proto__" key stays a plain data property.
    Object.defineProperty(out, k, { value: pickRemote ? r[k] : l[k], enumerable: true, writable: true, configurable: true });
  }
  return `${JSON.stringify(out, null, 2)}\n`;
}
