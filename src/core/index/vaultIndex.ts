/**
 * `VaultIndex` — incremental in-memory index of all notes of a vault (docs/ARCHITECTURE.md):
 * parsed notes, link resolution, backlinks, unlinked references, block ids, tags, tasks,
 * reminders, journals, full-text search, suggestions and the graph.
 *
 * Every change (`update`, `remove`, `rename`, attachments, `setConfig`, `clear`) is applied
 * incrementally: the note is parsed once, its entries in the lookup maps are swapped, and only
 * the links that the change can affect are re-resolved — every link is filed under its target's
 * base-name key, and adding/removing a file (or changing aliases) re-resolves exactly the links
 * filed under the file's keys. So a link to a missing page becomes resolved as soon as the page
 * is created, and a link can move to a shorter path that appears later.
 *
 * Listeners registered with {@link VaultIndex.subscribe} are notified once per microtask after
 * any number of changes; {@link VaultIndex.version} increments with every change.
 */
import { blockAtLine, parseNote } from '@/core/markdown';
import { isConflictCopy, isIgnoredPath, isMarkdown, pageTitle, stripMdExtension } from '@/core/paths';
import {
  DEFAULT_VAULT_CONFIG,
  type Backlink,
  type Block,
  type BlockRef,
  type GraphData,
  type Link,
  type LocalDate,
  type ParsedNote,
  type ReminderRef,
  type SearchHit,
  type TaskRef,
  type VaultConfig,
  type VaultPath,
} from '@/core/types';
import {
  aliasLinkKey,
  cleanTarget,
  countAdd,
  countDelete,
  fileNameKey,
  isExternalTarget,
  linkKey,
  multiAdd,
  multiDelete,
  NameTable,
  noteNameKey,
} from './resolve';
import { findExcerpt, SearchEngine } from './search';
import { bestCandidate, foldQuery, RANK, type NameCandidate } from './suggest';
import { compareCode, compareNames, excerptStart, fold, foldedText, indexOfWordStart } from './text';
import { findMentions, mentionMatchers } from './unlinked';

/** A path-based link of a note and the file it currently resolves to. */
export interface ResolvedLink {
  link: Link;
  /** Resolved file (note or attachment), null when the target does not exist. */
  target: VaultPath | null;
}

/** A tag with usage statistics (see {@link VaultIndex.tags}). */
export interface TagInfo {
  /** Canonical tag (lower-case, NFC, without `#`) — the form used in `ParsedNote.tags`. */
  tag: string;
  /** Occurrences: tagged blocks, plus one per note tagged only in its frontmatter. */
  count: number;
  /** Notes with the tag, sorted. */
  paths: VaultPath[];
  /** Spelling as first written (e.g. "Praca" for #Praca). */
  display: string;
}

/** A link target that does not exist (yet). */
export interface MissingPage {
  /** Target as written in the first link found (fragment stripped). */
  name: string;
  /** Notes linking to it, sorted. */
  sources: VaultPath[];
}

/** An unlinked mention of a page (see {@link VaultIndex.unlinkedReferences}). */
export interface UnlinkedReference {
  path: VaultPath;
  line: number;
  excerpt: string;
}

/** Page suggestion (see {@link VaultIndex.suggestPages}). */
export interface PageSuggestion {
  path: VaultPath;
  title: string;
  /** Set when the match came from a frontmatter alias. */
  alias?: string;
}

/** Internal per-note record. */
interface Entry {
  path: VaultPath;
  content: string;
  note: ParsedNote;
  /** Update sequence number (higher = more recently updated). */
  seq: number;
  /** Path-based links (wiki, embeds, Markdown links to files) with their resolution. */
  links: (ResolvedLink & { key: string })[];
  /** Block references `((id))` / `!((id))`. */
  refs: Link[];
  /** Names for suggestions: title, path (folded) and aliases. */
  names: NameCandidate[];
  foldedPath: string;
  /** Lazily: NFC lower-cased content (unlinked references pre-filter). */
  lower?: string;
  /** Lazily: tag key → spelling as written. */
  tagCase?: Map<string, string>;
}

/** Quiet time after the last change before the search index catches up in the background. */
const SEARCH_IDLE_MS = 300;
/** Documents added to the search index per background tick. */
const SEARCH_CHUNK = 100;

/** Most mentions returned by {@link VaultIndex.unlinkedReferences}. */
const MAX_UNLINKED = 200;
/** Default number of search hits. */
const DEFAULT_SEARCH_LIMIT = 50;

/** Can `path` be indexed at all (not ignored, not a sync conflict copy)? */
function indexable(path: VaultPath): boolean {
  return path !== '' && !isIgnoredPath(path) && !isConflictCopy(path);
}

/** Config fields that change parsing results. */
function parseSignature(c: VaultConfig): string {
  return JSON.stringify([c.journalsFolder, c.journalFileFormat, c.defaultReminderTime]);
}

function sameStrings(a: readonly string[], b: readonly string[]): boolean {
  return a.length === b.length && a.every((s, i) => s === b[i]);
}

/** Journals first (newest first), then other notes by path. */
function compareEntries(a: Entry, b: Entry): number {
  const ja = a.note.journalDate;
  const jb = b.note.journalDate;
  if (ja && jb) return compareCode(jb, ja) || compareCode(a.path, b.path);
  if (ja) return -1;
  if (jb) return 1;
  return compareCode(a.path, b.path);
}

/** Tag written in a block's raw text: `#tag` or `#[[tag name]]`. */
const RAW_TAG_RE = /(?:^|[\s(])#(?:\[\[([^\]\n]+)\]\]|([\p{L}\p{N}\p{M}_\-/]+))/gu;

export class VaultIndex {
  private config: VaultConfig;
  private readonly entries = new Map<VaultPath, Entry>();
  private readonly attachmentSet = new Set<VaultPath>();
  private readonly names = new NameTable();
  /** link key → notes having a link with that key. */
  private readonly sourcesByKey = new Map<string, Set<VaultPath>>();
  /** resolved target → notes linking to it → number of such links. */
  private readonly incoming = new Map<VaultPath, Map<VaultPath, number>>();
  /** link key → notes with unresolved links under that key → number of such links. */
  private readonly unresolved = new Map<string, Map<VaultPath, number>>();
  /** block id → notes defining it. */
  private readonly blockOwners = new Map<string, Set<VaultPath>>();
  /** block id → notes referencing it with `((id))`. */
  private readonly blockRefSources = new Map<string, Set<VaultPath>>();
  /** tag → note → occurrences. */
  private readonly tagIndex = new Map<string, Map<VaultPath, number>>();
  private readonly searchEngine = new SearchEngine();
  /** Notes whose search document is out of date (indexed lazily: idle time or next search). */
  private readonly searchDirty = new Set<VaultPath>();
  private searchTimer: ReturnType<typeof setTimeout> | null = null;
  /** Time (ms) of the last change of a search document. */
  private lastChange = 0;
  private readonly listeners = new Set<() => void>();
  /** Derived lists, dropped on every change. */
  private readonly cache = new Map<string, unknown>();
  private versionCounter = 0;
  private seq = 0;
  private notifyQueued = false;

  constructor(config?: VaultConfig) {
    this.config = { ...DEFAULT_VAULT_CONFIG, ...config };
  }

  // -------------------------------------------------------------------------------------------
  // Changes
  // -------------------------------------------------------------------------------------------

  /** Incremented on every change. */
  get version(): number {
    return this.versionCounter;
  }

  /** Change the vault config; notes are re-parsed when parsing-relevant fields changed. */
  setConfig(c: VaultConfig): void {
    const next = { ...DEFAULT_VAULT_CONFIG, ...c };
    const reparse = parseSignature(next) !== parseSignature(this.config);
    this.config = next;
    if (!reparse || this.entries.size === 0) return;
    // A copy: re-indexing moves each entry to the end of the map.
    for (const e of Array.from(this.entries.values())) this.relink(this.putNote(e.path, e.content, { search: false, seq: e.seq }), e.path);
    this.changed();
  }

  /**
   * Index (or re-index) a file. Markdown files are parsed; any other path is remembered as an
   * attachment (content ignored) so `![[photo.png]]` resolves. Ignored paths (`.trash/`, …)
   * and sync conflict copies are skipped. Unchanged content is a no-op.
   */
  update(path: VaultPath, content: string): void {
    if (!indexable(path)) return;
    if (!isMarkdown(path)) {
      this.updateAttachment(path);
      return;
    }
    const prev = this.entries.get(path);
    if (prev && prev.content === content) return;
    this.relink(this.putNote(path, content, { search: true }), path);
    this.changed();
  }

  /** Remember a non-Markdown file (image, PDF, …) as a link target. */
  updateAttachment(path: VaultPath): void {
    if (!indexable(path) || isMarkdown(path) || this.attachmentSet.has(path)) return;
    this.attachmentSet.add(path);
    this.names.addAttachment(path);
    this.relink(new Set([fileNameKey(path)]), null);
    this.changed();
  }

  /** Forget an attachment. */
  removeAttachment(path: VaultPath): void {
    if (!this.attachmentSet.delete(path)) return;
    this.names.removeAttachment(path);
    this.relink(new Set([fileNameKey(path)]), null);
    this.changed();
  }

  /** Forget a note or attachment. */
  remove(path: VaultPath): void {
    const e = this.entries.get(path);
    if (e) {
      this.detachNote(e, true);
      this.relink(this.entryKeys(e), null);
      this.changed();
      return;
    }
    this.removeAttachment(path);
  }

  /** A file moved: forget `oldPath`, index `newPath` with `content` (one change). */
  rename(oldPath: VaultPath, newPath: VaultPath, content: string): void {
    if (oldPath === newPath) {
      this.update(newPath, content);
      return;
    }
    const keys = new Set<string>();
    let touched = false;
    const old = this.entries.get(oldPath);
    if (old) {
      this.detachNote(old, true);
      for (const k of this.entryKeys(old)) keys.add(k);
      touched = true;
    } else if (this.attachmentSet.delete(oldPath)) {
      this.names.removeAttachment(oldPath);
      keys.add(fileNameKey(oldPath));
      touched = true;
    }
    if (indexable(newPath)) {
      if (isMarkdown(newPath)) {
        for (const k of this.putNote(newPath, content, { search: true })) keys.add(k);
        touched = true;
      } else if (!this.attachmentSet.has(newPath)) {
        this.attachmentSet.add(newPath);
        this.names.addAttachment(newPath);
        keys.add(fileNameKey(newPath));
        touched = true;
      }
    }
    this.relink(keys, isMarkdown(newPath) ? newPath : null);
    if (touched) this.changed();
  }

  /** Forget everything. */
  clear(): void {
    this.entries.clear();
    this.attachmentSet.clear();
    this.names.clear();
    this.sourcesByKey.clear();
    this.incoming.clear();
    this.unresolved.clear();
    this.blockOwners.clear();
    this.blockRefSources.clear();
    this.tagIndex.clear();
    this.cancelSearchTimer();
    this.searchDirty.clear();
    this.searchEngine.clear();
    this.changed();
  }

  /**
   * Bring the full-text index up to date now. Search documents are otherwise built lazily —
   * in the background once changes pause, or at the latest by the next {@link search} — so
   * bulk indexing is not slowed down by the search index.
   */
  flushSearch(): void {
    this.cancelSearchTimer();
    for (const p of this.searchDirty) this.indexSearchDoc(p);
    this.searchDirty.clear();
  }

  /** Stop background work (pending search documents are built on the next search). */
  dispose(): void {
    this.cancelSearchTimer();
    this.listeners.clear();
  }

  /** Subscribe to changes (batched: at most one call per microtask). Returns unsubscribe. */
  subscribe(cb: () => void): () => void {
    this.listeners.add(cb);
    return () => {
      this.listeners.delete(cb);
    };
  }

  // -------------------------------------------------------------------------------------------
  // Notes
  // -------------------------------------------------------------------------------------------

  note(path: VaultPath): ParsedNote | undefined {
    return this.entries.get(path)?.note;
  }

  /** All notes, sorted by path. */
  notes(): ParsedNote[] {
    return this.cached('notes', () => this.sortedEntries().map((e) => e.note));
  }

  /** All note paths, sorted. */
  paths(): VaultPath[] {
    return this.cached('paths', () => this.sortedEntries().map((e) => e.path));
  }

  /** Known attachment paths, sorted. */
  attachments(): VaultPath[] {
    return this.cached('attachments', () => [...this.attachmentSet].sort(compareCode));
  }

  /** Raw content of a note as last indexed. */
  content(path: VaultPath): string | undefined {
    return this.entries.get(path)?.content;
  }

  /** Is `path` an indexed note or attachment? */
  has(path: VaultPath): boolean {
    return this.entries.has(path) || this.attachmentSet.has(path);
  }

  // -------------------------------------------------------------------------------------------
  // Links
  // -------------------------------------------------------------------------------------------

  /** Resolve a link target (docs/SYNTAX.md §2); `#fragment` / `|display` are ignored. */
  resolveLink(target: string, from?: VaultPath): VaultPath | null {
    return this.names.resolve(target, from);
  }

  /**
   * Links of a note with their current resolution, in document order: wiki links, embeds,
   * Markdown links to files (URLs excluded) and block references (resolved to the note that
   * defines the block).
   */
  outgoingLinks(path: VaultPath): ResolvedLink[] {
    const e = this.entries.get(path);
    if (!e) return [];
    const out: ResolvedLink[] = e.links.map((l) => ({ link: l.link, target: l.target }));
    for (const r of e.refs) out.push({ link: r, target: r.blockId ? this.blockOwner(r.blockId) : null });
    return out.sort((a, b) => a.link.from - b.link.from);
  }

  /**
   * Links from other notes to `path`: wiki links, embeds, Markdown links and block references
   * `((id))` to blocks of `path`, each with its containing block. Journals first (newest
   * first), then other notes by path; links in document order.
   */
  backlinks(path: VaultPath): Backlink[] {
    const out: { entry: Entry; link: Link }[] = [];
    for (const src of this.incoming.get(path)?.keys() ?? []) {
      if (src === path) continue;
      const se = this.entries.get(src);
      if (!se) continue;
      for (const l of se.links) if (l.target === path) out.push({ entry: se, link: l.link });
    }
    const e = this.entries.get(path);
    if (e) {
      for (const id of Object.keys(e.note.blockIds)) {
        if (this.blockOwner(id) !== path) continue;
        for (const src of this.blockRefSources.get(id) ?? []) {
          if (src === path) continue;
          const se = this.entries.get(src);
          if (!se) continue;
          for (const l of se.refs) if (l.blockId === id) out.push({ entry: se, link: l });
        }
      }
    }
    out.sort((a, b) => compareEntries(a.entry, b.entry) || a.link.from - b.link.from);
    return out.map(({ entry, link }) => {
      const bl: Backlink = { path: entry.path, link };
      const block = blockAtLine(entry.note, link.line);
      if (block) bl.block = block;
      return bl;
    });
  }

  /**
   * Notes whose path-based links (not `((id))` references) resolve to `path`, sorted —
   * the files to rewrite when `path` is renamed. Includes `path` itself when it links to
   * itself by name.
   */
  linksTo(path: VaultPath): VaultPath[] {
    const out: VaultPath[] = [];
    for (const src of this.incoming.get(path)?.keys() ?? []) {
      if (src === path) {
        const self = this.entries.get(src);
        if (!self?.links.some((l) => l.target === path && l.key !== '')) continue;
      }
      out.push(src);
    }
    return out.sort(compareCode);
  }

  /**
   * Plain-text mentions of the page's title or aliases in other notes (whole words,
   * case-insensitive; not in links, code, URLs, tags or frontmatter). One per line, at most 200.
   */
  unlinkedReferences(path: VaultPath): UnlinkedReference[] {
    const e = this.entries.get(path);
    if (!e) return [];
    const matchers = mentionMatchers([e.note.title, ...e.note.aliases]);
    if (matchers.length === 0) return [];
    const out: UnlinkedReference[] = [];
    for (const other of this.cached('byRelevance', () => [...this.entries.values()].sort(compareEntries))) {
      if (other.path === path) continue;
      const lower = (other.lower ??= other.content.normalize('NFC').toLowerCase());
      if (!matchers.some((m) => lower.includes(m.lower))) continue;
      for (const m of findMentions(other.content, other.note, matchers, MAX_UNLINKED - out.length)) {
        out.push({ path: other.path, line: m.line, excerpt: m.excerpt });
      }
      if (out.length >= MAX_UNLINKED) break;
    }
    return out;
  }

  /** Link targets that do not exist, most linked first. */
  missingPages(): MissingPage[] {
    return this.cached('missing', () => {
      const out: (MissingPage & { count: number })[] = [];
      for (const [key, sources] of this.unresolved) {
        const sorted = [...sources.keys()].sort(compareCode);
        const name = this.missingName(key, sorted);
        if (name !== null) out.push({ name, sources: sorted, count: sorted.length });
      }
      out.sort((a, b) => b.count - a.count || compareNames(a.name, b.name));
      return out.map(({ name, sources }) => ({ name, sources }));
    });
  }

  // -------------------------------------------------------------------------------------------
  // Blocks, tags, tasks, reminders, journals
  // -------------------------------------------------------------------------------------------

  /**
   * Block with explicit id `id` anywhere in the vault (`^id` or Logseq `id::`). A leading `^`
   * or surrounding `((…))` is accepted. Duplicated ids: the note with the smallest path wins.
   */
  blockById(id: string): BlockRef | null {
    const clean = id.trim().replace(/^\(\((.*)\)\)$/, '$1').replace(/^\^/, '');
    const owner = this.blockOwner(clean);
    if (!owner) return null;
    const block = this.entries.get(owner)?.note.blockIds[clean];
    return block ? { path: owner, block } : null;
  }

  /** Tags with usage counts, most used first (ties alphabetically). */
  tags(): TagInfo[] {
    return this.cached('tags', () => {
      const out: TagInfo[] = [];
      for (const [tag, perPath] of this.tagIndex) {
        let count = 0;
        for (const c of perPath.values()) count += c;
        const paths = [...perPath.keys()].sort(compareCode);
        out.push({ tag, count, paths, display: this.tagDisplay(tag, paths) });
      }
      return out.sort((a, b) => b.count - a.count || compareNames(a.tag, b.tag));
    });
  }

  /** All tasks of the vault (notes by path, tasks in document order). */
  tasks(): TaskRef[] {
    return this.cached('tasks', () => {
      const out: TaskRef[] = [];
      for (const e of this.sortedEntries()) {
        if (e.note.tasks.length === 0) continue;
        for (const block of e.note.allBlocks) if (block.task) out.push({ path: e.path, task: block.task, block });
      }
      return out;
    });
  }

  /** All reminders of the vault (notes by path, document order), with the task if any. */
  reminders(): ReminderRef[] {
    return this.cached('reminders', () => {
      const out: ReminderRef[] = [];
      for (const e of this.sortedEntries()) {
        if (e.note.reminders.length === 0) continue;
        for (const block of e.note.allBlocks) {
          for (const reminder of block.reminders) {
            const ref: ReminderRef = { path: e.path, reminder, block };
            if (block.task) ref.task = block.task;
            out.push(ref);
          }
        }
      }
      return out;
    });
  }

  /** Journal pages, newest first. */
  journals(): { date: LocalDate; path: VaultPath }[] {
    return this.cached('journals', () => {
      const out: { date: LocalDate; path: VaultPath }[] = [];
      for (const e of this.entries.values()) if (e.note.journalDate) out.push({ date: e.note.journalDate, path: e.path });
      return out.sort((a, b) => compareCode(b.date, a.date) || compareCode(a.path, b.path));
    });
  }

  // -------------------------------------------------------------------------------------------
  // Search and suggestions
  // -------------------------------------------------------------------------------------------

  /**
   * Full-text search (title ×3, aliases ×2, content; prefix + fuzzy; diacritic-insensitive).
   * Each hit has the first matching line and an excerpt around the match.
   */
  search(q: string, opts?: { limit?: number }): SearchHit[] {
    const limit = opts?.limit ?? DEFAULT_SEARCH_LIMIT;
    if (!q.trim()) return [];
    this.flushSearch();
    const out: SearchHit[] = [];
    for (const raw of this.searchEngine.search(q, limit)) {
      const e = this.entries.get(raw.path);
      if (!e) continue;
      const hit: SearchHit = { path: raw.path, title: e.note.title, score: raw.score };
      // Prefer a match in the body over one in the frontmatter (aliases, tags).
      const body = bodyStartLine(e.note);
      const found = (body > 0 ? findExcerpt(e.content, raw.terms, body) : null) ?? findExcerpt(e.content, raw.terms);
      if (found) {
        hit.line = found.line;
        hit.excerpt = found.excerpt;
      } else {
        const first = e.note.allBlocks.find((b) => b.kind !== 'frontmatter' && b.text.trim() !== '');
        if (first) hit.excerpt = excerptStart(first.text);
      }
      out.push(hit);
    }
    return out;
  }

  /**
   * Pages for `[[` completion / quick switcher: exact > starts with > word starts with >
   * contains > fuzzy, over titles, aliases (then `alias` is set) and — when the query has a
   * `/` — paths. Case- and diacritic-insensitive. Empty prefix: most recently updated first.
   */
  suggestPages(prefix: string, limit = 20): PageSuggestion[] {
    const q = foldQuery(prefix);
    if (limit <= 0) return [];
    if (!q) {
      return [...this.entries.values()]
        .sort((a, b) => b.seq - a.seq)
        .slice(0, limit)
        .map((e) => ({ path: e.path, title: e.note.title }));
    }
    const withPath = q.includes('/');
    const scored: { s: PageSuggestion; rank: number; alias: boolean; name: string; len: number; seq: number }[] = [];
    for (const e of this.entries.values()) {
      const cands = withPath ? [...e.names, { folded: e.foldedPath }] : e.names;
      const { rank, cand } = bestCandidate(cands, q);
      if (rank === RANK.none || !cand) continue;
      const s: PageSuggestion = { path: e.path, title: e.note.title };
      if (cand.alias !== undefined) s.alias = cand.alias;
      scored.push({ s, rank, alias: cand.alias !== undefined, name: cand.folded, len: cand.folded.length, seq: e.seq });
    }
    scored.sort(
      (a, b) =>
        b.rank - a.rank ||
        Number(a.alias) - Number(b.alias) ||
        a.len - b.len ||
        // Same name: the page `[[name]]` resolves to (shortest path) first; else most recent.
        (a.name === b.name ? a.s.path.length - b.s.path.length : b.seq - a.seq) ||
        compareNames(a.s.title, b.s.title) ||
        compareCode(a.s.path, b.s.path),
    );
    return scored.slice(0, limit).map((x) => x.s);
  }

  /**
   * Blocks for `((` completion: blocks whose text contains every word of `text`
   * (diacritic-insensitive). Blocks that already have an id come first, then blocks starting
   * with the query, then more recently updated notes. Empty text: blocks with ids of the most
   * recently updated notes, then other blocks.
   */
  suggestBlocks(text: string, limit = 20): BlockRef[] {
    if (limit <= 0) return [];
    const words = foldQuery(text).split(' ').filter(Boolean);
    const byRecency = [...this.entries.values()].sort((a, b) => b.seq - a.seq);
    if (words.length === 0) {
      const withId: BlockRef[] = [];
      const rest: BlockRef[] = [];
      for (const e of byRecency) {
        for (const block of e.note.allBlocks) {
          if (!suggestable(block)) continue;
          if (block.id) withId.push({ path: e.path, block });
          else if (rest.length < limit) rest.push({ path: e.path, block });
        }
        if (withId.length >= limit) break;
      }
      return [...withId, ...rest].slice(0, limit);
    }
    const scored: { ref: BlockRef; score: number; rank: number }[] = [];
    byRecency.forEach((e, rank) => {
      for (const block of e.note.allBlocks) {
        if (!suggestable(block)) continue;
        const f = foldedText(block);
        if (!words.every((w) => f.includes(w))) continue;
        const first = words[0]!;
        const quality = f.startsWith(first) ? 2 : indexOfWordStart(f, first) !== -1 ? 1 : 0;
        scored.push({ ref: { path: e.path, block }, score: (block.id ? 4 : 0) + quality, rank });
      }
    });
    scored.sort((a, b) => b.score - a.score || a.rank - b.rank || a.ref.block.line - b.ref.block.line);
    return scored.slice(0, limit).map((x) => x.ref);
  }

  // -------------------------------------------------------------------------------------------
  // Graph
  // -------------------------------------------------------------------------------------------

  /**
   * Note graph: a node per note (size = number of incident edges), an edge per linked pair
   * (wiki links, embeds, Markdown links and block references; deduplicated, no self-loops,
   * links to attachments skipped). `tags: true` adds `#tag` nodes; `missing: true` adds nodes
   * for link targets that do not exist yet.
   */
  graph(opts?: { tags?: boolean; missing?: boolean }): GraphData {
    const nodes = new Map<string, GraphData['nodes'][number]>();
    const edges = new Map<string, GraphData['edges'][number]>();
    const degree = new Map<string, number>();
    const missingIds = new Map<string, string>();
    const addEdge = (source: string, target: string): void => {
      if (source === target) return;
      const id = `${source}\u0000${target}`;
      if (edges.has(id)) return;
      edges.set(id, { source, target });
      degree.set(source, (degree.get(source) ?? 0) + 1);
      degree.set(target, (degree.get(target) ?? 0) + 1);
    };
    const sorted = this.sortedEntries();
    for (const e of sorted) nodes.set(e.path, { id: e.path, title: e.note.title, size: 0 });
    for (const e of sorted) {
      for (const l of e.links) {
        if (l.target !== null) {
          if (this.entries.has(l.target)) addEdge(e.path, l.target);
        } else if (opts?.missing && l.key && isPageTarget(l.link.target)) {
          const name = cleanTarget(l.link.target).normalize('NFC');
          const k = name.toLowerCase();
          let id = missingIds.get(k);
          if (id === undefined) {
            id = nodes.has(name) ? `missing:${name}` : name;
            missingIds.set(k, id);
            nodes.set(id, { id, title: pageTitle(name), size: 0, missing: true });
          }
          addEdge(e.path, id);
        }
      }
      for (const r of e.refs) {
        const owner = r.blockId ? this.blockOwner(r.blockId) : null;
        if (owner) addEdge(e.path, owner);
      }
      if (opts?.tags) {
        for (const t of e.note.tags) {
          const id = `#${t}`;
          if (!nodes.has(id)) nodes.set(id, { id, title: id, size: 0, tag: true });
          addEdge(e.path, id);
        }
      }
    }
    const out: GraphData = { nodes: [], edges: [...edges.values()] };
    for (const n of nodes.values()) out.nodes.push({ ...n, size: degree.get(n.id) ?? 0 });
    return out;
  }

  // -------------------------------------------------------------------------------------------
  // Internals
  // -------------------------------------------------------------------------------------------

  /** Record a change: bump the version, drop derived lists, notify (batched). */
  private changed(): void {
    this.versionCounter++;
    this.cache.clear();
    if (this.notifyQueued) return;
    this.notifyQueued = true;
    queueMicrotask(() => {
      this.notifyQueued = false;
      for (const cb of Array.from(this.listeners)) {
        try {
          cb();
        } catch (e) {
          console.error('[index] listener failed', e);
        }
      }
    });
  }

  /** Mark a note's search document as stale; the background update starts once changes pause. */
  private markSearch(path: VaultPath): void {
    this.searchDirty.add(path);
    this.lastChange = Date.now();
    // One timer at a time (re-arming a timer per update is costly during bulk indexing).
    this.searchTimer ??= setTimeout(() => this.searchTick(), SEARCH_IDLE_MS);
  }

  /** Background step: index a chunk of stale documents, continue on the next tick. */
  private searchTick(): void {
    this.searchTimer = null;
    const quiet = Date.now() - this.lastChange;
    if (quiet < SEARCH_IDLE_MS) {
      this.searchTimer = setTimeout(() => this.searchTick(), SEARCH_IDLE_MS - quiet);
      return;
    }
    let n = 0;
    for (const p of this.searchDirty) {
      this.indexSearchDoc(p);
      this.searchDirty.delete(p);
      if (++n >= SEARCH_CHUNK) break;
    }
    if (this.searchDirty.size > 0) this.searchTimer = setTimeout(() => this.searchTick(), 0);
  }

  private cancelSearchTimer(): void {
    if (this.searchTimer === null) return;
    clearTimeout(this.searchTimer);
    this.searchTimer = null;
  }

  private indexSearchDoc(path: VaultPath): void {
    const e = this.entries.get(path);
    if (e) this.searchEngine.set(path, e.note.title, e.note.aliases, e.content);
    else this.searchEngine.remove(path);
  }

  private cached<T>(key: string, build: () => T): T {
    if (this.cache.has(key)) return this.cache.get(key) as T;
    const v = build();
    this.cache.set(key, v);
    return v;
  }

  private sortedEntries(): Entry[] {
    return this.cached('sorted', () => [...this.entries.values()].sort((a, b) => compareCode(a.path, b.path)));
  }

  /** Smallest path defining block `id`. */
  private blockOwner(id: string): VaultPath | null {
    const owners = this.blockOwners.get(id);
    if (!owners) return null;
    let best: VaultPath | null = null;
    for (const p of owners) if (best === null || compareCode(p, best) < 0) best = p;
    return best;
  }

  /** Keys whose links may resolve to this note (name + aliases). */
  private entryKeys(e: Entry): Set<string> {
    const keys = new Set<string>([noteNameKey(e.path)]);
    for (const a of e.note.aliases) keys.add(aliasLinkKey(a));
    return keys;
  }

  /**
   * Parse and (re)index a note; returns the link keys whose resolution in *other* notes may
   * have changed (the note's name when new, aliases when they changed).
   */
  private putNote(path: VaultPath, content: string, opts: { search: boolean; seq?: number }): Set<string> {
    const note = parseNote(path, content, this.config);
    const prev = this.entries.get(path);
    if (prev) this.detachNote(prev, false);
    const entry = this.makeEntry(path, content, note, opts.seq ?? ++this.seq);
    this.attachNote(entry);
    if (opts.search) this.markSearch(path);
    if (!prev) return this.entryKeys(entry);
    if (sameStrings(prev.note.aliases, note.aliases)) return new Set();
    const keys = new Set<string>();
    for (const a of prev.note.aliases) keys.add(aliasLinkKey(a));
    for (const a of note.aliases) keys.add(aliasLinkKey(a));
    return keys;
  }

  private makeEntry(path: VaultPath, content: string, note: ParsedNote, seq: number): Entry {
    const links: Entry['links'] = [];
    const refs: Link[] = [];
    for (const link of note.links) {
      if (link.kind === 'blockref' || link.kind === 'blockembed') refs.push(link);
      else if (link.kind === 'url' || (link.kind === 'markdown' && isExternalTarget(link.target))) continue;
      else links.push({ link, key: link.target.trim() ? linkKey(link.target) : '', target: null });
    }
    const names: NameCandidate[] = [{ folded: fold(note.title) }];
    for (const alias of note.aliases) names.push({ folded: fold(alias), alias });
    return { path, content, note, seq, links, refs, names, foldedPath: fold(stripMdExtension(path)) };
  }

  /** Put an entry into every map and resolve its links. */
  private attachNote(e: Entry): void {
    this.entries.set(e.path, e);
    this.names.addNote(e.path, e.note.aliases);
    for (const l of e.refs) if (l.blockId) multiAdd(this.blockRefSources, l.blockId, e.path);
    for (const id of Object.keys(e.note.blockIds)) multiAdd(this.blockOwners, id, e.path);
    for (const tag of e.note.tags) {
      let perPath = this.tagIndex.get(tag);
      if (!perPath) this.tagIndex.set(tag, (perPath = new Map()));
      let n = 0;
      for (const b of e.note.allBlocks) if (b.tags.includes(tag)) n++;
      perPath.set(e.path, Math.max(1, n));
    }
    this.resolveLinks(e);
  }

  /** Remove an entry from every map (and optionally from search). */
  private detachNote(e: Entry, discardSearch: boolean): void {
    this.unlinkOutgoing(e);
    this.names.removeNote(e.path, e.note.aliases);
    for (const l of e.refs) if (l.blockId) multiDelete(this.blockRefSources, l.blockId, e.path);
    for (const id of Object.keys(e.note.blockIds)) multiDelete(this.blockOwners, id, e.path);
    for (const tag of e.note.tags) {
      const perPath = this.tagIndex.get(tag);
      perPath?.delete(e.path);
      if (perPath?.size === 0) this.tagIndex.delete(tag);
    }
    this.entries.delete(e.path);
    if (discardSearch) this.markSearch(e.path);
  }

  /** Count a link of `src` in the reverse maps (`add` false = uncount). */
  private fileLink(src: VaultPath, l: Entry['links'][number], add: boolean): void {
    if (l.target !== null) (add ? countAdd : countDelete)(this.incoming, l.target, src);
    else if (l.key) (add ? countAdd : countDelete)(this.unresolved, l.key, src);
  }

  /** Resolve all path-based links of `e` and file them in the reverse maps. */
  private resolveLinks(e: Entry): void {
    for (const l of e.links) {
      l.target = this.names.resolve(l.link.target, e.path);
      if (l.key) multiAdd(this.sourcesByKey, l.key, e.path);
      this.fileLink(e.path, l, true);
    }
  }

  private unlinkOutgoing(e: Entry): void {
    for (const l of e.links) {
      if (l.key) multiDelete(this.sourcesByKey, l.key, e.path);
      this.fileLink(e.path, l, false);
    }
  }

  /** Re-resolve the links filed under one of `keys` in every note except `skip`. */
  private relink(keys: Set<string>, skip: VaultPath | null): void {
    if (keys.size === 0) return;
    const sources = new Set<VaultPath>();
    for (const k of keys) for (const s of this.sourcesByKey.get(k) ?? []) if (s !== skip) sources.add(s);
    for (const s of sources) {
      const e = this.entries.get(s);
      if (!e) continue;
      for (const l of e.links) {
        if (!keys.has(l.key)) continue;
        const next = this.names.resolve(l.link.target, e.path);
        if (next === l.target) continue;
        this.fileLink(e.path, l, false);
        l.target = next;
        this.fileLink(e.path, l, true);
      }
    }
  }

  /** Spelling of a missing target, null when it is not a page (e.g. a missing image). */
  private missingName(key: string, sources: readonly VaultPath[]): string | null {
    for (const src of sources) {
      const e = this.entries.get(src);
      const l = e?.links.find((x) => x.key === key && x.target === null);
      if (l) return isPageTarget(l.link.target) ? cleanTarget(l.link.target).normalize('NFC') : null;
    }
    return null;
  }

  /** First spelling of a tag among `paths` (sorted), else the tag itself. */
  private tagDisplay(tag: string, paths: readonly VaultPath[]): string {
    for (const p of paths) {
      const e = this.entries.get(p);
      if (!e) continue;
      const spelled = (e.tagCase ??= tagSpellings(e.note)).get(tag);
      if (spelled) return spelled;
    }
    return tag;
  }
}

/** First line after the frontmatter (YAML or leading `key:: value` lines), else 0. */
function bodyStartLine(note: ParsedNote): number {
  const first = note.blocks[0];
  return first?.kind === 'frontmatter' ? first.endLine + 1 : 0;
}

/** Can a block be offered for `((` completion? */
function suggestable(block: Block): boolean {
  return block.kind !== 'frontmatter' && block.text.trim() !== '';
}

/** Is an unresolved target a page (not a missing attachment such as `photo.png`)? */
function isPageTarget(target: string): boolean {
  const t = cleanTarget(target);
  const m = /\.([^./\s]+)$/.exec(t);
  return !m || /^(md|markdown)$/i.test(m[1]!);
}

/** Tag key → spelling as written (frontmatter values and `#tags` in blocks). */
function tagSpellings(note: ParsedNote): Map<string, string> {
  const out = new Map<string, string>();
  const add = (name: string): void => {
    const n = name.trim().replace(/^#/, '').replace(/\/+$/, '').normalize('NFC');
    const key = n.toLowerCase();
    if (n && note.tags.includes(key) && !out.has(key)) out.set(key, n);
  };
  for (const k of ['tags', 'tag']) {
    const v = note.frontmatter[k];
    if (typeof v === 'string') for (const part of v.split(/[,\s]+/)) add(part);
    else if (Array.isArray(v)) for (const part of v) if (typeof part === 'string') add(part);
  }
  for (const b of note.allBlocks) {
    if (b.tags.length === 0) continue;
    for (const m of b.raw.matchAll(RAW_TAG_RE)) add(m[1] ?? m[2] ?? '');
  }
  return out;
}
