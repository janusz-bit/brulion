/**
 * Ranking for page suggestions (`[[` completion, quick switcher). Names and queries are
 * folded (case- and diacritic-insensitive). Ranks: exact > starts with > a word starts with >
 * contains > fuzzy (in-order characters, or one typo in a word's beginning).
 */
import { fold } from './text';

/** Match quality of a name against a query; 0 = no match. */
export const RANK = {
  exact: 6,
  prefix: 5,
  wordPrefix: 4,
  contains: 3,
  fuzzyClose: 2,
  fuzzy: 1,
  none: 0,
} as const;

const WORD_SPLIT_RE = /[^\p{L}\p{N}]+/u;

/** Words of a folded name ("projekty/dom-jana" → ["projekty", "dom", "jana"]). */
function words(folded: string): string[] {
  return folded.split(WORD_SPLIT_RE).filter(Boolean);
}

/**
 * Is the edit distance (insert/delete/substitute/adjacent swap) between `a` and `b` at most
 * `max`? Small strings only (O(|a|·|b|)).
 */
export function withinEditDistance(a: string, b: string, max: number): boolean {
  if (Math.abs(a.length - b.length) > max) return false;
  if (a === b) return true;
  const n = a.length;
  const m = b.length;
  let prev2: number[] = [];
  let prev: number[] = Array.from({ length: m + 1 }, (_, j) => j);
  for (let i = 1; i <= n; i++) {
    const cur: number[] = [i];
    let rowMin = i;
    for (let j = 1; j <= m; j++) {
      const cost = a[i - 1] === b[j - 1] ? 0 : 1;
      let v = Math.min(prev[j]! + 1, cur[j - 1]! + 1, prev[j - 1]! + cost);
      if (i > 1 && j > 1 && a[i - 1] === b[j - 2] && a[i - 2] === b[j - 1]) v = Math.min(v, prev2[j - 2]! + 1);
      cur.push(v);
      if (v < rowMin) rowMin = v;
    }
    if (rowMin > max) return false;
    prev2 = prev;
    prev = cur;
  }
  return prev[m]! <= max;
}

/** Span of the shortest in-order occurrence of `q`'s characters in `name` (greedy), or -1. */
function subsequenceSpan(name: string, q: string): number {
  let start = -1;
  let j = 0;
  for (let i = 0; i < name.length && j < q.length; i++) {
    if (name[i] === q[j]) {
      if (j === 0) start = i;
      j++;
      if (j === q.length) return i - start + 1;
    }
  }
  return -1;
}

/** Rank of a folded `name` for the folded, non-empty query `q`. */
export function rankName(name: string, q: string): number {
  if (!q || !name) return RANK.none;
  if (name === q) return RANK.exact;
  if (name.startsWith(q)) return RANK.prefix;
  if (name.includes(q)) return wordStartIndex(name, q) ? RANK.wordPrefix : RANK.contains;
  // Multi-word query: every query word starts a word of the name ("dom ja" → "Dom Jana").
  const qs = words(q);
  if (qs.length > 1) {
    const ws = words(name);
    if (qs.every((qw) => ws.some((w) => w.startsWith(qw)))) return RANK.wordPrefix;
  }
  if (q.length >= 4) {
    const max = q.length >= 7 ? 2 : 1;
    for (const w of words(name)) {
      if (withinEditDistance(q, w.slice(0, q.length), max) || withinEditDistance(q, w, max)) return RANK.fuzzyClose;
    }
  }
  if (q.length >= 2) {
    const span = subsequenceSpan(name, q);
    if (span !== -1) return span <= q.length * 2 ? RANK.fuzzyClose : RANK.fuzzy;
  }
  return RANK.none;
}

/** Does `q` occur in `name` at the start of a word? */
function wordStartIndex(name: string, q: string): boolean {
  let i = name.indexOf(q);
  while (i !== -1) {
    if (i === 0 || WORD_SPLIT_RE.test(name[i - 1]!)) return true;
    i = name.indexOf(q, i + 1);
  }
  return false;
}

/** One candidate name of a page (its title, its path or one of its aliases). */
export interface NameCandidate {
  /** Folded name. */
  folded: string;
  /** Alias as written when this candidate is an alias. */
  alias?: string;
}

/** Best rank of a page given its candidate names; returns the winning candidate too. */
export function bestCandidate(cands: readonly NameCandidate[], q: string): { rank: number; cand: NameCandidate | null } {
  let rank: number = RANK.none;
  let cand: NameCandidate | null = null;
  for (const c of cands) {
    const r = rankName(c.folded, q);
    // Titles win ties against aliases (they are listed first).
    if (r > rank) {
      rank = r;
      cand = c;
    }
  }
  return { rank, cand };
}

/** Fold a user query for suggestions (trims, collapses whitespace). */
export function foldQuery(q: string): string {
  return fold(q.trim().replace(/\s+/g, ' '));
}
