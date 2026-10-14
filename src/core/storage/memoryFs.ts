/**
 * In-memory FileSystemAdapter (tests, demo vaults, previews). Directories are explicit
 * entries created on demand for every parent of a written file. An "external" API
 * (`externalWrite`, `externalRemove`, `externalRename`, `externalMkdir`) simulates other
 * programs (Syncthing, another editor) changing files; those changes are reported to
 * `watch()` subscribers in batches (one microtask), like a debounced native watcher.
 */
import { normalizePath } from '../paths';
import type { FileStat, FileSystemAdapter, FsEvent, VaultPath } from '../types';
import { FsError, requireEntryPath } from './errors';
import { decodeUtf8, encodeUtf8, utf8Length } from './hash';
import { ancestors, isStrictlyWithin, isWithin, rebase } from './pathUtil';

type MemEntry = { kind: 'file'; data: string | Uint8Array; mtime: number } | { kind: 'dir'; mtime: number };

export interface MemoryFsOptions {
  /** Human readable label (default "memory"). */
  label?: string;
  /** Clock for modification times (ms since epoch). Default `Date.now`. */
  now?: () => number;
  /** Initial files, path -> content. Parent directories are created implicitly. */
  files?: Record<string, string | Uint8Array>;
  /**
   * Also report this adapter's own writes to watchers (like native watchers do). Useful to
   * test that consumers suppress the echo of their own writes. Default false.
   */
  reportOwnWrites?: boolean;
}

export interface ExternalChangeOptions {
  /** Change the content without notifying watchers (simulates changes while not watching). */
  silent?: boolean;
}

function sizeOf(data: string | Uint8Array): number {
  return typeof data === 'string' ? utf8Length(data) : data.byteLength;
}

/** Map-based FileSystemAdapter implementation. */
export class MemoryFs implements FileSystemAdapter {
  readonly kind = 'memory' as const;
  readonly label: string;
  private readonly entries = new Map<VaultPath, MemEntry>();
  private readonly listeners = new Set<(events: FsEvent[]) => void>();
  private readonly now: () => number;
  private readonly reportOwnWrites: boolean;
  private queued: FsEvent[] = [];
  private flushScheduled = false;

  constructor(opts: MemoryFsOptions = {}) {
    this.label = opts.label ?? 'memory';
    this.now = opts.now ?? Date.now;
    this.reportOwnWrites = opts.reportOwnWrites ?? false;
    for (const [path, content] of Object.entries(opts.files ?? {})) this.writeSync(path, content);
  }

  // -------------------------------------------------------------------------
  // FileSystemAdapter
  // -------------------------------------------------------------------------

  async list(): Promise<FileStat[]> {
    return [...this.entries.keys()].sort().map((p) => this.statOf(p, this.entries.get(p)!));
  }

  async stat(path: VaultPath): Promise<FileStat | null> {
    const p = normalizePath(path);
    if (p === '') return { path: '', kind: 'dir', size: 0, mtime: 0 };
    const e = this.entries.get(p);
    return e ? this.statOf(p, e) : null;
  }

  async readText(path: VaultPath): Promise<string> {
    const data = this.readSync(path);
    return typeof data === 'string' ? data : decodeUtf8(data);
  }

  async readBinary(path: VaultPath): Promise<Uint8Array> {
    const data = this.readSync(path);
    return typeof data === 'string' ? encodeUtf8(data) : data.slice();
  }

  async writeText(path: VaultPath, content: string): Promise<FileStat> {
    const st = this.writeSync(path, content);
    this.own({ type: st.created ? 'create' : 'modify', path: st.stat.path });
    return st.stat;
  }

  async writeBinary(path: VaultPath, data: Uint8Array): Promise<FileStat> {
    const st = this.writeSync(path, data.slice());
    this.own({ type: st.created ? 'create' : 'modify', path: st.stat.path });
    return st.stat;
  }

  async remove(path: VaultPath): Promise<void> {
    for (const ev of this.removeSync(path)) this.own(ev);
  }

  async rename(from: VaultPath, to: VaultPath): Promise<void> {
    const ev = this.renameSync(from, to);
    if (ev) this.own(ev);
  }

  async mkdir(path: VaultPath): Promise<void> {
    const created = this.mkdirSync(path);
    if (created) this.own({ type: 'create', path: normalizePath(path) });
  }

  watch(onEvents: (events: FsEvent[]) => void): () => void {
    this.listeners.add(onEvents);
    return () => {
      this.listeners.delete(onEvents);
    };
  }

  // -------------------------------------------------------------------------
  // "External" changes (other programs) — reported to watchers
  // -------------------------------------------------------------------------

  /** Write a file as if another program did it. */
  externalWrite(path: VaultPath, content: string | Uint8Array, opts: ExternalChangeOptions = {}): FileStat {
    const { stat, created } = this.writeSync(path, typeof content === 'string' ? content : content.slice());
    if (!opts.silent) this.queue([{ type: created ? 'create' : 'modify', path: stat.path }]);
    return stat;
  }

  /** Remove a file or directory as if another program did it. */
  externalRemove(path: VaultPath, opts: ExternalChangeOptions = {}): void {
    const events = this.removeSync(path);
    if (!opts.silent) this.queue(events);
  }

  /** Rename/move as if another program did it. */
  externalRename(from: VaultPath, to: VaultPath, opts: ExternalChangeOptions = {}): void {
    const ev = this.renameSync(from, to);
    if (ev && !opts.silent) this.queue([ev]);
  }

  /** Create a directory as if another program did it. */
  externalMkdir(path: VaultPath, opts: ExternalChangeOptions = {}): void {
    if (this.mkdirSync(path) && !opts.silent) this.queue([{ type: 'create', path: normalizePath(path) }]);
  }

  /**
   * Deliver arbitrary watcher events without changing any file (simulates coarse or
   * delayed native watcher events, e.g. a single event for a whole moved folder).
   */
  externalEvents(events: FsEvent[]): void {
    this.queue(events.map((e) => ({ ...e })));
  }

  /** Snapshot of all files (path -> content) for assertions. */
  dump(): Record<VaultPath, string | Uint8Array> {
    const out: Record<VaultPath, string | Uint8Array> = {};
    for (const p of [...this.entries.keys()].sort()) {
      const e = this.entries.get(p)!;
      if (e.kind === 'file') out[p] = e.data;
    }
    return out;
  }

  /** Resolves after queued watcher events have been delivered. */
  async flushEvents(): Promise<void> {
    await Promise.resolve();
    this.deliver();
  }

  // -------------------------------------------------------------------------
  // Internals (synchronous; the async API wraps them)
  // -------------------------------------------------------------------------

  private statOf(path: VaultPath, e: MemEntry): FileStat {
    return e.kind === 'file'
      ? { path, kind: 'file', size: sizeOf(e.data), mtime: e.mtime }
      : { path, kind: 'dir', size: 0, mtime: e.mtime };
  }

  private readSync(path: VaultPath): string | Uint8Array {
    const p = requireEntryPath(path);
    const e = this.entries.get(p);
    if (!e) throw new FsError('ENOENT', p);
    if (e.kind === 'dir') throw new FsError('EISDIR', p);
    return e.data;
  }

  /** Create missing parent directories; throws ENOTDIR if a parent is a file. */
  private ensureParents(p: VaultPath, mtime: number): void {
    for (const dir of ancestors(p)) {
      const e = this.entries.get(dir);
      if (!e) this.entries.set(dir, { kind: 'dir', mtime });
      else if (e.kind === 'file') throw new FsError('ENOTDIR', dir);
    }
  }

  private writeSync(path: VaultPath, data: string | Uint8Array): { stat: FileStat; created: boolean } {
    const p = requireEntryPath(path);
    const existing = this.entries.get(p);
    if (existing?.kind === 'dir') throw new FsError('EISDIR', p);
    const mtime = this.now();
    this.ensureParents(p, mtime);
    const entry: MemEntry = { kind: 'file', data, mtime };
    this.entries.set(p, entry);
    return { stat: this.statOf(p, entry), created: !existing };
  }

  private mkdirSync(path: VaultPath): boolean {
    const p = normalizePath(path);
    if (p === '') return false;
    const existing = this.entries.get(p);
    if (existing?.kind === 'file') throw new FsError('EEXIST', p);
    if (existing) return false;
    const mtime = this.now();
    this.ensureParents(p, mtime);
    this.entries.set(p, { kind: 'dir', mtime });
    return true;
  }

  private removeSync(path: VaultPath): FsEvent[] {
    const p = requireEntryPath(path);
    const events: FsEvent[] = [];
    for (const key of this.entries.keys()) {
      if (!isWithin(key, p)) continue;
      this.entries.delete(key);
      events.push({ type: 'delete', path: key });
    }
    return events;
  }

  private renameSync(from: VaultPath, to: VaultPath): FsEvent | null {
    const a = requireEntryPath(from);
    const b = requireEntryPath(to);
    const src = this.entries.get(a);
    if (!src) throw new FsError('ENOENT', a);
    if (a === b) return null;
    if (isStrictlyWithin(b, a)) throw new FsError('EINVAL', b, 'Cannot move a directory into itself');
    const dest = this.entries.get(b);
    if (dest) {
      if (dest.kind === 'dir' || src.kind === 'dir') {
        const destEmptyDir = dest.kind === 'dir' && ![...this.entries.keys()].some((k) => isStrictlyWithin(k, b));
        if (!(src.kind === 'dir' && destEmptyDir)) throw new FsError('EEXIST', b);
      }
      this.entries.delete(b);
    }
    this.ensureParents(b, this.now());
    const moved: [VaultPath, MemEntry][] = [];
    for (const [key, e] of this.entries) if (isWithin(key, a)) moved.push([key, e]);
    for (const [key] of moved) this.entries.delete(key);
    for (const [key, e] of moved) this.entries.set(rebase(key, a, b), e);
    return { type: 'rename', path: b, oldPath: a };
  }

  private own(ev: FsEvent): void {
    if (this.reportOwnWrites) this.queue([ev]);
  }

  private queue(events: FsEvent[]): void {
    if (events.length === 0 || this.listeners.size === 0) return;
    this.queued.push(...events);
    if (this.flushScheduled) return;
    this.flushScheduled = true;
    queueMicrotask(() => this.deliver());
  }

  private deliver(): void {
    this.flushScheduled = false;
    if (this.queued.length === 0) return;
    const batch = this.queued;
    this.queued = [];
    for (const cb of Array.from(this.listeners)) {
      try {
        cb(batch.map((e) => ({ ...e })));
      } catch (e) {
        console.warn('[storage] MemoryFs watcher callback failed', e);
      }
    }
  }
}
