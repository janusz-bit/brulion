/**
 * Fuzzy matching for the command palette and quick switcher: case- and diacritic-insensitive
 * ("zrodlo" finds "Źródło", "lodz" finds "Łódź"), subsequence matching with bonuses for word
 * starts and consecutive characters. Several words in the query must all match (any order).
 */

/** Fold one character: lower case, no diacritics (ł → l). Always returns one character. */
export function foldChar(ch: string): string {
  if (ch === 'ł' || ch === 'Ł') return 'l';
  const f = ch.normalize('NFD').replace(/\p{M}/gu, '').toLowerCase();
  return f.length === 1 ? f : (f[0] ?? ch);
}

/** Fold a string character by character (same length as the input in UTF-16 units of BMP text). */
export function foldString(s: string): string {
  let out = '';
  for (let i = 0; i < s.length; i++) out += foldChar(s[i]!);
  return out;
}

export interface FuzzyMatch {
  score: number;
  /** Indices of matched characters in the text (for highlighting), ascending. */
  indices: number[];
}

const WORD_SEP = /[\s\-_/.:()[\]#„”"']/;

function isWordStart(text: string, i: number): boolean {
  if (i === 0) return true;
  const prev = text[i - 1]!;
  if (WORD_SEP.test(prev)) return true;
  const cur = text[i]!;
  // camelCase boundary
  return prev === prev.toLowerCase() && cur !== cur.toLowerCase();
}

/** Score one query word against the (original) text; null when it does not match. */
function matchWord(word: string, text: string, folded: string): FuzzyMatch | null {
  if (!word) return { score: 0, indices: [] };
  // Contiguous substring: strongest signal.
  const at = folded.indexOf(word);
  if (at !== -1) {
    let score = 100 + word.length * 10;
    if (at === 0) score += 60;
    else if (isWordStart(text, at)) score += 40;
    score -= Math.min(at, 30);
    return { score, indices: Array.from({ length: word.length }, (_, k) => at + k) };
  }
  // Subsequence, greedily preferring word starts.
  const indices: number[] = [];
  let score = 0;
  let from = 0;
  let prev = -2;
  for (const ch of word) {
    let found = -1;
    // Prefer the next word start that matches, else the first occurrence.
    for (let i = from; i < folded.length; i++) {
      if (folded[i] !== ch) continue;
      if (found === -1) found = i;
      if (i === prev + 1 || isWordStart(text, i)) {
        found = i;
        break;
      }
    }
    if (found === -1) return null;
    if (found === prev + 1) score += 8;
    if (isWordStart(text, found)) score += 10;
    score -= Math.min(found - from, 10);
    indices.push(found);
    prev = found;
    from = found + 1;
  }
  return { score: score + word.length * 2, indices };
}

/**
 * Match `query` against `text`. Empty queries match everything with score 0. Returns null when
 * some query word does not match.
 */
export function fuzzyMatch(query: string, text: string): FuzzyMatch | null {
  const words = foldString(query.trim()).split(/\s+/).filter(Boolean);
  if (words.length === 0) return { score: 0, indices: [] };
  const folded = foldString(text);
  let score = 0;
  const all = new Set<number>();
  for (const w of words) {
    const m = matchWord(w, text, folded);
    if (!m) return null;
    score += m.score;
    for (const i of m.indices) all.add(i);
  }
  // Shorter texts rank higher for the same match quality.
  score -= Math.min(text.length, 80) * 0.2;
  // Whole-text match.
  if (folded === words.join(' ')) score += 50;
  return { score, indices: [...all].sort((a, b) => a - b) };
}

/**
 * Filter and sort `items` by the best match of `query` against their texts (`texts(item)`
 * returns the main text first; extra texts such as keywords count a little less).
 */
export function fuzzyFilter<T>(
  items: readonly T[],
  query: string,
  texts: (item: T) => readonly string[],
  limit = 50,
): { item: T; match: FuzzyMatch }[] {
  if (!query.trim()) return items.slice(0, limit).map((item) => ({ item, match: { score: 0, indices: [] } }));
  const out: { item: T; match: FuzzyMatch; order: number }[] = [];
  items.forEach((item, order) => {
    const [main = '', ...extra] = texts(item);
    let best = fuzzyMatch(query, main);
    for (const t of extra) {
      const m = fuzzyMatch(query, t);
      if (m && (!best || m.score * 0.8 > best.score)) best = { score: m.score * 0.8, indices: [] };
    }
    if (best) out.push({ item, match: best, order });
  });
  out.sort((a, b) => b.match.score - a.match.score || a.order - b.order);
  return out.slice(0, limit).map(({ item, match }) => ({ item, match }));
}

/** Split `text` into highlighted / plain segments by match indices. */
export function highlightSegments(text: string, indices: readonly number[]): { text: string; hit: boolean }[] {
  if (indices.length === 0) return [{ text, hit: false }];
  const set = new Set(indices);
  const out: { text: string; hit: boolean }[] = [];
  for (let i = 0; i < text.length; i++) {
    const hit = set.has(i);
    const last = out[out.length - 1];
    if (last && last.hit === hit) last.text += text[i];
    else out.push({ text: text[i]!, hit });
  }
  return out;
}
