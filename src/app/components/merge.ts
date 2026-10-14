/** Pure logic behind <MergeView>: conflict hunks, per-hunk resolutions, building the result. */
import { splitLines } from './diff';

export type MergeHunk =
  | { kind: 'ok'; lines: string[] }
  | { kind: 'conflict'; base: string[]; local: string[]; remote: string[] };

export type MergePick = 'local' | 'remote' | 'both' | 'manual';

export type Resolution = { pick: 'local' | 'remote' | 'both' } | { pick: 'manual'; text: string };

/** Hunk index → resolution (only conflict hunks need one). */
export type Resolutions = Readonly<Record<number, Resolution | undefined>>;

/** Indexes of the conflict hunks. */
export function conflictIndexes(hunks: readonly MergeHunk[]): number[] {
  const out: number[] = [];
  hunks.forEach((h, i) => {
    if (h.kind === 'conflict') out.push(i);
  });
  return out;
}

/** Lines a resolution produces for a conflict hunk. */
export function resolvedLines(hunk: Extract<MergeHunk, { kind: 'conflict' }>, res: Resolution): string[] {
  switch (res.pick) {
    case 'local':
      return [...hunk.local];
    case 'remote':
      return [...hunk.remote];
    case 'both':
      return [...hunk.local, ...hunk.remote];
    case 'manual':
      return splitLines(res.text);
  }
}

/** Initial text for manual editing: the current choice, else both versions. */
export function manualSeed(hunk: Extract<MergeHunk, { kind: 'conflict' }>, res: Resolution | undefined, eol = '\n'): string {
  if (res?.pick === 'manual') return res.text;
  return resolvedLines(hunk, res ?? { pick: 'both' }).join(eol);
}

export function unresolvedCount(hunks: readonly MergeHunk[], resolutions: Resolutions): number {
  return conflictIndexes(hunks).filter((i) => !resolutions[i]).length;
}

/**
 * Join all hunks into the merged text, or null while a conflict is unresolved.
 * `finalNewline` appends `eol` after the last line (files usually end with one).
 */
export function buildMerged(
  hunks: readonly MergeHunk[],
  resolutions: Resolutions,
  opts: { eol?: string; finalNewline?: boolean } = {},
): string | null {
  const eol = opts.eol ?? '\n';
  const lines: string[] = [];
  for (let i = 0; i < hunks.length; i++) {
    const h = hunks[i]!;
    if (h.kind === 'ok') {
      lines.push(...h.lines);
      continue;
    }
    const res = resolutions[i];
    if (!res) return null;
    lines.push(...resolvedLines(h, res));
  }
  if (lines.length === 0) return '';
  return lines.join(eol) + (opts.finalNewline === false ? '' : eol);
}

/** One region of node-diff3's `diff3Merge(local, base, remote)` output. */
export interface Diff3Region {
  ok?: string[];
  conflict?: { a: string[]; o: string[]; b: string[] };
}

/**
 * Convert `diff3Merge(local, base, remote)` regions (node-diff3: `a` = local, `o` = base,
 * `b` = remote) into hunks; adjacent ok regions are merged.
 */
export function hunksFromDiff3(regions: readonly Diff3Region[]): MergeHunk[] {
  const out: MergeHunk[] = [];
  for (const r of regions) {
    if (r.ok) {
      const last = out[out.length - 1];
      if (last?.kind === 'ok') last.lines.push(...r.ok);
      else out.push({ kind: 'ok', lines: [...r.ok] });
    } else if (r.conflict) {
      out.push({ kind: 'conflict', base: [...r.conflict.o], local: [...r.conflict.a], remote: [...r.conflict.b] });
    }
  }
  return out;
}
