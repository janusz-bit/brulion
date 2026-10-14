/**
 * In-memory EditorWorkspace for tests and the editor playground: a MemoryFs plus the real
 * `VaultIndex` (@/core/index). Writes to one path are serialised and `modify` is a
 * read-modify-write like the Workspace service's, so save races behave as in the app.
 *
 * Test helpers: `externalWrite()` simulates sync/another app changing a file, `writes` logs
 * editor saves, `opened` logs openExternal() URLs.
 */
import { foldText } from '@/core/dates';
import { VaultIndex } from '@/core/index';
import { ensureBlockId as ensureBlockIdIn, parseNote } from '@/core/markdown';
import { basename, dirname, extname, isMarkdown, joinPath, normalizePath, pageTitle, sanitizeFileName, stripMdExtension, uniquePath } from '@/core/paths';
import { MemoryFs } from '@/core/storage';
import { DEFAULT_VAULT_CONFIG, type BlockRef, type ParsedNote, type VaultConfig, type VaultPath } from '@/core/types';
import type { EditorIndex, EditorWorkspace, FileChange, PageSuggestion } from './context';

/**
 * Minimal EditorIndex over parsed notes (not a VaultIndex): for tests that need an index
 * without VaultIndex's extras (e.g. code that must also work with other EditorIndex
 * implementations). FakeWorkspace itself uses the real VaultIndex.
 */
export class TinyIndex implements EditorIndex {
  private readonly notesByPath = new Map<VaultPath, ParsedNote>();
  private readonly contents = new Map<VaultPath, string>();
  version = 0;

  constructor(private readonly config: VaultConfig = DEFAULT_VAULT_CONFIG) {}

  update(path: VaultPath, content: string): void {
    if (!isMarkdown(path)) return;
    this.contents.set(path, content);
    this.notesByPath.set(path, parseNote(path, content, this.config));
    this.version++;
  }

  remove(path: VaultPath): void {
    this.contents.delete(path);
    this.notesByPath.delete(path);
    this.version++;
  }

  note(path: VaultPath): ParsedNote | undefined {
    return this.notesByPath.get(path);
  }

  content(path: VaultPath): string | undefined {
    return this.contents.get(path);
  }

  paths(): VaultPath[] {
    return [...this.notesByPath.keys()].sort();
  }

  suggestPages(prefix: string, limit = 20): PageSuggestion[] {
    const q = foldText(prefix.trim());
    const scored: { s: PageSuggestion; score: number }[] = [];
    const rank = (name: string) => {
      const f = foldText(name);
      if (!q) return 1;
      if (f.startsWith(q)) return 4;
      if (f.split(/[\s/_-]+/).some((w) => w.startsWith(q))) return 3;
      if (f.includes(q)) return 2;
      return 0;
    };
    for (const note of this.notesByPath.values()) {
      const r = Math.max(rank(note.title), rank(stripMdExtension(note.path)) - 1);
      if (r > 0) scored.push({ s: { path: note.path, title: note.title }, score: r + 0.5 });
      for (const alias of note.aliases) {
        const ra = rank(alias);
        if (ra > 0 && q) scored.push({ s: { path: note.path, title: note.title, alias }, score: ra });
      }
    }
    scored.sort((a, b) => b.score - a.score || a.s.title.length - b.s.title.length || a.s.title.localeCompare(b.s.title));
    return scored.slice(0, limit).map((x) => x.s);
  }

  suggestBlocks(text: string, limit = 20): BlockRef[] {
    const q = foldText(text.trim());
    const out: BlockRef[] = [];
    for (const note of this.notesByPath.values()) {
      for (const block of note.allBlocks) {
        if (block.kind === 'frontmatter' || !block.text.trim()) continue;
        if (q && !foldText(block.text).includes(q)) continue;
        out.push({ path: note.path, block });
        if (out.length >= limit) return out;
      }
    }
    return out;
  }

  resolveLink(target: string, from?: VaultPath): VaultPath | null {
    const name = target.split('#')[0]!.trim();
    if (!name) return from ?? null;
    const lower = normalizePath(name).toLowerCase();
    const withMd = lower.endsWith('.md') ? lower : `${lower}.md`;
    const paths = [...this.notesByPath.keys()];
    const exact = paths.find((p) => p.toLowerCase() === withMd || p.toLowerCase() === lower);
    if (exact) return exact;
    if (from) {
      const rel = joinPath(dirname(from), name).toLowerCase();
      const r = paths.find((p) => p.toLowerCase() === (rel.endsWith('.md') ? rel : `${rel}.md`));
      if (r) return r;
    }
    const base = basename(withMd).normalize('NFC');
    const byName = paths.filter((p) => basename(p).toLowerCase().normalize('NFC') === base).sort((a, b) => a.length - b.length || a.localeCompare(b));
    if (byName.length) return byName[0]!;
    for (const note of this.notesByPath.values()) {
      if (note.aliases.some((a) => a.toLowerCase() === lower)) return note.path;
    }
    return null;
  }

  blockById(id: string): BlockRef | null {
    for (const note of this.notesByPath.values()) {
      const block = note.blockIds[id];
      if (block) return { path: note.path, block };
    }
    return null;
  }

  tags(): { tag: string; count: number; paths: VaultPath[] }[] {
    const map = new Map<string, { tag: string; count: number; paths: Set<VaultPath> }>();
    for (const note of this.notesByPath.values()) {
      const counted = new Set<string>();
      const add = (tag: string) => {
        const key = tag.toLowerCase();
        const e = map.get(key) ?? { tag, count: 0, paths: new Set<VaultPath>() };
        e.count++;
        e.paths.add(note.path);
        map.set(key, e);
        counted.add(key);
      };
      for (const b of note.allBlocks) for (const tag of b.tags) add(tag);
      for (const tag of note.tags) if (!counted.has(tag.toLowerCase())) add(tag);
    }
    return [...map.values()].map((e) => ({ tag: e.tag, count: e.count, paths: [...e.paths] })).sort((a, b) => b.count - a.count || a.tag.localeCompare(b.tag));
  }
}


export interface FakeWorkspaceOptions {
  files?: Record<VaultPath, string | Uint8Array>;
  config?: Partial<VaultConfig>;
  /** Delay of write() in ms (to test in-flight saves). Default 0. */
  writeDelay?: number;
}

const MIME: Record<string, string> = {
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.gif': 'image/gif',
  '.webp': 'image/webp',
  '.svg': 'image/svg+xml',
  '.pdf': 'application/pdf',
};

export class FakeWorkspace implements EditorWorkspace {
  readonly fs: MemoryFs;
  readonly config: VaultConfig;
  readonly index: VaultIndex;
  readonly writes: { path: VaultPath; content: string }[] = [];
  readonly opened: string[] = [];
  readonly platform = {
    openExternal: async (url: string): Promise<void> => {
      this.opened.push(url);
    },
  };
  private readonly listeners = new Set<(changes: readonly FileChange[]) => void>();
  private readonly binaries = new Map<VaultPath, Uint8Array>();
  private readonly urls = new Map<VaultPath, string>();
  private readonly writeDelay: number;
  /** Per-path queue: writes and modifies of one file run one after another. */
  private readonly chains = new Map<VaultPath, Promise<unknown>>();
  private pending: FileChange[] = [];

  constructor(opts: FakeWorkspaceOptions = {}) {
    this.config = { ...DEFAULT_VAULT_CONFIG, ...opts.config };
    this.fs = new MemoryFs({ files: opts.files ?? {} });
    this.index = new VaultIndex(this.config);
    this.writeDelay = opts.writeDelay ?? 0;
    for (const [path, content] of Object.entries(opts.files ?? {})) {
      if (typeof content === 'string') this.index.update(path, content);
      else {
        this.binaries.set(path, content);
        this.index.updateAttachment(path);
      }
    }
  }

  /** Read a note; waits for queued writes of the path (like the vault). */
  async read(path: VaultPath): Promise<string> {
    await this.chains.get(path);
    return this.fs.readText(path);
  }

  private serial<T>(path: VaultPath, fn: () => Promise<T>): Promise<T> {
    const prev = this.chains.get(path) ?? Promise.resolve();
    const next = prev.then(fn, fn);
    const tail = next.catch(() => undefined);
    this.chains.set(path, tail);
    void tail.then(() => {
      if (this.chains.get(path) === tail) this.chains.delete(path);
    });
    return next;
  }

  private async writeNow(path: VaultPath, content: string): Promise<void> {
    if (this.writeDelay > 0) await new Promise((r) => setTimeout(r, this.writeDelay));
    const existed = (await this.fs.stat(path)) !== null;
    await this.fs.writeText(path, content);
    this.writes.push({ path, content });
    this.index.update(path, content);
    this.emit([{ type: existed ? 'modify' : 'create', path, source: 'user' }]);
  }

  /** Write a note (serialised with other writes of the same path, like the Workspace). */
  write(path: VaultPath, content: string): Promise<void> {
    return this.serial(path, () => this.writeNow(path, content));
  }

  /** Read-modify-write (serialised); unchanged content writes nothing. */
  modify(path: VaultPath, fn: (content: string) => string): Promise<string> {
    return this.serial(path, async () => {
      const old = await this.fs.readText(path);
      const next = fn(old);
      if (next !== old) await this.writeNow(path, next);
      return next;
    });
  }

  /** Simulate another device / app changing a file (sync, Syncthing, another editor). */
  externalWrite(path: VaultPath, content: string): void {
    this.fs.externalWrite(path, content, { silent: true });
    this.index.update(path, content);
    this.emit([{ type: 'modify', path, source: 'external' }]);
  }

  /** Simulate a rename done elsewhere (file manager, sync, the shell's rename refactor). */
  async externalRename(from: VaultPath, to: VaultPath): Promise<void> {
    const content = await this.fs.readText(from);
    await this.fs.rename(from, to);
    this.index.rename(from, to, content);
    this.emit([{ type: 'rename', path: to, oldPath: from, source: 'external' }]);
  }

  /** Number of file-change listeners (leak checks in tests). */
  get listenerCount(): number {
    return this.listeners.size;
  }

  onFileChanged(cb: (changes: readonly FileChange[]) => void): () => void {
    this.listeners.add(cb);
    return () => this.listeners.delete(cb);
  }

  /** Changes are delivered in batches after a microtask, like the vault does. */
  private emit(changes: FileChange[]): void {
    const first = this.pending.length === 0;
    this.pending.push(...changes);
    if (!first) return;
    queueMicrotask(() => {
      const batch = this.pending;
      this.pending = [];
      for (const l of Array.from(this.listeners)) l(batch);
    });
  }

  assetUrl(path: VaultPath): string | null {
    const cached = this.urls.get(path);
    if (cached) return cached;
    const bytes = this.binaries.get(path);
    if (!bytes || typeof URL === 'undefined' || typeof URL.createObjectURL !== 'function') return null;
    const url = URL.createObjectURL(new Blob([bytes as BlobPart], { type: MIME[extname(path).toLowerCase()] ?? 'application/octet-stream' }));
    this.urls.set(path, url);
    return url;
  }

  async saveAttachment(name: string, data: Uint8Array): Promise<VaultPath> {
    const folder = this.config.attachmentsFolder;
    const path = uniquePath(joinPath(folder, sanitizeFileName(name)), (p) => this.binaries.has(p) || this.index.note(p) !== undefined);
    await this.fs.writeBinary(path, data);
    this.binaries.set(path, data);
    this.index.updateAttachment(path);
    this.emit([{ type: 'create', path, source: 'user' }]);
    return path;
  }

  async ensureBlockId(path: VaultPath, line: number): Promise<string> {
    let id = '';
    await this.modify(path, (content) => {
      const res = ensureBlockIdIn(content, line);
      id = res.id;
      return res.content;
    });
    return id;
  }

  listTemplates(): VaultPath[] {
    const prefix = `${this.config.templatesFolder.toLowerCase()}/`;
    return this.index.paths().filter((p) => p.toLowerCase().startsWith(prefix));
  }

  /** Title of a page path (convenience for the playground). */
  title(path: VaultPath): string {
    return pageTitle(path);
  }
}
