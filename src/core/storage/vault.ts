/**
 * Vault: the app's view of a folder of notes on top of a FileSystemAdapter.
 *
 * - Keeps a stat cache of all non-ignored files/folders and a content cache of text files.
 * - `write()` is a no-op when the content hash is unchanged, snapshots the previous content
 *   into History, and remembers the hash it wrote so watcher echoes are ignored. Before
 *   overwriting it checks (mtime, size) on disk, so an external change the watcher has not
 *   reported yet is taken in (reported + kept in History) instead of silently overwritten.
 * - Deletes go to `.trash/` (restorable). Renames work for files and whole folders; case-only
 *   renames work on case-insensitive backends and never replace a distinct file. On such
 *   backends a write to "notatka.md" updates the known "Notatka.md" (no phantom duplicate).
 * - External changes (Syncthing, Nextcloud client, other editors, other tabs) arrive through
 *   `fs.watch` (`startWatching`) or `rescan()` and are emitted with source 'external'. An
 *   event for the vault root (`path: ''`) triggers a full rescan (watcher overflow).
 * - Concurrency: operations on the same path are serialised (keyed promise chains); a file
 *   operation holds its folders shared, so different files of one folder are written
 *   concurrently, while folder operations wait for (and block) everything inside the folder.
 * - Changes are delivered to `onChange` listeners in batches (one microtask).
 */
import { isIgnoredPath, isMarkdown, joinPath, normalizePath } from '../paths';
import { DEFAULT_VAULT_CONFIG, type FileStat, type FileSystemAdapter, type FsEvent, type FsEventType, type VaultConfig, type VaultPath } from '../types';
import { CONFIG_PATH, parseVaultConfig, serializeVaultConfig } from './config';
import { FsError, isNotFoundError, requireEntryPath } from './errors';
import { contentHash } from './hash';
import type { History } from './history';
import type { LocalStore } from './localStore';
import { KeyedLock, type LockSpec } from './locks';
import { ancestors, insertBeforeExtension, isStrictlyWithin, isWithin, rebase } from './pathUtil';
import { isTextPath } from './textFiles';

/** Who caused a change. */
export type ChangeSource = 'user' | 'external' | 'sync' | 'import' | 'system';

/** A change to a vault file, delivered to `Vault.onChange` listeners. */
export interface VaultChange {
  type: FsEventType;
  path: VaultPath;
  /** Only for 'rename'. */
  oldPath?: VaultPath;
  source: ChangeSource;
}

export interface VaultOptions {
  id: string;
  name: string;
  /** Device-local store (trash metadata). */
  store: LocalStore;
  /** Version history; when given, previous contents are snapshotted before overwrites. */
  history?: History;
  /** Clock (trash timestamps). Default `Date.now`. */
  now?: () => number;
}

/** Information about an item in `.trash/`. */
export interface TrashInfo {
  /** Where the item was before it was deleted. */
  original: VaultPath;
  /** Deletion time (ms) when deleted on this device. */
  deletedAt?: number;
}

interface TrashMeta {
  original: VaultPath;
  deletedAt: number;
}

export const TRASH_DIR = '.trash';

/** Paths the vault tracks: everything not ignored, plus the vault config file. */
function isTracked(p: VaultPath): boolean {
  return p === CONFIG_PATH || !isIgnoredPath(p);
}

function isInTrash(p: VaultPath): boolean {
  return isWithin(p, TRASH_DIR);
}

/**
 * Lock keys for an operation on a file: the file exclusively, every ancestor folder shared
 * (operations on different files of one folder run concurrently; a folder operation waits).
 */
function fileKeys(p: VaultPath): LockSpec {
  return { exclusive: [`f:${p}`], shared: ancestors(p).map((d) => `d:${d}`) };
}

/** Lock keys for an operation on an entry that may be a file or a folder (and everything inside). */
function entryKeys(p: VaultPath): LockSpec {
  return { exclusive: [`f:${p}`, `d:${p}`], shared: ancestors(p).map((d) => `d:${d}`) };
}

/** Lock keys for an operation on two entries (rename, restore). */
function bothKeys(a: VaultPath, b: VaultPath): LockSpec {
  const ka = entryKeys(a);
  const kb = entryKeys(b);
  return { exclusive: [...ka.exclusive, ...kb.exclusive], shared: [...(ka.shared ?? []), ...(kb.shared ?? [])] };
}

/** Every key of a spec (for waiting). */
function allKeys(spec: LockSpec): string[] {
  return [...spec.exclusive, ...(spec.shared ?? [])];
}

function byPath(a: FileStat, b: FileStat): number {
  return a.path < b.path ? -1 : a.path > b.path ? 1 : 0;
}

const pad2 = (n: number): string => String(n).padStart(2, '0');

/** Local wall-clock stamp "YYYYMMDD-HHmmss". */
export function trashStamp(ms: number): string {
  const d = new Date(ms);
  return `${d.getFullYear()}${pad2(d.getMonth() + 1)}${pad2(d.getDate())}-${pad2(d.getHours())}${pad2(d.getMinutes())}${pad2(d.getSeconds())}`;
}

/** Remove a " (YYYYMMDD-HHmmss)" (optionally " n") trash suffix from one path segment. */
function stripTrashStamp(segment: string): string {
  return segment.replace(/ \(\d{8}-\d{6}\)(?: \d+)?(?=\.[^.]*$|$)/, '');
}

/** Valid trash metadata: an original path that is a real entry outside the trash. */
function isTrashMeta(v: unknown): v is TrashMeta {
  if (typeof v !== 'object' || v === null) return false;
  const { original, deletedAt } = v as Partial<TrashMeta>;
  if (typeof original !== 'string' || typeof deletedAt !== 'number') return false;
  const p = normalizePath(original);
  return p !== '' && !isInTrash(p);
}

/**
 * Pick a non-colliding path by appending " 2", " 3", ... before the extension. Unlike
 * `uniquePath` from paths.ts this keeps the extension's case ("Raport.PDF" -> "Raport 2.PDF").
 */
function uniqueName(base: VaultPath, isTaken: (p: VaultPath) => boolean): VaultPath {
  if (!isTaken(base)) return base;
  for (let i = 2; ; i++) {
    const candidate = insertBeforeExtension(base, ` ${i}`);
    if (!isTaken(candidate)) return candidate;
  }
}

/** True when two stats plausibly describe the same entry (same kind, size and mtime). */
function sameStat(a: FileStat, b: FileStat): boolean {
  return a.kind === b.kind && a.size === b.size && a.mtime === b.mtime;
}

/** Multiset of lower-cased paths, for O(1) case-insensitive "is this name taken?" checks. */
class LowerCaseIndex {
  private readonly counts = new Map<string, number>();

  add(p: VaultPath): void {
    const k = p.toLowerCase();
    this.counts.set(k, (this.counts.get(k) ?? 0) + 1);
  }

  remove(p: VaultPath): void {
    const k = p.toLowerCase();
    const n = (this.counts.get(k) ?? 0) - 1;
    if (n > 0) this.counts.set(k, n);
    else this.counts.delete(k);
  }

  /** `lower` must already be lower-cased. */
  has(lower: string): boolean {
    return this.counts.has(lower);
  }
}

/** Stat map that keeps a shared LowerCaseIndex in sync with its keys. */
class StatMap extends Map<VaultPath, FileStat> {
  constructor(private readonly index: LowerCaseIndex) {
    super();
  }

  override set(key: VaultPath, value: FileStat): this {
    if (!super.has(key)) this.index.add(key);
    return super.set(key, value);
  }

  override delete(key: VaultPath): boolean {
    const had = super.delete(key);
    if (had) this.index.remove(key);
    return had;
  }

  override clear(): void {
    for (const key of super.keys()) this.index.remove(key);
    super.clear();
  }
}

/** Source of a backend listing; one batch of watcher events shares one listing. */
type Lister = () => Promise<FileStat[]>;

/** A Lister that lists at most once (a failed listing is retried on the next call). */
function listOnce(list: Lister): Lister {
  let pending: Promise<FileStat[]> | null = null;
  return () => {
    if (!pending) {
      const p = list();
      pending = p;
      p.catch(() => {
        if (pending === p) pending = null;
      });
    }
    return pending;
  };
}

/** Structural check for watcher events (they come from native code and other tabs). */
function isWellFormedEvent(ev: unknown): ev is FsEvent {
  if (typeof ev !== 'object' || ev === null) return false;
  const { type, path, oldPath } = ev as Partial<FsEvent>;
  return (
    (type === 'create' || type === 'modify' || type === 'delete' || type === 'rename') &&
    typeof path === 'string' &&
    (oldPath === undefined || typeof oldPath === 'string')
  );
}

/** A vault of Markdown notes and attachments. */
export class Vault {
  readonly id: string;
  readonly name: string;
  readonly fs: FileSystemAdapter;
  config: VaultConfig = { ...DEFAULT_VAULT_CONFIG };

  private readonly store: LocalStore;
  private readonly history: History | undefined;
  private readonly now: () => number;
  /** Lower-cased keys of fileStats and dirStats (case-insensitive name collisions). */
  private readonly lowerPaths = new LowerCaseIndex();
  private readonly fileStats = new StatMap(this.lowerPaths);
  private readonly dirStats = new StatMap(this.lowerPaths);
  private readonly contents = new Map<VaultPath, string>();
  private readonly hashes = new Map<VaultPath, string>();
  private readonly generations = new Map<VaultPath, number>();
  private epoch = 0;
  private readonly locks = new KeyedLock();
  /** Lower-cased paths reserved by in-flight create()/restore() calls. */
  private readonly reserved = new Set<string>();
  private readonly listeners = new Set<(changes: VaultChange[]) => void>();
  private pending: VaultChange[] = [];
  private flushScheduled = false;
  private unwatch: (() => void) | null = null;
  private watchChain: Promise<void> = Promise.resolve();
  private rawConfig: Record<string, unknown> = {};
  private sortedFiles: FileStat[] | null = null;

  constructor(fs: FileSystemAdapter, opts: VaultOptions) {
    this.fs = fs;
    this.id = opts.id;
    this.name = opts.name;
    this.store = opts.store;
    this.history = opts.history;
    this.now = opts.now ?? Date.now;
  }

  // -------------------------------------------------------------------------
  // Opening and querying
  // -------------------------------------------------------------------------

  /** List the backend and load `.brulion/config.json`. Can be called again to reload. */
  async open(): Promise<void> {
    const listing = await this.fs.list();
    this.epoch++;
    this.fileStats.clear();
    this.dirStats.clear();
    this.contents.clear();
    this.hashes.clear();
    this.sortedFiles = null;
    for (const st of listing) this.track(st);
    await this.loadConfig();
  }

  /** Non-ignored files (conflict copies included), sorted by path. The stats are read-only. */
  files(): FileStat[] {
    if (!this.sortedFiles) {
      this.sortedFiles = [...this.fileStats.values()].filter((s) => !isIgnoredPath(s.path)).sort(byPath);
    }
    return this.sortedFiles.slice();
  }

  /** Markdown files (`.md`, `.markdown`), sorted by path. */
  markdownFiles(): FileStat[] {
    return this.files().filter((s) => isMarkdown(s.path));
  }

  /** Non-ignored folders (including empty ones), sorted by path. */
  folders(): FileStat[] {
    return [...this.dirStats.values()].sort(byPath);
  }

  /** True if a file or folder is known at `path`. */
  exists(path: VaultPath): boolean {
    const p = normalizePath(path);
    return this.fileStats.has(p) || this.dirStats.has(p);
  }

  /** Cached stat of a file or folder. */
  stat(path: VaultPath): FileStat | undefined {
    const p = normalizePath(path);
    return this.fileStats.get(p) ?? this.dirStats.get(p);
  }

  /** Whether `startWatching()` is active. */
  get watching(): boolean {
    return this.unwatch !== null;
  }

  // -------------------------------------------------------------------------
  // Reading and writing
  // -------------------------------------------------------------------------

  /** Read a text file (served from the content cache when possible; waits for pending writes). */
  async read(path: VaultPath): Promise<string> {
    const p = requireEntryPath(path);
    // Waits for writes to this file and folder operations above it, not for sibling writes.
    await this.locks.waitExclusive(allKeys(entryKeys(p)));
    const cached = this.contents.get(p);
    if (cached !== undefined) return cached;
    const gen = this.generationOf(p);
    const text = await this.fs.readText(p);
    if (gen === this.generationOf(p) && this.fileStats.has(p)) this.cacheText(p, text);
    return text;
  }

  /** Read a file's raw bytes (waits for pending writes). */
  async readBinary(path: VaultPath): Promise<Uint8Array> {
    const p = requireEntryPath(path);
    // Waits for writes to this file and folder operations above it, not for sibling writes.
    await this.locks.waitExclusive(allKeys(entryKeys(p)));
    return this.fs.readBinary(p);
  }

  /**
   * Write a text file. No-op when the content is unchanged. The previous content is
   * snapshotted into History (reason = `source`). Emits 'create' or 'modify'.
   */
  async write(path: VaultPath, content: string, source: ChangeSource = 'user'): Promise<void> {
    const p = requireEntryPath(path);
    const stored = await this.locks.runWith(fileKeys(p), () => this.writeUnlocked(p, content, source));
    // Case-insensitive backend: `p` reaches a known file spelled differently; update that one.
    if (stored !== null) await this.write(stored, content, source);
  }

  /** Write a binary file (attachments). No-op when the bytes are known to be unchanged. */
  async writeBinary(path: VaultPath, data: Uint8Array, source: ChangeSource = 'user'): Promise<void> {
    const p = requireEntryPath(path);
    const stored = await this.locks.runWith(fileKeys(p), () => this.writeUnlocked(p, data, source));
    if (stored !== null) await this.writeBinary(stored, data, source);
  }

  /** Create a new file; if `path` is taken, " 2", " 3", ... is appended. Returns the path used. */
  async create(path: VaultPath, content: string | Uint8Array, source: ChangeSource = 'user'): Promise<VaultPath> {
    const base = requireEntryPath(path);
    const taken = new Set<string>();
    for (;;) {
      const candidate = this.freeCandidate(base, taken);
      const key = candidate.toLowerCase();
      this.reserved.add(key);
      try {
        const created = await this.locks.runWith(fileKeys(candidate), async () => {
          const st = await this.fs.stat(candidate);
          if (st) {
            // Exists on disk but was unknown (external change not seen yet): pick another name.
            this.track(st);
            return false;
          }
          // A case variant is never a candidate (freeCandidate), so this always writes.
          return (await this.writeUnlocked(candidate, content, source)) === null;
        });
        if (created) return candidate;
        taken.add(key);
      } finally {
        this.reserved.delete(key);
      }
    }
  }

  /**
   * Delete a file or folder by moving it to `.trash/<path>` (a " (YYYYMMDD-HHmmss)" suffix
   * is added when that trash path is taken). Paths already inside `.trash/` are deleted
   * permanently. Missing paths are ignored.
   */
  async delete(path: VaultPath, source: ChangeSource = 'user'): Promise<void> {
    const p = requireEntryPath(path);
    if (isInTrash(p)) return this.deletePermanently(p, source);
    await this.locks.runWith(entryKeys(p), async () => {
      const st = this.fileStats.get(p) ?? this.dirStats.get(p) ?? (await this.fs.stat(p));
      if (!st) return;
      const target = await this.trashTarget(p, st.kind);
      try {
        await this.fs.rename(p, target);
      } catch (e) {
        if (!isNotFoundError(e)) throw e;
        // Already gone on disk (stale cache): just forget it.
        this.emit(this.forget(p, source));
        return;
      }
      try {
        const meta: TrashMeta = { original: p, deletedAt: this.now() };
        await this.store.set(this.trashKey(target), meta);
      } catch (e) {
        console.warn('[vault] could not record trash metadata', e);
      }
      this.emit(this.forget(p, source));
    });
  }

  /** Delete a file or folder permanently (no trash). */
  async deletePermanently(path: VaultPath, source: ChangeSource = 'user'): Promise<void> {
    const p = requireEntryPath(path);
    await this.locks.runWith(entryKeys(p), async () => {
      await this.fs.remove(p);
      if (isInTrash(p)) await this.dropTrashMeta(p);
      this.emit(this.forget(p, source));
    });
  }

  /**
   * Rename/move a file or folder. Fails with EEXIST if `to` exists (case-only renames are
   * allowed). Emits one 'rename' change per file (also for every file inside a folder).
   */
  async rename(from: VaultPath, to: VaultPath, source: ChangeSource = 'user'): Promise<void> {
    const a = requireEntryPath(from);
    const b = requireEntryPath(to);
    if (a === b) return;
    if (isStrictlyWithin(b, a)) throw new FsError('EINVAL', b, 'Cannot move a folder into itself');
    await this.locks.runWith(bothKeys(a, b), async () => {
      const st = this.fileStats.get(a) ?? this.dirStats.get(a) ?? (await this.fs.stat(a));
      if (!st) throw new FsError('ENOENT', a);
      await this.assertRenameTargetFree(a, b);
      await this.fs.rename(a, b);
      this.emit(await this.relocate(a, b, st.kind, source));
    });
  }

  /** Save `.brulion/config.json` (unknown keys already in the file are preserved). */
  async saveConfig(cfg: VaultConfig): Promise<void> {
    const text = serializeVaultConfig(cfg, this.rawConfig);
    await this.write(CONFIG_PATH, text, 'user');
    this.applyConfig(text);
  }

  // -------------------------------------------------------------------------
  // Trash
  // -------------------------------------------------------------------------

  /** Files inside `.trash/`, sorted by path. */
  async trash(): Promise<FileStat[]> {
    const listing = await this.fs.list();
    return listing
      .map((s) => ({ ...s, path: normalizePath(s.path) }))
      .filter((s) => s.kind === 'file' && isStrictlyWithin(s.path, TRASH_DIR))
      .sort(byPath);
  }

  /** Original location of a trashed file/folder (from local metadata, else from its path). */
  async trashInfo(trashPath: VaultPath): Promise<TrashInfo> {
    const t = requireEntryPath(trashPath);
    if (!isStrictlyWithin(t, TRASH_DIR)) throw new FsError('EINVAL', t, 'Not inside the trash');
    const chain = [t, ...ancestors(t).reverse()].filter((p) => isStrictlyWithin(p, TRASH_DIR));
    let metas: unknown[] = [];
    try {
      metas = await this.store.getMany<unknown>(chain.map((p) => this.trashKey(p)));
    } catch (e) {
      console.warn('[vault] could not read trash metadata', e);
    }
    for (let i = 0; i < chain.length; i++) {
      const meta = metas[i];
      if (isTrashMeta(meta)) return { original: rebase(t, chain[i]!, normalizePath(meta.original)), deletedAt: meta.deletedAt };
    }
    const original = normalizePath(t.slice(TRASH_DIR.length + 1).split('/').map(stripTrashStamp).join('/'));
    if (!original) throw new FsError('EINVAL', t, 'Cannot determine original path');
    return { original };
  }

  /** Move a trashed file or folder back to where it was (unique path if taken). */
  async restore(trashPath: VaultPath, source: ChangeSource = 'user'): Promise<VaultPath> {
    const t = requireEntryPath(trashPath);
    const { original } = await this.trashInfo(t);
    return this.locks.runWith(bothKeys(t, original), async () => {
      const st = await this.fs.stat(t);
      if (!st) throw new FsError('ENOENT', t);
      // A folder of the original path may meanwhile be a file: restore into "Folder 2/" then.
      const target = await this.freePath(await this.avoidFileAncestors(original, 0, (n) => ` ${n + 1}`));
      const key = target.toLowerCase();
      this.reserved.add(key);
      try {
        await this.fs.rename(t, target);
        await this.dropTrashMeta(t);
        this.emit(await this.relocate(t, target, st.kind, source));
        return target;
      } finally {
        this.reserved.delete(key);
      }
    });
  }

  /** Permanently delete everything in the trash. */
  async emptyTrash(): Promise<void> {
    await this.deletePermanently(TRASH_DIR, 'user');
  }

  // -------------------------------------------------------------------------
  // Change notification and external changes
  // -------------------------------------------------------------------------

  /** Subscribe to batched changes. Returns an unsubscribe function. */
  onChange(cb: (changes: VaultChange[]) => void): () => void {
    this.listeners.add(cb);
    return () => {
      this.listeners.delete(cb);
    };
  }

  /**
   * Subscribe to `fs.watch` (if the backend supports it). Idempotent. An event whose path is
   * the vault root (`''`) makes the vault rescan everything (use it when events were lost).
   */
  startWatching(): void {
    if (this.unwatch || typeof this.fs.watch !== 'function') return;
    this.unwatch = this.fs.watch((events) => this.enqueueFsEvents(events));
  }

  stopWatching(): void {
    const unwatch = this.unwatch;
    this.unwatch = null;
    unwatch?.();
  }

  /**
   * Detect external changes by listing the backend and diffing (mtime, size) against the
   * known stats; changed text files are compared by content hash. Emits and returns the changes.
   */
  async rescan(): Promise<VaultChange[]> {
    const listing = await this.fs.list();
    const files = new Map<VaultPath, FileStat>();
    const dirs = new Map<VaultPath, FileStat>();
    for (const raw of listing) {
      const p = normalizePath(raw.path);
      if (p === '' || !isTracked(p)) continue;
      (raw.kind === 'dir' ? dirs : files).set(p, { path: p, kind: raw.kind, size: raw.size, mtime: raw.mtime });
    }
    const changes: VaultChange[] = [];
    // Case-only renames first: on case-insensitive backends stat() of the old name still succeeds.
    const listedLower = new Map<string, VaultPath>();
    for (const p of files.keys()) listedLower.set(p.toLowerCase(), p);
    // Snapshot of the keys: relocating inserts new ones.
    for (const p of Array.from(this.fileStats.keys())) {
      const variant = files.has(p) ? undefined : listedLower.get(p.toLowerCase());
      if (variant === undefined) continue;
      const moved = await this.locks.runWith(bothKeys(p, variant), () => this.relocateCaseVariant(p, variant));
      if (moved) changes.push(...moved);
    }
    const candidates = new Set<VaultPath>();
    for (const [p, st] of files) {
      const known = this.fileStats.get(p);
      if (!known || known.mtime !== st.mtime || known.size !== st.size) candidates.add(p);
    }
    for (const p of this.fileStats.keys()) if (!files.has(p)) candidates.add(p);
    // A known *file* that is now a folder is left to reconcile (it reports the file's deletion).
    for (const st of dirs.values()) if (!this.dirStats.has(st.path) && !this.fileStats.has(st.path)) this.track(st);
    const sameListing: Lister = () => Promise.resolve(listing);
    for (const p of candidates) {
      changes.push(...(await this.locks.runWith(entryKeys(p), () => this.reconcile(p, 'external', sameListing))));
    }
    this.dropVanishedDirs(new Set(dirs.keys()));
    this.emit(changes);
    return changes;
  }

  /** Resolves when all pending operations and watcher events are processed and listeners notified. */
  async idle(): Promise<void> {
    for (let i = 0; i < 100; i++) {
      await Promise.resolve();
      const chain = this.watchChain;
      await chain;
      await this.locks.idle();
      if (chain === this.watchChain && this.locks.size === 0) break;
    }
    this.flush();
  }

  /** Stop watching and wait for pending work. */
  async close(): Promise<void> {
    this.stopWatching();
    await this.idle();
  }

  // -------------------------------------------------------------------------
  // Internals
  // -------------------------------------------------------------------------

  private generationOf(p: VaultPath): string {
    return `${this.epoch}:${this.generations.get(p) ?? 0}`;
  }

  private bump(p: VaultPath): void {
    this.generations.set(p, (this.generations.get(p) ?? 0) + 1);
  }

  /** Record a stat (and implied parent folders) if the path is tracked. */
  private track(raw: FileStat): FileStat | null {
    const p = normalizePath(raw.path);
    if (p === '' || !isTracked(p)) return null;
    // Frozen: the same objects are handed out by files()/stat(), so callers cannot corrupt the cache.
    const st: FileStat = Object.freeze({ path: p, kind: raw.kind, size: raw.kind === 'dir' ? 0 : raw.size, mtime: raw.mtime });
    if (st.kind === 'dir') {
      this.dirStats.set(p, st);
      this.fileStats.delete(p);
    } else {
      this.fileStats.set(p, st);
      this.dirStats.delete(p);
    }
    for (const d of ancestors(p)) {
      if (!this.dirStats.has(d) && isTracked(d)) this.dirStats.set(d, Object.freeze({ path: d, kind: 'dir', size: 0, mtime: 0 }));
    }
    this.sortedFiles = null;
    return st;
  }

  private cacheText(p: VaultPath, text: string): void {
    if (!isTracked(p)) return;
    this.contents.set(p, text);
    this.hashes.set(p, contentHash(text));
  }

  private applyConfig(text: string | null): void {
    if (text === null) {
      this.config = { ...DEFAULT_VAULT_CONFIG };
      this.rawConfig = {};
      return;
    }
    const parsed = parseVaultConfig(text);
    for (const w of parsed.warnings) console.warn(`[vault] ${w}`);
    this.config = parsed.config;
    this.rawConfig = parsed.raw;
  }

  private async loadConfig(): Promise<void> {
    if (!this.fileStats.has(CONFIG_PATH)) {
      this.applyConfig(null);
      return;
    }
    try {
      const text = await this.fs.readText(CONFIG_PATH);
      this.cacheText(CONFIG_PATH, text);
      this.applyConfig(text);
    } catch (e) {
      console.warn('[vault] could not read config', e);
      this.applyConfig(null);
    }
  }

  private async snapshot(p: VaultPath, content: string, source: ChangeSource): Promise<void> {
    if (!this.history || !isTracked(p)) return;
    try {
      await this.history.snapshot(p, content, source);
    } catch (e) {
      console.warn('[vault] history snapshot failed', e);
    }
  }

  /**
   * Write `data` to `p` (caller holds the lock). Returns null when done, or, without writing,
   * the known path `p` resolves to on a case-insensitive backend (see storedCaseVariant).
   */
  private async writeUnlocked(p: VaultPath, data: string | Uint8Array, source: ChangeSource): Promise<VaultPath | null> {
    const isText = typeof data === 'string';
    // Notes written as bytes (sync downloads, imports) still get their old text snapshotted.
    const textual = isText || isTextPath(p);
    const hash = contentHash(data);
    const known = this.fileStats.get(p);
    if (known && this.hashes.get(p) === hash) return null;
    if (known) {
      // The file may have changed behind our back (watcher event still pending, not watching,
      // another tab). Take that change in first, so it is reported and kept in History
      // instead of being overwritten without a trace.
      let disk: FileStat | null | undefined;
      try {
        disk = await this.fs.stat(p);
      } catch (e) {
        console.warn('[vault] stat before write failed', p, e);
      }
      if (disk === null || (disk !== undefined && !sameStat(disk, known))) {
        this.emit(await this.reconcile(p, 'external'));
        if (this.fileStats.has(p) && this.hashes.get(p) === hash) return null;
      }
    }
    let existed = this.fileStats.has(p);
    if (!existed) {
      const st = await this.fs.stat(p);
      if (st?.kind === 'dir') throw new FsError('EISDIR', p);
      if (st) {
        const stored = await this.storedCaseVariant(p, st);
        if (stored !== null) return stored;
        this.track(st);
        existed = true;
      }
    }
    let prev = this.contents.get(p);
    if (textual && existed && prev === undefined) {
      try {
        prev = await this.fs.readText(p);
      } catch (e) {
        if (!isNotFoundError(e)) console.warn('[vault] could not read previous content', e);
      }
      if (prev !== undefined && contentHash(prev) === hash) {
        this.cacheText(p, prev);
        return null;
      }
    }
    if (textual && prev !== undefined) await this.snapshot(p, prev, source);
    this.bump(p);
    this.contents.delete(p);
    // Remember what we are writing before the backend reports it, so the echo is recognised.
    this.hashes.set(p, hash);
    let st: FileStat;
    try {
      st = isText ? await this.fs.writeText(p, data) : await this.fs.writeBinary(p, data);
    } catch (e) {
      this.hashes.delete(p);
      throw e;
    }
    if (!this.track({ ...st, path: p })) {
      this.hashes.delete(p);
      return null;
    }
    if (isText) {
      this.contents.set(p, data);
      if (p === CONFIG_PATH) this.applyConfig(data);
    }
    this.emit([{ type: existed ? 'modify' : 'create', path: p, source }]);
    return null;
  }

  /**
   * The known file that the unknown path `p` reaches on a case-insensitive backend (Android
   * shared storage, Windows, macOS): a tracked file spelled like `p` except for letter case that
   * the backend reports with the same stat. On case-sensitive backends `p` is a distinct file
   * with its own stat. Null when there is no such file.
   */
  private async storedCaseVariant(p: VaultPath, st: FileStat): Promise<VaultPath | null> {
    const lower = p.toLowerCase();
    if (st.kind !== 'file' || !this.lowerPaths.has(lower)) return null;
    for (const v of Array.from(this.fileStats.keys())) {
      if (v === p || v.toLowerCase() !== lower) continue;
      const vst = await this.fs.stat(v).catch(() => null);
      if (vst && sameStat(vst, st)) return v;
    }
    return null;
  }

  /** First candidate path derived from `base` not known, reserved or in `taken` (case-insensitive). */
  private freeCandidate(base: VaultPath, taken: Set<string>): VaultPath {
    return uniqueName(base, (p) => {
      const l = p.toLowerCase();
      return this.lowerPaths.has(l) || this.reserved.has(l) || taken.has(l);
    });
  }

  /**
   * Throws EEXIST if renaming `a` to `b` would replace another entry. Case-only renames
   * ("Notatka.md" -> "notatka.md") are allowed unless `b` is a distinct entry: one the vault
   * lists under exactly that name, or (on case-sensitive backends) a different file on disk.
   * On case-insensitive backends `stat(b)` describes `a` itself.
   */
  private async assertRenameTargetFree(a: VaultPath, b: VaultPath): Promise<void> {
    if (this.fileStats.has(b) || this.dirStats.has(b)) throw new FsError('EEXIST', b);
    const dest = await this.fs.stat(b);
    if (!dest) return;
    if (a.toLowerCase() !== b.toLowerCase()) throw new FsError('EEXIST', b);
    const src = await this.fs.stat(a);
    if (!src || !sameStat(src, dest)) throw new FsError('EEXIST', b);
  }

  /** A path derived from `base` that is free in the cache and on the backend. */
  private async freePath(base: VaultPath): Promise<VaultPath> {
    const taken = new Set<string>();
    for (;;) {
      const candidate = this.freeCandidate(base, taken);
      const st = await this.fs.stat(candidate);
      if (!st) return candidate;
      this.track(st);
      taken.add(candidate.toLowerCase());
    }
  }

  private trashKey(trashPath: VaultPath): string {
    return `vault/${this.id}/trash/${trashPath}`;
  }

  private async dropTrashMeta(trashPath: VaultPath): Promise<void> {
    try {
      const key = this.trashKey(trashPath);
      const nested = await this.store.keys(`${key}/`);
      await this.store.deleteMany([key, ...nested]);
    } catch (e) {
      console.warn('[vault] could not update trash metadata', e);
    }
  }

  private async trashTarget(p: VaultPath, kind: 'file' | 'dir'): Promise<VaultPath> {
    const stamp = ` (${trashStamp(this.now())})`;
    // ".trash/Zakupy" may be a trashed *file* while "Zakupy/x.md" is deleted: use "Zakupy (stamp)/".
    const base = await this.avoidFileAncestors(joinPath(TRASH_DIR, p), 1, (n) => (n === 1 ? stamp : `${stamp} ${n}`));
    if (!(await this.fs.stat(base))) return base;
    const stamped = insertBeforeExtension(base, stamp, kind === 'dir');
    let candidate = stamped;
    for (let i = 2; await this.fs.stat(candidate); i++) candidate = insertBeforeExtension(stamped, ` ${i}`, kind === 'dir');
    return candidate;
  }

  /**
   * Rewrite `path` so that none of its folders is an existing *file* on the backend (moving
   * there would fail with ENOTDIR): the first blocking segment gets `suffix(1)`, `suffix(2)`,
   * ... appended until it names a folder or nothing. The first `keep` segments are left alone.
   */
  private async avoidFileAncestors(path: VaultPath, keep: number, suffix: (n: number) => string): Promise<VaultPath> {
    const segments = path.split('/');
    for (let i = keep; i < segments.length - 1; i++) {
      let st = await this.fs.stat(segments.slice(0, i + 1).join('/'));
      const name = segments[i]!;
      for (let n = 1; st?.kind === 'file'; n++) {
        segments[i] = name + suffix(n);
        st = await this.fs.stat(segments.slice(0, i + 1).join('/'));
      }
      // Nothing exists below a missing folder.
      if (!st) break;
    }
    return segments.join('/');
  }

  /** Drop `p` (and everything below it) from the caches; returns 'delete' changes for files. */
  private forget(p: VaultPath, source: ChangeSource): VaultChange[] {
    return [...this.dropFile(p, source), ...this.dropFolder(p, source)];
  }

  /** Drop the file `p` from the caches; returns its 'delete' change (none if unknown). O(1). */
  private dropFile(p: VaultPath, source: ChangeSource): VaultChange[] {
    if (!this.fileStats.has(p)) return [];
    this.fileStats.delete(p);
    this.contents.delete(p);
    this.hashes.delete(p);
    this.bump(p);
    this.sortedFiles = null;
    if (p === CONFIG_PATH) this.applyConfig(null);
    return [{ type: 'delete', path: p, source }];
  }

  /**
   * Drop the folder `p` and everything below it; returns 'delete' changes for the files. Every
   * tracked ancestor of a known file is a known folder (see track), so below a tracked path
   * that is not a known folder nothing is cached: deleting a file does not scan the vault.
   */
  private dropFolder(p: VaultPath, source: ChangeSource): VaultChange[] {
    if (!this.dirStats.has(p) && isTracked(p)) return [];
    const changes: VaultChange[] = [];
    for (const path of Array.from(this.fileStats.keys())) if (isStrictlyWithin(path, p)) changes.push(...this.dropFile(path, source));
    for (const d of this.dirStats.keys()) if (isWithin(d, p)) this.dirStats.delete(d);
    return changes;
  }

  /**
   * Update caches after the backend moved `a` to `b`. Returns per-file changes: 'rename'
   * between tracked paths, 'delete' when moved out of view (e.g. into an ignored folder),
   * 'create' for files that appear (e.g. restored from the trash).
   */
  private async relocate(a: VaultPath, b: VaultPath, kind: 'file' | 'dir', source: ChangeSource): Promise<VaultChange[]> {
    const changes: VaultChange[] = [];
    const moves: [VaultPath, VaultPath][] = [];
    if (kind === 'file') moves.push([a, b]);
    else {
      for (const p of this.fileStats.keys()) if (isStrictlyWithin(p, a)) moves.push([p, rebase(p, a, b)]);
      const dirs = [...this.dirStats.values()].filter((d) => isWithin(d.path, a));
      for (const d of dirs) this.dirStats.delete(d.path);
      for (const d of dirs) this.track({ ...d, path: rebase(d.path, a, b) });
      if (!this.dirStats.has(b)) this.track({ path: b, kind: 'dir', size: 0, mtime: 0 });
    }
    let configTouched = false;
    for (const [o, n] of moves) {
      const st = this.fileStats.get(o);
      const content = this.contents.get(o);
      const hash = this.hashes.get(o);
      this.fileStats.delete(o);
      this.contents.delete(o);
      this.hashes.delete(o);
      this.bump(o);
      this.bump(n);
      if (o === CONFIG_PATH || n === CONFIG_PATH) configTouched = true;
      const visible = st ? this.track({ ...st, path: n }) : null;
      if (visible) {
        if (content !== undefined) this.contents.set(n, content);
        if (hash !== undefined) this.hashes.set(n, hash);
      }
      if (this.history) {
        try {
          await this.history.rename(o, n);
        } catch (e) {
          console.warn('[vault] could not move history', e);
        }
      }
      if (st && visible) changes.push({ type: 'rename', path: n, oldPath: o, source });
      else if (st) changes.push({ type: 'delete', path: o, source });
    }
    this.sortedFiles = null;
    // Files we did not know at the source (restores from the trash, stale caches) or that
    // changed while moving (copy-based moves): reconcile the destination with the backend.
    if (isTracked(b) && (kind === 'dir' || !this.fileStats.has(b))) changes.push(...(await this.scanInto(b, source)));
    if (configTouched) await this.loadConfig();
    return changes;
  }

  /**
   * Reconcile everything at/below `target` with the backend (caller holds the lock). `list`
   * provides the backend listing (shared by a batch of watcher events).
   */
  private async scanInto(target: VaultPath, source: ChangeSource, list: Lister = () => this.fs.list()): Promise<VaultChange[]> {
    const st = await this.fs.stat(target);
    if (!st) return [];
    if (st.kind === 'file') return this.reconcile(target, source, list);
    this.track(st);
    const changes: VaultChange[] = [];
    const listing = await list();
    for (const raw of listing) {
      const p = normalizePath(raw.path);
      if (!isStrictlyWithin(p, target) || !isTracked(p)) continue;
      if (raw.kind === 'dir') {
        this.track(raw);
        continue;
      }
      const known = this.fileStats.get(p);
      if (!known || known.mtime !== raw.mtime || known.size !== raw.size) changes.push(...(await this.reconcile(p, source, list)));
    }
    return changes;
  }

  /**
   * Bring the caches for `p` in line with the backend (caller holds the lock). Content
   * equal to what we know (e.g. the echo of our own write) produces no change.
   */
  private async reconcile(p: VaultPath, source: ChangeSource, list?: Lister): Promise<VaultChange[]> {
    if (!isTracked(p)) return [];
    let st: FileStat | null;
    try {
      st = await this.fs.stat(p);
    } catch (e) {
      console.warn('[vault] stat failed', p, e);
      return [];
    }
    const known = this.fileStats.get(p);
    if (!st) {
      if (known) {
        const prev = this.contents.get(p);
        if (prev !== undefined) await this.snapshot(p, prev, source);
        return this.forget(p, source);
      }
      return this.dirStats.has(p) ? this.forget(p, source) : [];
    }
    if (st.kind === 'dir') {
      const wasKnownDir = this.dirStats.has(p);
      // A file replaced by a folder of the same name: the file is gone (keep its content).
      const prev = known ? this.contents.get(p) : undefined;
      if (prev !== undefined) await this.snapshot(p, prev, source);
      // Only the file entry: files inside the new folder may already be tracked.
      const changes = known ? this.dropFile(p, source) : [];
      this.track(st);
      if (!wasKnownDir) changes.push(...(await this.scanInto(p, source, list)));
      return changes;
    }
    // A folder replaced by a file of the same name: everything that was inside is gone.
    const gone = this.dirStats.has(p) ? this.dropFolder(p, source) : [];
    return [...gone, ...(await this.reconcileFile(p, st, known, source))];
  }

  /** reconcile() for a path that is a file on the backend (`st`); `known` is its cached stat. */
  private async reconcileFile(p: VaultPath, st: FileStat, known: FileStat | undefined, source: ChangeSource): Promise<VaultChange[]> {
    if (!isTextPath(p)) {
      if (known && known.mtime === st.mtime && known.size === st.size) return [];
      this.bump(p);
      this.hashes.delete(p);
      this.contents.delete(p);
      this.track(st);
      return [{ type: known ? 'modify' : 'create', path: p, source }];
    }
    let text: string;
    try {
      text = await this.fs.readText(p);
    } catch (e) {
      if (isNotFoundError(e)) return known ? this.forget(p, source) : [];
      console.warn('[vault] read failed', p, e);
      return [];
    }
    const hash = contentHash(text);
    const knownHash = this.hashes.get(p);
    // Unchanged: same hash as we know (e.g. the echo of our own write) or, for content never
    // read, the same (mtime, size) as before (e.g. a rename echo) — like binary files.
    if (known && (knownHash === hash || (knownHash === undefined && sameStat(known, st)))) {
      this.track(st);
      if (!this.contents.has(p)) this.cacheText(p, text);
      return [];
    }
    const prev = this.contents.get(p);
    if (known && prev !== undefined) await this.snapshot(p, prev, source);
    this.bump(p);
    this.track(st);
    this.contents.set(p, text);
    this.hashes.set(p, hash);
    if (p === CONFIG_PATH) this.applyConfig(text);
    return [{ type: known ? 'modify' : 'create', path: p, source }];
  }

  /** External rename: move caches (keeping content/history) if the content is unchanged. */
  private async detectRename(o: VaultPath, n: VaultPath): Promise<VaultChange[] | null> {
    if (!isTracked(o) || !isTracked(n)) return null;
    const knownFile = this.fileStats.get(o);
    const knownDir = this.dirStats.get(o);
    if ((!knownFile && !knownDir) || this.exists(n)) return null;
    const [stO, stN] = await Promise.all([this.fs.stat(o), this.fs.stat(n)]);
    if (stO || !stN) return null;
    if (knownFile && stN.kind === 'file') {
      const hash = this.hashes.get(o);
      if (hash !== undefined && isTextPath(n)) {
        const text = await this.fs.readText(n).catch(() => undefined);
        if (text === undefined || contentHash(text) !== hash) return null;
      } else if (knownFile.size !== stN.size) return null;
      const changes = await this.relocate(o, n, 'file', 'external');
      this.track(stN);
      return changes;
    }
    if (knownDir && stN.kind === 'dir') return this.relocate(o, n, 'dir', 'external');
    return null;
  }

  /**
   * Case-only renames ("Notatka.md" -> "notatka.md") reported by the watcher as rename(o -> n)
   * or delete(o) + create(n). On case-insensitive backends (Android shared storage, Windows,
   * macOS) stat(o) still succeeds because it finds `n`, so the real names are taken from a
   * listing. Returns null when the batch has no candidate pairs (no listing needed).
   */
  private async caseOnlyRenames(events: FsEvent[], list: Lister): Promise<{ pairs: [VaultPath, VaultPath][]; listedDirs: Set<VaultPath> } | null> {
    const gone = new Set<VaultPath>();
    const addedByLower = new Map<string, VaultPath>();
    const added = (raw: string): void => {
      const n = normalizePath(raw);
      if (n && isTracked(n) && !this.exists(n)) addedByLower.set(n.toLowerCase(), n);
    };
    for (const ev of events) {
      if (ev.type === 'rename' && ev.oldPath) {
        gone.add(normalizePath(ev.oldPath));
        added(ev.path);
      } else if (ev.type === 'delete') gone.add(normalizePath(ev.path));
      else added(ev.path);
    }
    const candidates: [VaultPath, VaultPath][] = [];
    for (const o of gone) {
      const n = addedByLower.get(o.toLowerCase());
      if (n !== undefined && n !== o && this.exists(o)) candidates.push([o, n]);
    }
    if (candidates.length === 0) return null;
    let listing: FileStat[];
    try {
      listing = await list();
    } catch (e) {
      console.warn('[vault] listing failed', e);
      return null;
    }
    const listed = new Set<VaultPath>();
    const listedDirs = new Set<VaultPath>();
    for (const st of listing) {
      const p = normalizePath(st.path);
      listed.add(p);
      if (st.kind === 'dir') listedDirs.add(p);
    }
    return { pairs: candidates.filter(([o, n]) => listed.has(n) && !listed.has(o)), listedDirs };
  }

  /**
   * Move the caches of `o` to its case variant `n` if the backend resolves `o` to the same
   * entry (case-insensitive). Returns null when that is not the case, e.g. on case-sensitive
   * backends where `o` is really gone (the caller's reconcile then reports delete + create).
   * Caller holds the locks of both paths.
   */
  private async relocateCaseVariant(o: VaultPath, n: VaultPath): Promise<VaultChange[] | null> {
    const kind = this.fileStats.has(o) ? 'file' : this.dirStats.has(o) ? 'dir' : null;
    if (kind === null || this.exists(n)) return null;
    let st: FileStat | null;
    try {
      st = await this.fs.stat(o);
    } catch {
      return null;
    }
    if (!st || (st.kind === 'dir') !== (kind === 'dir')) return null;
    return this.relocate(o, n, kind, 'external');
  }

  /** Forget folders that are neither listed by the backend nor contain a known file. */
  private dropVanishedDirs(listedDirs: ReadonlySet<VaultPath>): void {
    const occupied = new Set<VaultPath>();
    for (const f of this.fileStats.keys()) for (const d of ancestors(f)) occupied.add(d);
    // Deleting the current entry while iterating a Map is safe.
    for (const d of this.dirStats.keys()) if (!listedDirs.has(d) && !occupied.has(d)) this.dirStats.delete(d);
  }

  private enqueueFsEvents(events: FsEvent[]): void {
    const batch = events.slice();
    this.watchChain = this.watchChain
      .then(() => this.processFsEvents(batch))
      .catch((e: unknown) => console.warn('[vault] processing file events failed', e));
  }

  private async processFsEvents(raw: FsEvent[]): Promise<void> {
    // Events come from native watchers and other tabs: drop malformed ones instead of failing the batch.
    const events = raw.filter(isWellFormedEvent);
    // An event for the vault root means "something changed, details lost" (e.g. a native
    // watcher overflow, Tauri's `rescan: true`): diff the whole vault instead.
    if (events.some((ev) => ev.type !== 'rename' && normalizePath(ev.path) === '')) {
      await this.rescan();
      return;
    }
    const changes: VaultChange[] = [];
    const handled = new Set<VaultPath>();
    // At most one listing per batch: a sync that adds hundreds of folders must not list the
    // whole vault once per folder event.
    const list = listOnce(() => this.fs.list());
    const caseRenames = await this.caseOnlyRenames(events, list);
    if (caseRenames) {
      for (const [o, n] of caseRenames.pairs) {
        const moved = await this.locks.runWith(bothKeys(o, n), () => this.relocateCaseVariant(o, n));
        if (!moved) continue;
        changes.push(...moved);
        // The new name is still reconciled below (its content may have changed as well).
        handled.add(o);
      }
      this.dropVanishedDirs(caseRenames.listedDirs);
    }
    for (const ev of events) {
      if (ev.type !== 'rename' || !ev.oldPath) continue;
      const o = normalizePath(ev.oldPath);
      const n = normalizePath(ev.path);
      if (!o || !n || o === n || handled.has(o)) continue;
      const moved = await this.locks.runWith(bothKeys(o, n), () => this.detectRename(o, n));
      if (moved) {
        changes.push(...moved);
        handled.add(o);
        handled.add(n);
      }
    }
    for (const ev of events) {
      const raws = ev.type === 'rename' && ev.oldPath ? [ev.oldPath, ev.path] : [ev.path];
      for (const raw of raws) {
        const p = normalizePath(raw);
        if (!p || handled.has(p) || !isTracked(p)) continue;
        handled.add(p);
        changes.push(...(await this.locks.runWith(entryKeys(p), () => this.reconcile(p, 'external', list))));
      }
    }
    await this.dropRemovedFolders(changes);
    this.emit(changes);
  }

  /**
   * After files vanished (deleted or moved away by another program), forget the folders that
   * held them if those folders are gone from the backend too. Polling watchers report only
   * files, so a folder deleted with everything inside would otherwise linger in folders().
   */
  private async dropRemovedFolders(changes: readonly VaultChange[]): Promise<void> {
    const candidates = new Set<VaultPath>();
    for (const c of changes) {
      const gone = c.type === 'delete' ? c.path : c.type === 'rename' ? c.oldPath : undefined;
      if (gone !== undefined) for (const d of ancestors(gone)) if (this.dirStats.has(d)) candidates.add(d);
    }
    if (candidates.size === 0) return;
    const occupied = new Set<VaultPath>();
    for (const f of this.fileStats.keys()) for (const d of ancestors(f)) occupied.add(d);
    // Deepest first; a folder's subfolders are checked before the folder itself.
    for (const d of [...candidates].sort((a, b) => b.length - a.length)) {
      if (occupied.has(d)) continue;
      // Under the folder's lock: a concurrent write into it either ran before (the folder
      // exists again) or runs after and tracks it again.
      await this.locks.runWith(entryKeys(d), async () => {
        if (!this.dirStats.has(d)) return;
        let st: FileStat | null;
        try {
          st = await this.fs.stat(d);
        } catch {
          return;
        }
        if (st) return;
        for (const x of this.dirStats.keys()) if (isWithin(x, d) && !occupied.has(x)) this.dirStats.delete(x);
      });
    }
  }

  private emit(changes: VaultChange[]): void {
    if (changes.length === 0) return;
    this.pending.push(...changes);
    if (this.flushScheduled) return;
    this.flushScheduled = true;
    queueMicrotask(() => this.flush());
  }

  private flush(): void {
    this.flushScheduled = false;
    if (this.pending.length === 0) return;
    const batch = this.pending;
    this.pending = [];
    for (const cb of Array.from(this.listeners)) {
      try {
        cb(batch.slice());
      } catch (e) {
        console.warn('[vault] change listener failed', e);
      }
    }
  }
}
