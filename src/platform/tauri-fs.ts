/**
 * FileSystemAdapter over Brulion's Rust commands (docs/ARCHITECTURE.md, "Tauri IPC
 * contract"). Every command takes the absolute vault `root` and a relative VaultPath; the
 * Rust side writes atomically (temp file + rename) and rejects paths escaping the root.
 *
 * - Errors arrive as strings `"<CODE>: <message>: <path>"`; codes known to the storage layer
 *   become `FsError`, others (EACCES, ENOSPC, EIO, ...) a {@link TauriFsError}.
 * - Binary writes send the bytes as the raw IPC body with `x-root` / `x-path` headers; on
 *   Android bodies above `base64Threshold` go as `{ base64 }` (raw bodies become JSON
 *   number arrays there).
 * - Vault paths are NFC (Polish letters typed on any device). Files created by other tools
 *   may be stored NFD on disk (e.g. copied from macOS); names seen in `list()` and watcher
 *   events are remembered so later operations reach the real on-disk name.
 * - `watch()` listens to `brulion://fs-events` for this root and runs `watch_start` /
 *   `watch_stop` (reference-counted per root; calls for one root are sent one after the
 *   other, since Rust lets the last one win). `rescan: true` is reported as an event for
 *   the root path `''`, which makes the Vault re-list everything. If the native watcher
 *   cannot start, every subscriber of that start falls back to polling.
 * - `absolutePath()` / `assetUrl()` give the native path / asset-protocol URL of an entry
 *   (images in notes).
 */
import type { InvokeArgs, InvokeOptions } from '@tauri-apps/api/core';
import type { FileStat, FileSystemAdapter, FsEvent, FsEventType, VaultPath } from '@/core/types';
import { normalizePath } from '@/core/paths';
import { diffSnapshots, FsError, type FsErrorCode } from '@/core/storage';
import { bytesToBase64, toBytes } from './bytes';
import { errorMessage } from './errors';
import { loadTauriEvent, tauriInvoke } from './tauri-ipc';

/** Event emitted by the Rust watcher. */
export const FS_EVENTS = 'brulion://fs-events';

/** `FileStatDto` of the IPC contract. */
export interface FileStatDto {
  path: string;
  kind: 'file' | 'dir';
  size: number;
  mtime: number;
}

/** Payload of {@link FS_EVENTS}. */
export interface FsEventsPayload {
  root: string;
  events: { type: FsEventType; path: string; oldPath?: string }[];
  rescan?: boolean;
}

const FS_ERROR_CODES: ReadonlySet<string> = new Set<FsErrorCode>(['ENOENT', 'EEXIST', 'EISDIR', 'ENOTDIR', 'EINVAL', 'ENOTEMPTY']);
const EVENT_TYPES: ReadonlySet<string> = new Set<FsEventType>(['create', 'modify', 'delete', 'rename']);

/** Native error with a POSIX code the storage layer has no FsError code for (EACCES, ENOSPC, ...). */
export class TauriFsError extends Error {
  override name = 'TauriFsError';
  readonly code: string;
  readonly path: string;

  constructor(code: string, path: string, message: string) {
    super(message);
    this.code = code;
    this.path = path;
  }
}

/**
 * Convert an IPC rejection into an FsError (known codes) or a TauriFsError. `path` is the
 * VaultPath of the operation; `diskPath` the path actually sent (stripped from the message).
 */
export function toFsError(e: unknown, path: string, diskPath: string = path): Error {
  if (e instanceof FsError || e instanceof TauriFsError) return e;
  const msg = errorMessage(e);
  const m = /^([A-Z][A-Z0-9]*):\s*([\s\S]*)$/.exec(msg);
  if (!m) return new TauriFsError('EIO', path, msg);
  const code = m[1]!;
  let rest = m[2]!;
  for (const p of [diskPath, path]) {
    if (p && rest.endsWith(`: ${p}`)) {
      rest = rest.slice(0, -(p.length + 2));
      break;
    }
  }
  if (FS_ERROR_CODES.has(code)) return new FsError(code as FsErrorCode, path, rest || undefined);
  return new TauriFsError(code, path, msg);
}

function isRecord(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null;
}

/** Validate a FileStatDto (null when malformed). Paths are returned as sent by Rust. */
export function parseStatDto(v: unknown): FileStatDto | null {
  if (!isRecord(v) || typeof v.path !== 'string') return null;
  const size = Number(v.size);
  const mtime = Number(v.mtime);
  return {
    path: v.path,
    kind: v.kind === 'dir' ? 'dir' : 'file',
    size: Number.isFinite(size) && size >= 0 ? size : 0,
    mtime: Number.isFinite(mtime) ? mtime : 0,
  };
}

/** Strip trailing separators so "/a/b/" and "/a/b" name the same root. */
export function normalizeRoot(root: string): string {
  const r = root.replace(/[\\/]+$/, '');
  return r === '' && root !== '' ? root.slice(0, 1) : r;
}

/** A native watcher shared by every subscriber of one root. */
interface NativeWatch {
  /** Subscribers that joined (including those still waiting for `started`). */
  refs: number;
  /** Settles when `watch_start` has answered. */
  started: Promise<void>;
}

// Native watchers per normalised root (several adapters / subscribers may watch one root).
const nativeWatches = new Map<string, NativeWatch>();

// Tail of the watch_start / watch_stop calls per normalised root. Rust lets the last call
// for a root win, but async commands may run in any order, so a call is only sent once the
// previous one for that root has answered.
const nativeCalls = new Map<string, Promise<void>>();

function nativeWatchCall(key: string, cmd: 'watch_start' | 'watch_stop', root: string): Promise<void> {
  const previous = nativeCalls.get(key) ?? Promise.resolve();
  const call = previous.then(() => tauriInvoke<null>(cmd, { root })).then(() => undefined);
  const tail = call.catch(() => undefined);
  nativeCalls.set(key, tail);
  void tail.then(() => {
    if (nativeCalls.get(key) === tail) nativeCalls.delete(key);
  });
  return call;
}

/**
 * Join the native watcher of `root`, starting it when nobody watches yet. Resolves with a
 * release function once `watch_start` succeeded. When it fails, every subscriber that
 * joined that start rejects (and should poll instead); the next subscriber tries again.
 */
async function acquireNativeWatch(root: string): Promise<() => void> {
  const key = normalizeRoot(root);
  let watch = nativeWatches.get(key);
  if (!watch) {
    const state: NativeWatch = { refs: 0, started: nativeWatchCall(key, 'watch_start', root) };
    state.started.catch(() => {
      // Forget a failed start at once so that later subscribers retry it.
      if (nativeWatches.get(key) === state) nativeWatches.delete(key);
    });
    nativeWatches.set(key, state);
    watch = state;
  }
  const joined = watch;
  joined.refs++;
  try {
    await joined.started;
  } catch (e) {
    joined.refs--;
    throw e;
  }
  let released = false;
  return () => {
    if (released) return;
    released = true;
    joined.refs--;
    if (joined.refs > 0 || nativeWatches.get(key) !== joined) return;
    nativeWatches.delete(key);
    nativeWatchCall(key, 'watch_stop', root).catch((e: unknown) => console.warn('[platform] watch_stop failed', e));
  };
}

/** Number of native watch subscriptions for `root`, pending starts included (tests / diagnostics). */
export function nativeWatchCount(root: string): number {
  return nativeWatches.get(normalizeRoot(root))?.refs ?? 0;
}

/** A path as sent by Rust: `/`-separated, without leading / trailing separators. */
function cleanDiskPath(raw: string): string {
  return raw.replace(/\\/g, '/').replace(/^\/+|\/+$/g, '');
}

export interface TauriFsOptions {
  /** Human readable label (default: the root path). */
  label?: string;
  /** Binary bodies larger than this many bytes are sent as `{ base64 }` (Android). Default Infinity. */
  base64Threshold?: number;
  /** Polling interval of the fallback watcher in ms (default 5000). */
  pollInterval?: number;
}

/** FileSystemAdapter backed by the Rust `fs_*` commands. */
export class TauriFs implements FileSystemAdapter {
  readonly kind = 'tauri' as const;
  readonly label: string;
  /** Absolute vault folder, exactly as passed to the commands. */
  readonly root: string;
  private readonly base64Threshold: number;
  private readonly pollInterval: number;
  /** NFC vault path -> on-disk path, for entries whose on-disk name is not NFC. */
  private readonly diskNames = new Map<VaultPath, string>();

  constructor(root: string, opts: TauriFsOptions = {}) {
    if (!root) throw new FsError('EINVAL', root, 'Vault root must not be empty');
    this.root = root;
    this.label = opts.label ?? root;
    this.base64Threshold = opts.base64Threshold ?? Number.POSITIVE_INFINITY;
    this.pollInterval = opts.pollInterval ?? 5000;
  }

  async list(): Promise<FileStat[]> {
    const raw = await this.call<unknown>('fs_list', { root: this.root }, '', '');
    if (!Array.isArray(raw)) throw new TauriFsError('EIO', '', 'fs_list returned an invalid answer');
    const out: FileStat[] = [];
    // A full listing knows every on-disk spelling: rebuild the map (drops stale names).
    const names = new Map<VaultPath, string>();
    for (const item of raw) {
      const dto = parseStatDto(item);
      if (!dto) continue;
      const disk = cleanDiskPath(dto.path);
      const p = normalizePath(disk);
      if (p === '') continue;
      if (p !== disk) names.set(p, disk);
      out.push({ path: p, kind: dto.kind, size: dto.size, mtime: dto.mtime });
    }
    this.diskNames.clear();
    for (const [p, disk] of names) this.diskNames.set(p, disk);
    return out;
  }

  async stat(path: VaultPath): Promise<FileStat | null> {
    const p = normalizePath(path);
    const disk = this.toDisk(p);
    const dto = parseStatDto(await this.call<unknown>('fs_stat', { root: this.root, path: disk }, p, disk));
    return dto ? { ...this.fromDto(dto), path: p } : null;
  }

  async readText(path: VaultPath): Promise<string> {
    const p = this.entryPath(path);
    const disk = this.toDisk(p);
    const text = await this.call<unknown>('fs_read_text', { root: this.root, path: disk }, p, disk);
    if (typeof text !== 'string') throw new TauriFsError('EIO', p, 'fs_read_text returned an invalid answer');
    return text;
  }

  async readBinary(path: VaultPath): Promise<Uint8Array> {
    const p = this.entryPath(path);
    const disk = this.toDisk(p);
    const bytes = toBytes(await this.call<unknown>('fs_read_binary', { root: this.root, path: disk }, p, disk));
    if (!bytes) throw new TauriFsError('EIO', p, 'fs_read_binary returned an invalid answer');
    return bytes;
  }

  async writeText(path: VaultPath, content: string): Promise<FileStat> {
    const p = this.entryPath(path);
    const disk = this.toDisk(p);
    const dto = await this.call<unknown>('fs_write_text', { root: this.root, path: disk, content }, p, disk);
    return this.writeResult(dto, p, 'fs_write_text');
  }

  async writeBinary(path: VaultPath, data: Uint8Array): Promise<FileStat> {
    const p = this.entryPath(path);
    const disk = this.toDisk(p);
    const headers = { 'x-root': encodeURIComponent(this.root), 'x-path': encodeURIComponent(disk) };
    // The raw body is copied by the IPC layer when the request is created.
    const body: InvokeArgs = data.byteLength > this.base64Threshold ? { base64: bytesToBase64(data) } : data;
    const dto = await this.call<unknown>('fs_write_binary', body, p, disk, { headers });
    return this.writeResult(dto, p, 'fs_write_binary');
  }

  async remove(path: VaultPath): Promise<void> {
    const p = this.entryPath(path);
    const disk = this.toDisk(p);
    await this.call<null>('fs_remove', { root: this.root, path: disk }, p, disk);
    this.forgetDiskNames(p);
  }

  async rename(from: VaultPath, to: VaultPath): Promise<void> {
    const f = this.entryPath(from);
    const t = this.entryPath(to);
    if (f === t) {
      // Same logical path: nothing to do (also covers NFD/NFC spellings of one name).
      return;
    }
    const diskFrom = this.toDisk(f);
    const diskTo = this.toDisk(t);
    await this.call<null>('fs_rename', { root: this.root, from: diskFrom, to: diskTo }, f, diskFrom);
    this.rebaseDiskNames(f, diskFrom, t, diskTo);
  }

  async mkdir(path: VaultPath): Promise<void> {
    const p = normalizePath(path);
    const disk = this.toDisk(p);
    await this.call<null>('fs_mkdir', { root: this.root, path: disk }, p, disk);
  }

  /**
   * Absolute native path of a vault entry (the root itself for `''`), using the entry's
   * on-disk spelling. Windows-style roots get `\\` separators.
   */
  absolutePath(path: VaultPath): string {
    const disk = this.toDisk(normalizePath(path));
    if (disk === '') return this.root;
    const windows = /^[A-Za-z]:\\|^\\\\/.test(this.root) && !this.root.includes('/');
    const sep = windows ? '\\' : '/';
    const base = this.root.replace(/[\\/]+$/, '');
    return `${base}${sep}${windows ? disk.replace(/\//g, '\\') : disk}`;
  }

  /**
   * URL that shows a vault file (image, PDF, audio) in the WebView through Tauri's asset
   * protocol (`convertFileSrc`; `TauriPlatform.openFileSystem` allows the vault with
   * `allow_vault_assets`, dot-folders excluded). Null outside the Tauri runtime.
   */
  assetUrl(path: VaultPath): string | null {
    const internals = (globalThis as { __TAURI_INTERNALS__?: { convertFileSrc?: (filePath: string, protocol?: string) => string } })
      .__TAURI_INTERNALS__;
    if (typeof internals?.convertFileSrc !== 'function') return null;
    return internals.convertFileSrc(this.absolutePath(path), 'asset');
  }

  watch(onEvents: (events: FsEvent[]) => void): () => void {
    let active = true;
    let unlisten: (() => void) | null = null;
    let release: (() => void) | null = null;
    let stopPolling: (() => void) | null = null;
    const rootKey = normalizeRoot(this.root);

    const deliver = (events: FsEvent[]): void => {
      if (active && events.length > 0) onEvents(events);
    };
    const start = async (): Promise<void> => {
      const { listen } = await loadTauriEvent();
      const un = await listen<unknown>(FS_EVENTS, (ev) => {
        if (!active || !isRecord(ev.payload)) return;
        const payload = ev.payload;
        if (typeof payload.root !== 'string' || normalizeRoot(payload.root) !== rootKey) return;
        deliver(this.mapEvents(payload));
      });
      if (!active) {
        un();
        return;
      }
      unlisten = un;
      try {
        const rel = await acquireNativeWatch(this.root);
        if (active) release = rel;
        else rel();
      } catch (e) {
        console.warn('[platform] native watcher unavailable, polling instead', e);
        if (active) stopPolling = this.poll(deliver);
      }
    };
    start().catch((e: unknown) => {
      console.warn('[platform] cannot watch vault', e);
      if (active) stopPolling = this.poll(deliver);
    });

    return () => {
      if (!active) return;
      active = false;
      unlisten?.();
      stopPolling?.();
      release?.();
    };
  }

  /** Map a watcher payload to FsEvents (malformed entries dropped, `rescan` -> root event). */
  private mapEvents(payload: Record<string, unknown>): FsEvent[] {
    const events: FsEvent[] = [];
    const raw = Array.isArray(payload.events) ? payload.events : [];
    for (const item of raw) {
      if (!isRecord(item) || typeof item.path !== 'string' || typeof item.type !== 'string' || !EVENT_TYPES.has(item.type)) continue;
      const type = item.type as FsEventType;
      if (type === 'rename') {
        if (typeof item.oldPath !== 'string') continue;
        const oldDisk = cleanDiskPath(item.oldPath);
        const oldPath = normalizePath(oldDisk);
        const newDisk = cleanDiskPath(item.path);
        // Move the names below a renamed folder first, then record the new spelling (an
        // NFC -> NFD rename has the same vault path on both sides).
        this.rebaseDiskNames(oldPath, oldDisk, normalizePath(newDisk), newDisk);
        events.push({ type, path: this.fromDisk(item.path), oldPath });
      } else {
        const path = this.fromDisk(item.path);
        if (type === 'delete') this.forgetDiskNames(path);
        events.push({ type, path });
      }
    }
    if (payload.rescan === true) events.push({ type: 'modify', path: '' });
    return events;
  }

  /** Fallback watcher: diff (mtime, size) snapshots of `list()`. */
  private poll(deliver: (events: FsEvent[]) => void): () => void {
    let snapshot: Map<VaultPath, string> | null = null;
    let running = false;
    const tick = async (): Promise<void> => {
      if (running) return;
      running = true;
      try {
        const next = new Map((await this.list()).filter((s) => s.kind === 'file').map((s) => [s.path, `${s.mtime}:${s.size}`] as const));
        if (snapshot) deliver(diffSnapshots(snapshot, next));
        snapshot = next;
      } catch (e) {
        console.warn('[platform] polling the vault failed', e);
      } finally {
        running = false;
      }
    };
    void tick();
    const timer = setInterval(() => void tick(), this.pollInterval);
    return () => clearInterval(timer);
  }

  private async call<T>(cmd: string, args: InvokeArgs, path: string, diskPath: string, options?: InvokeOptions): Promise<T> {
    try {
      return await tauriInvoke<T>(cmd, args, options);
    } catch (e) {
      throw toFsError(e, path, diskPath);
    }
  }

  private entryPath(path: string): VaultPath {
    const p = normalizePath(path);
    if (p === '') throw new FsError('EINVAL', path, 'Path must not be empty');
    return p;
  }

  private writeResult(dto: unknown, path: VaultPath, cmd: string): FileStat {
    const st = parseStatDto(dto);
    if (!st) throw new TauriFsError('EIO', path, `${cmd} returned an invalid answer`);
    return { path, kind: st.kind, size: st.size, mtime: st.mtime };
  }

  /** FileStat with an NFC path; remembers non-NFC on-disk names. */
  private fromDto(dto: FileStatDto): FileStat {
    return { path: this.fromDisk(dto.path), kind: dto.kind, size: dto.size, mtime: dto.mtime };
  }

  /** NFC vault path of an on-disk path; remembers the spelling when it is not NFC. */
  private fromDisk(raw: string): VaultPath {
    const disk = cleanDiskPath(raw);
    const p = normalizePath(disk);
    if (p !== '' && p !== disk) this.diskNames.set(p, disk);
    return p;
  }

  /** On-disk spelling of an NFC vault path (longest known prefix wins). */
  private toDisk(p: VaultPath): string {
    if (this.diskNames.size === 0 || p === '') return p;
    const exact = this.diskNames.get(p);
    if (exact !== undefined) return exact;
    const segs = p.split('/');
    for (let i = segs.length - 1; i > 0; i--) {
      const prefix = segs.slice(0, i).join('/');
      const disk = this.diskNames.get(prefix);
      if (disk !== undefined) return `${disk}/${segs.slice(i).join('/')}`;
    }
    return p;
  }

  /** After a rename, children of a moved directory keep their on-disk spelling. */
  private rebaseDiskNames(from: VaultPath, diskFrom: string, to: VaultPath, diskTo: string): void {
    if (this.diskNames.size === 0) return;
    const moved: [VaultPath, string][] = [];
    for (const [key, disk] of this.diskNames) {
      if (key !== from && !key.startsWith(`${from}/`)) continue;
      this.diskNames.delete(key);
      if (key !== from && disk.startsWith(`${diskFrom}/`)) moved.push([to + key.slice(from.length), diskTo + disk.slice(diskFrom.length)]);
    }
    for (const [key, disk] of moved) if (key !== disk) this.diskNames.set(key, disk);
  }

  private forgetDiskNames(p: VaultPath): void {
    if (this.diskNames.size === 0) return;
    for (const key of this.diskNames.keys()) {
      if (key === p || key.startsWith(`${p}/`)) this.diskNames.delete(key);
    }
  }
}
