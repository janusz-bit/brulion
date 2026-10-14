/**
 * FileSystemAdapter over the File System Access API (Chromium: a real folder picked by the
 * user, or the origin private file system). Browser-only at runtime, but nothing here touches
 * globals at module load, and tests drive it with an in-memory fake handle.
 *
 * - Writes use `createWritable()`, which writes to a swap file and replaces the target
 *   atomically on `close()` — sync tools never see half-written notes.
 * - `rename` uses `handle.move()` when available, otherwise copy + delete.
 * - `watch()` polls (default every 3 s) and diffs (mtime, size) snapshots.
 */
import { basename, dirname, joinPath, normalizePath } from '../paths';
import type { FileStat, FileSystemAdapter, FsEvent, VaultPath } from '../types';
import { FsError, requireEntryPath } from './errors';
import { isStrictlyWithin } from './pathUtil';

// ---------------------------------------------------------------------------
// Minimal structural types for the parts of the API we use. TypeScript's DOM lib does not
// declare async iteration of directory handles (DOM.AsyncIterable is not in our lib set)
// nor `move()` / permissions, so we describe the subset ourselves.
// ---------------------------------------------------------------------------

export interface FsaFile {
  readonly size: number;
  readonly lastModified: number;
  text(): Promise<string>;
  arrayBuffer(): Promise<ArrayBuffer>;
}

export interface FsaWritable {
  write(data: string | Uint8Array): Promise<void>;
  close(): Promise<void>;
  abort?(reason?: unknown): Promise<void>;
}

export type FsaPermissionState = 'granted' | 'denied' | 'prompt';

export interface FsaPermissionDescriptor {
  mode?: 'read' | 'readwrite';
}

interface FsaHandleBase {
  readonly name: string;
  /** Not universally available; `move(parent, newName)`. */
  move?(destination: FsaDirectoryHandle, newName: string): Promise<void>;
  queryPermission?(desc?: FsaPermissionDescriptor): Promise<FsaPermissionState>;
  requestPermission?(desc?: FsaPermissionDescriptor): Promise<FsaPermissionState>;
}

export interface FsaFileHandle extends FsaHandleBase {
  readonly kind: 'file';
  getFile(): Promise<FsaFile>;
  createWritable(options?: { keepExistingData?: boolean }): Promise<FsaWritable>;
}

export interface FsaDirectoryHandle extends FsaHandleBase {
  readonly kind: 'directory';
  getDirectoryHandle(name: string, options?: { create?: boolean }): Promise<FsaDirectoryHandle>;
  getFileHandle(name: string, options?: { create?: boolean }): Promise<FsaFileHandle>;
  removeEntry(name: string, options?: { recursive?: boolean }): Promise<void>;
  entries(): AsyncIterableIterator<[string, FsaFileHandle | FsaDirectoryHandle]>;
}

export type FsaHandle = FsaFileHandle | FsaDirectoryHandle;

/** Prefix of temporary entries (ignored by the vault, see isTempFile in paths.ts). */
const TEMP_PREFIX = '.brulion-tmp-';

/** Directories never listed (huge and irrelevant), same as the native Tauri lister. */
const SKIPPED_DIRS = new Set(['.git', 'node_modules']);

export interface FsAccessFsOptions {
  /** Label shown in the UI (default: the folder name). */
  label?: string;
  /** Polling interval for `watch()` in ms (default 3000). */
  pollInterval?: number;
}

function errorName(e: unknown): string {
  return typeof e === 'object' && e !== null && typeof (e as { name?: unknown }).name === 'string' ? (e as { name: string }).name : '';
}

/** Translate DOMExceptions of the File System Access API into FsError. */
function mapError(e: unknown, path: VaultPath, expect: 'file' | 'dir' | 'any' = 'any'): unknown {
  switch (errorName(e)) {
    case 'NotFoundError':
      return new FsError('ENOENT', path);
    case 'TypeMismatchError':
      return new FsError(expect === 'file' ? 'EISDIR' : 'ENOTDIR', path);
    case 'InvalidModificationError':
      return new FsError('ENOTEMPTY', path);
    default:
      return e;
  }
}

/** Signature used to detect changes between polls. */
function signature(st: FileStat): string {
  return `${st.mtime}:${st.size}`;
}

/** Diff two (path -> signature) snapshots into watcher events. */
export function diffSnapshots(prev: ReadonlyMap<VaultPath, string>, next: ReadonlyMap<VaultPath, string>): FsEvent[] {
  const events: FsEvent[] = [];
  for (const [path, sig] of next) {
    const old = prev.get(path);
    if (old === undefined) events.push({ type: 'create', path });
    else if (old !== sig) events.push({ type: 'modify', path });
  }
  for (const path of prev.keys()) if (!next.has(path)) events.push({ type: 'delete', path });
  return events;
}

interface Poller {
  onEvents: (events: FsEvent[]) => void;
  snapshot: Map<VaultPath, string> | null;
  timer: ReturnType<typeof setInterval> | null;
  running: Promise<void> | null;
  stopped: boolean;
  warned: boolean;
}

/** FileSystemAdapter for a folder opened through the File System Access API. */
export class FsAccessFs implements FileSystemAdapter {
  readonly kind = 'fsaccess' as const;
  readonly label: string;
  readonly root: FsaDirectoryHandle;
  private readonly pollInterval: number;
  private readonly pollers = new Set<Poller>();

  constructor(dirHandle: FileSystemDirectoryHandle | FsaDirectoryHandle, opts: FsAccessFsOptions = {}) {
    // The real handle implements FsaDirectoryHandle at runtime; TS's DOM lib just lacks entries()/move().
    this.root = dirHandle as unknown as FsaDirectoryHandle;
    this.label = opts.label ?? dirHandle.name;
    this.pollInterval = opts.pollInterval ?? 3000;
  }

  // -------------------------------------------------------------------------
  // FileSystemAdapter
  // -------------------------------------------------------------------------

  async list(): Promise<FileStat[]> {
    const out: FileStat[] = [];
    await this.walk(this.root, '', out);
    return out.sort((a, b) => (a.path < b.path ? -1 : a.path > b.path ? 1 : 0));
  }

  async stat(path: VaultPath): Promise<FileStat | null> {
    const p = normalizePath(path);
    if (p === '') return { path: '', kind: 'dir', size: 0, mtime: 0 };
    const handle = await this.entry(p);
    if (!handle) return null;
    if (handle.kind === 'directory') return { path: p, kind: 'dir', size: 0, mtime: 0 };
    try {
      const f = await handle.getFile();
      return { path: p, kind: 'file', size: f.size, mtime: f.lastModified };
    } catch (e) {
      if (errorName(e) === 'NotFoundError') return null;
      throw e;
    }
  }

  async readText(path: VaultPath): Promise<string> {
    const p = requireEntryPath(path);
    const f = await this.fileOf(p);
    return f.text();
  }

  async readBinary(path: VaultPath): Promise<Uint8Array> {
    const p = requireEntryPath(path);
    const f = await this.fileOf(p);
    return new Uint8Array(await f.arrayBuffer());
  }

  async writeText(path: VaultPath, content: string): Promise<FileStat> {
    return this.writeData(requireEntryPath(path), content);
  }

  async writeBinary(path: VaultPath, data: Uint8Array): Promise<FileStat> {
    return this.writeData(requireEntryPath(path), data);
  }

  async remove(path: VaultPath): Promise<void> {
    const p = requireEntryPath(path);
    let parent: FsaDirectoryHandle;
    try {
      parent = await this.dirAt(dirname(p), false);
    } catch (e) {
      if (errorName(e) === 'NotFoundError' || errorName(e) === 'TypeMismatchError') return;
      throw mapError(e, p);
    }
    try {
      await parent.removeEntry(basename(p), { recursive: true });
    } catch (e) {
      if (errorName(e) === 'NotFoundError') return;
      throw mapError(e, p);
    }
    this.forgetOwn(p);
  }

  async rename(from: VaultPath, to: VaultPath): Promise<void> {
    const a = requireEntryPath(from);
    const b = requireEntryPath(to);
    const src = await this.entry(a);
    if (!src) throw new FsError('ENOENT', a);
    if (a === b) return;
    // Compared case-insensitively: on case-insensitive folders "dir/sub" lies inside "Dir", and
    // copying a folder into itself would never end.
    if (isStrictlyWithin(b.toLowerCase(), a.toLowerCase())) throw new FsError('EINVAL', b, 'Cannot move a directory into itself');
    if (a.toLowerCase() === b.toLowerCase()) return this.renameCaseOnly(src, a, b);
    await this.moveEntry(src, a, b);
  }

  async mkdir(path: VaultPath): Promise<void> {
    const p = normalizePath(path);
    if (p === '') return;
    try {
      await this.dirAt(p, true);
    } catch (e) {
      if (errorName(e) === 'TypeMismatchError') throw new FsError('EEXIST', p);
      throw mapError(e, p, 'dir');
    }
  }

  /** Poll the folder and report differences (create / modify / delete) every `pollInterval` ms. */
  watch(onEvents: (events: FsEvent[]) => void): () => void {
    const poller: Poller = { onEvents, snapshot: null, timer: null, running: null, stopped: false, warned: false };
    this.pollers.add(poller);
    void this.poll(poller);
    poller.timer = setInterval(() => void this.poll(poller), this.pollInterval);
    return () => {
      poller.stopped = true;
      if (poller.timer !== null) clearInterval(poller.timer);
      this.pollers.delete(poller);
    };
  }

  /** Poll immediately for all active watchers (e.g. when the window regains focus). */
  async checkNow(): Promise<void> {
    await Promise.all([...this.pollers].map(async (p) => {
      // A poll may be in flight with a listing taken before the caller's change: finish it first.
      if (p.running) await p.running;
      await this.poll(p);
    }));
  }

  // -------------------------------------------------------------------------
  // Internals
  // -------------------------------------------------------------------------

  private poll(poller: Poller): Promise<void> {
    if (poller.stopped) return Promise.resolve();
    if (poller.running) return poller.running;
    poller.running = (async () => {
      try {
        const listing = await this.list();
        const next = new Map<VaultPath, string>();
        for (const st of listing) if (st.kind === 'file') next.set(st.path, signature(st));
        if (poller.snapshot && !poller.stopped) {
          const events = diffSnapshots(poller.snapshot, next);
          if (events.length > 0) {
            try {
              poller.onEvents(events);
            } catch (e) {
              console.warn('[storage] FsAccessFs watcher callback failed', e);
            }
          }
        }
        poller.snapshot = next;
      } catch (e) {
        if (!poller.warned) console.warn('[storage] FsAccessFs polling failed', e);
        poller.warned = true;
      } finally {
        poller.running = null;
      }
    })();
    return poller.running;
  }

  /** Record our own write in watcher snapshots so the next poll does not echo it. */
  private noteOwn(st: FileStat): void {
    for (const p of this.pollers) p.snapshot?.set(st.path, signature(st));
  }

  /** Drop removed/moved entries from watcher snapshots (no delete echo for own changes). */
  private forgetOwn(path: VaultPath): void {
    for (const p of this.pollers) {
      if (!p.snapshot) continue;
      for (const key of p.snapshot.keys()) if (key === path || key.startsWith(`${path}/`)) p.snapshot.delete(key);
    }
  }

  private async walk(dir: FsaDirectoryHandle, prefix: string, out: FileStat[]): Promise<void> {
    const files: [VaultPath, FsaFileHandle][] = [];
    const dirs: [VaultPath, FsaDirectoryHandle][] = [];
    for await (const [name, handle] of dir.entries()) {
      const p = prefix ? `${prefix}/${name}` : name;
      if (handle.kind === 'directory') {
        if (SKIPPED_DIRS.has(name)) continue;
        out.push({ path: p, kind: 'dir', size: 0, mtime: 0 });
        dirs.push([p, handle]);
      } else files.push([p, handle]);
    }
    await Promise.all(
      files.map(async ([p, h]) => {
        try {
          const f = await h.getFile();
          out.push({ path: p, kind: 'file', size: f.size, mtime: f.lastModified });
        } catch (e) {
          // Deleted while listing: skip it.
          if (errorName(e) !== 'NotFoundError') throw e;
        }
      }),
    );
    for (const [p, h] of dirs) {
      try {
        await this.walk(h, p, out);
      } catch (e) {
        if (errorName(e) !== 'NotFoundError') throw e;
      }
    }
  }

  /** Directory handle for `path` ('' = root), optionally creating missing directories. */
  private async dirAt(path: VaultPath, create: boolean): Promise<FsaDirectoryHandle> {
    let dir = this.root;
    if (path === '') return dir;
    for (const seg of path.split('/')) dir = await dir.getDirectoryHandle(seg, { create });
    return dir;
  }

  /** Handle at `path`, or null when it does not exist. */
  private async entry(path: VaultPath): Promise<FsaHandle | null> {
    if (path === '') return this.root;
    let parent: FsaDirectoryHandle;
    try {
      parent = await this.dirAt(dirname(path), false);
    } catch (e) {
      const n = errorName(e);
      if (n === 'NotFoundError' || n === 'TypeMismatchError') return null;
      throw e;
    }
    const name = basename(path);
    try {
      return await parent.getFileHandle(name);
    } catch (e) {
      if (errorName(e) !== 'TypeMismatchError' && errorName(e) !== 'NotFoundError') throw e;
    }
    try {
      return await parent.getDirectoryHandle(name);
    } catch (e) {
      if (errorName(e) === 'NotFoundError' || errorName(e) === 'TypeMismatchError') return null;
      throw e;
    }
  }

  private async parentDir(p: VaultPath, create: boolean): Promise<FsaDirectoryHandle> {
    try {
      return await this.dirAt(dirname(p), create);
    } catch (e) {
      throw mapError(e, dirname(p), 'dir');
    }
  }

  private async fileOf(p: VaultPath): Promise<FsaFile> {
    const parent = await this.parentDir(p, false);
    try {
      const fh = await parent.getFileHandle(basename(p));
      return await fh.getFile();
    } catch (e) {
      throw mapError(e, p, 'file');
    }
  }

  private async writeData(p: VaultPath, data: string | Uint8Array): Promise<FileStat> {
    const parent = await this.parentDir(p, true);
    let fh: FsaFileHandle;
    try {
      fh = await parent.getFileHandle(basename(p), { create: true });
    } catch (e) {
      throw mapError(e, p, 'file');
    }
    const w = await fh.createWritable();
    try {
      await w.write(data);
      await w.close();
    } catch (e) {
      await w.abort?.(e).catch(() => {});
      throw e;
    }
    const f = await fh.getFile();
    const st: FileStat = { path: p, kind: 'file', size: f.size, mtime: f.lastModified };
    this.noteOwn(st);
    return st;
  }

  /**
   * Case-only rename ("Notatka.md" -> "notatka.md"). On case-insensitive folders (Windows,
   * macOS, Android shared storage) `b` resolves to the source itself, so it must never be
   * treated as an existing destination (removing it would delete the source). Moving through a
   * temporary sibling works on case-sensitive and case-insensitive folders alike; on failure the
   * entry is moved back.
   */
  private async renameCaseOnly(src: FsaHandle, a: VaultPath, b: VaultPath): Promise<void> {
    const tmp = joinPath(dirname(a), `${TEMP_PREFIX}${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 10)}`);
    await this.moveEntry(src, a, tmp);
    try {
      const moved = await this.entry(tmp);
      if (!moved) throw new FsError('ENOENT', tmp);
      await this.moveEntry(moved, tmp, b);
    } catch (e) {
      const back = await this.entry(tmp).catch(() => null);
      if (back) await this.moveEntry(back, tmp, a).catch(() => {});
      throw e;
    }
  }

  /** Move `src` (at `a`) to `b`: an existing file at `b` is replaced, anything else refused. */
  private async moveEntry(src: FsaHandle, a: VaultPath, b: VaultPath): Promise<void> {
    const dest = await this.entry(b);
    if (dest) {
      if (src.kind === 'file' && dest.kind === 'file') await this.remove(b);
      else throw new FsError('EEXIST', b);
    }
    const destDir = await this.parentDir(b, true);
    const name = basename(b);
    if (typeof src.move === 'function') {
      try {
        await src.move(destDir, name);
        this.forgetOwn(a);
        return;
      } catch {
        // Moves can be unsupported (e.g. directories outside OPFS). If it actually moved, we're done.
        if (!(await this.entry(a)) && (await this.entry(b))) return;
      }
    }
    await this.copyEntry(src, destDir, name);
    await this.remove(a);
  }

  /** Recursively copy `src` into `destDir` under `name`. */
  private async copyEntry(src: FsaHandle, destDir: FsaDirectoryHandle, name: string): Promise<void> {
    if (src.kind === 'file') {
      const f = await src.getFile();
      const target = await destDir.getFileHandle(name, { create: true });
      const w = await target.createWritable();
      try {
        await w.write(new Uint8Array(await f.arrayBuffer()));
        await w.close();
      } catch (e) {
        await w.abort?.(e).catch(() => {});
        throw e;
      }
      return;
    }
    const target = await destDir.getDirectoryHandle(name, { create: true });
    for await (const [childName, child] of src.entries()) await this.copyEntry(child, target, childName);
  }
}
