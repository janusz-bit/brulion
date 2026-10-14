/**
 * Full-text search over notes (MiniSearch): fields title, aliases and content; terms are
 * folded (lower-case, no diacritics) so "zolw" finds "żółw"; prefix + fuzzy matching with
 * title/alias boosts. Results carry the matched document terms so the caller can build an
 * excerpt around the first occurrence ({@link findExcerpt}).
 */
import MiniSearch, { type SearchOptions } from 'minisearch';
import type { VaultPath } from '@/core/types';
import { fold, foldWithMap, lineExcerpt } from './text';

/** Document stored in MiniSearch (indexed fields are pre-folded; `name` is the stored title). */
interface SearchDoc {
  id: VaultPath;
  title: string;
  aliases: string;
  content: string;
  name: string;
}

/** A raw search hit (before excerpts are added). */
export interface RawHit {
  path: VaultPath;
  title: string;
  score: number;
  /** Matched document terms (folded). */
  terms: string[];
}

/** Fuzzy edit distance: 20% of the term length, only for terms of 4+ characters. */
function fuzzyFor(term: string): number | false {
  return term.length >= 4 ? 0.2 : false;
}

/** Split already folded text into terms (folded text has no combining marks). */
function tokenizeFolded(text: string): string[] {
  return text.split(FOLDED_SPLIT_RE).filter(Boolean);
}

const FOLDED_SPLIT_RE = /[^\p{L}\p{N}]+/u;

/** Terms are folded before tokenising, so term processing is the identity. */
function keepTerm(term: string): string | null {
  return term || null;
}

/** Query tokenizer: fold first (also NFD input), then split. */
function tokenizeQuery(q: string): string[] {
  return tokenizeFolded(fold(q));
}

const SEARCH_OPTIONS: SearchOptions = {
  prefix: true,
  fuzzy: fuzzyFor,
  boost: { title: 3, aliases: 2 },
  tokenize: tokenizeQuery,
  processTerm: keepTerm,
};

function createMini(): MiniSearch<SearchDoc> {
  return new MiniSearch<SearchDoc>({
    fields: ['title', 'aliases', 'content'],
    storeFields: ['name'],
    // Whole fields are folded once (vectorised) instead of folding every term.
    tokenize: tokenizeFolded,
    processTerm: keepTerm,
    searchOptions: SEARCH_OPTIONS,
  });
}

/** Incremental MiniSearch wrapper keyed by vault path. */
export class SearchEngine {
  private mini = createMini();

  /** Add or replace the document of `path`. */
  set(path: VaultPath, title: string, aliases: readonly string[], content: string): void {
    const doc: SearchDoc = { id: path, title: fold(title), aliases: fold(aliases.join('\n')), content: fold(content), name: title };
    if (this.mini.has(path)) this.mini.replace(doc);
    else this.mini.add(doc);
  }

  /** Forget `path` (lazy discard; MiniSearch vacuums in the background). */
  remove(path: VaultPath): void {
    if (this.mini.has(path)) this.mini.discard(path);
  }

  clear(): void {
    this.mini = createMini();
  }

  /** Number of indexed documents. */
  get size(): number {
    return this.mini.documentCount;
  }

  /**
   * Search: all terms must match (AND); when that finds nothing for a multi-word query, any
   * term may match (OR). Best first, at most `limit` hits.
   */
  search(query: string, limit: number): RawHit[] {
    const q = query.trim();
    if (!q || limit <= 0) return [];
    let results = this.mini.search(q, { combineWith: 'AND' });
    if (results.length === 0 && tokenizeQuery(q).length > 1) results = this.mini.search(q, { combineWith: 'OR' });
    const out: RawHit[] = [];
    for (const r of results.slice(0, limit)) {
      out.push({ path: r.id as VaultPath, title: String(r.name ?? ''), score: r.score, terms: r.terms });
    }
    return out;
  }
}

/** Index of `term` in folded `hay` as a whole token (letters/digits on neither side), or -1. */
function indexOfToken(hay: string, term: string): number {
  if (!term) return -1;
  let i = hay.indexOf(term);
  while (i !== -1) {
    if (!isTokenChar(hay, i - 1) && !isTokenChar(hay, i + term.length)) return i;
    i = hay.indexOf(term, i + 1);
  }
  return -1;
}

const TOKEN_CHAR_RE = /[\p{L}\p{N}\p{M}]/u;

/** Is the UTF-16 unit at `i` part of a token? (Folded text: surrogates count as letters.) */
function isTokenChar(s: string, i: number): boolean {
  if (i < 0 || i >= s.length) return false;
  const c = s.charCodeAt(i);
  if (c >= 0xd800 && c <= 0xdfff) return true;
  return TOKEN_CHAR_RE.test(s[i]!);
}

/**
 * First line of `content` (0-based, from `fromLine`) containing one of the folded `terms` as a
 * whole token, with an excerpt around it. Null when no line contains any term (e.g. a title-only
 * match).
 */
export function findExcerpt(content: string, terms: readonly string[], fromLine = 0): { line: number; excerpt: string } | null {
  const wanted = terms.filter(Boolean);
  if (wanted.length === 0) return null;
  const lines = content.split(/\r\n|\r|\n/);
  for (let i = Math.max(0, fromLine); i < lines.length; i++) {
    const line = lines[i]!;
    const folded = fold(line);
    let best = -1;
    let bestTerm = '';
    for (const t of wanted) {
      const k = indexOfToken(folded, t);
      if (k !== -1 && (best === -1 || k < best)) {
        best = k;
        bestTerm = t;
      }
    }
    if (best === -1) continue;
    const fm = foldWithMap(line);
    const k = fm.text === folded ? best : Math.max(0, indexOfToken(fm.text, bestTerm));
    const start = fm.map[k] ?? 0;
    const end = fm.map[Math.min(k + bestTerm.length, fm.text.length)] ?? line.length;
    return { line: i, excerpt: lineExcerpt(line, start, end) };
  }
  return null;
}
