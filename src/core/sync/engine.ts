/**
 * Bidirectional WebDAV sync of a vault (Nextcloud, ownCloud, any WebDAV server).
 *
 * For every path the engine compares the local file, the remote file and what both looked
 * like after the last sync (persisted per path in the LocalStore), then uploads, downloads,
 * deletes or merges. Text files changed on both sides are merged three-way against the last
 * synced content; real conflicts keep the local file and save the remote version as a
 * conflict copy next to it, so neither side is ever lost. Deletions go to the vault trash.
 */
import { isIgnoredPath, normalizePath } from '@/core/paths';
import { CONFIG_PATH, contentHash, encodeUtf8, isNotFoundError, utf8Length, type LocalStore, type Vault } from '@/core/storage';
import type { FileStat, VaultPath } from '../types';
import { conflictCopyPath, parseBrulionConflict } from './conflicts';
import { errorMessage, isAbortError, isWebDavError, SyncError } from './errors';
import { merge3, mergeJson3 } from './merge';
import { runPool } from './pool';
import type { PutOptions, RemoteEntry, SyncChangeAction, SyncFileState, SyncProgress, SyncReport } from './types';
import type { GetOptions, ListOptions } from './webdav';

/** Store key prefix of per-path sync state (`SyncFileState`). */
export const SYNC_STATE_PREFIX = 'sync:state:';
/** Store key prefix of merge bases (last synced text). */
export const SYNC_BASE_PREFIX = 'sync:base:';
/** Store key of engine metadata (which remote the state belongs to). */
export const SYNC_META_KEY = 'sync:meta';

const MB = 1024 * 1024;
/** Tracked notes (besides the vault config) before the "one side suddenly empty" guard applies. */
const EMPTY_SIDE_GUARD_MIN = 2;
/** Attempts to merge when the remote file keeps changing under us (HTTP 412). */
const MAX_MERGE_ATTEMPTS = 3;

/** The subset of `WebDavClient` the engine uses (tests may pass a fake). */
export interface SyncRemote {
  /** Changes when the account or folder changes; the engine then starts over. */
  readonly identity: string;
  list(opts?: ListOptions): Promise<RemoteEntry[]>;
  stat(path: VaultPath): Promise<RemoteEntry | null>;
  get(path: VaultPath, opts?: GetOptions): Promise<{ data: Uint8Array; etag: string }>;
  put(path: VaultPath, data: Uint8Array, opts?: PutOptions): Promise<{ etag?: string }>;
  delete(path: VaultPath, opts?: { ifMatch?: string }): Promise<void>;
  ensureBase(): Promise<void>;
}

/** Constructor options of `SyncEngine`. */
export interface SyncEngineOptions {
  vault: Vault;
  client: SyncRemote;
  /** Device-local store for sync bookkeeping (never synced). */
  store: LocalStore;
  /** This device's name, used in conflict copy names. */
  device: string;
  /** Parallel transfers (default 4). */
  concurrency?: number;
  /** Clock (ms). Default `Date.now`. */
  now?: () => number;
  /** Files larger than this are skipped (default 200 MB). */
  maxFileSize?: number;
  /** Text files up to this size keep a merge base (default 2 MB). */
  maxBaseSize?: number;
}

/** Options of one `SyncEngine.sync()` run. */
export interface SyncRunOptions {
  signal?: AbortSignal;
  onProgress?: (p: SyncProgress) => void;
  /**
   * Allow a run in which one side is empty although files were synced before (normally
   * refused with SyncError 'local-empty' / 'remote-empty' to protect against accidents).
   */
  allowEmptySide?: boolean;
}

/** Facts about one path that decide what to do. */
export interface SyncFacts {
  local: boolean;
  remote: boolean;
  /** A sync state exists (the path was synced before). */
  known: boolean;
  localChanged: boolean;
  remoteChanged: boolean;
}

/** What to do with one path. */
export type SyncAction = 'noop' | 'upload' | 'download' | 'reconcile' | 'deleteRemote' | 'deleteLocal' | 'forget';

/**
 * Decide the action for a path (pure). `reconcile` = compare contents and merge (both sides
 * changed, or both exist without history). A deletion on one side loses against a
 * modification on the other (the modified file is restored).
 */
export function planSync(f: SyncFacts): SyncAction {
  if (!f.known) {
    if (f.local && f.remote) return 'reconcile';
    if (f.local) return 'upload';
    if (f.remote) return 'download';
    return 'forget';
  }
  if (f.local && f.remote) {
    if (f.localChanged && f.remoteChanged) return 'reconcile';
    if (f.localChanged) return 'upload';
    if (f.remoteChanged) return 'download';
    return 'noop';
  }
  if (f.local) return f.localChanged ? 'upload' : 'deleteLocal';
  if (f.remote) return f.remoteChanged ? 'download' : 'deleteRemote';
  return 'forget';
}

const TEXT_EXTENSIONS = new Set(['md', 'markdown', 'txt', 'json', 'csv', 'canvas', 'css', 'ics', 'opml', 'html', 'svg']);

/** Files the engine merges as text (by extension); everything else is binary. */
export function isSyncTextPath(path: VaultPath): boolean {
  const name = path.slice(path.lastIndexOf('/') + 1);
  const dot = name.lastIndexOf('.');
  return dot > 0 && TEXT_EXTENSIONS.has(name.slice(dot + 1).toLowerCase());
}

/**
 * `isIgnoredPath`, with hidden (dot) names compared in any letter case: on case-insensitive
 * file systems (Android shared storage, FAT, Windows, macOS) a remote ".GIT/hooks/x" or
 * ".Obsidian/plugins/..." would land in the real ignored folder (code that git or Obsidian
 * runs). Visible names keep their case ("Import/Logseq/..." is a normal notes folder).
 */
function isIgnoredAnyCase(p: VaultPath): boolean {
  if (isIgnoredPath(p)) return true;
  const lowered = p
    .split('/')
    .map((seg) => (seg.startsWith('.') ? seg.toLowerCase() : seg))
    .join('/');
  return lowered !== p && isIgnoredPath(lowered);
}

/**
 * Paths the engine syncs: everything the vault tracks except ignored folders (`.trash`,
 * `.git`, `.obsidian`, `logseq`, ...; hidden ones in any letter case) and temp files, plus
 * `.brulion/config.json`.
 */
export function isSyncablePath(path: VaultPath): boolean {
  const p = normalizePath(path);
  if (p === '') return false;
  if (p === CONFIG_PATH) return true;
  return !isIgnoredAnyCase(p);
}

/** Remote folders not worth listing (ignored folders; `.brulion` holds the config). */
function skipRemoteDir(path: VaultPath): boolean {
  return path !== '.brulion' && isIgnoredAnyCase(path);
}

const strictUtf8 = new TextDecoder('utf-8', { fatal: true });

/**
 * Decode UTF-8 text (a leading BOM is dropped); undefined when the bytes are not valid UTF-8
 * (e.g. a Windows-1250 CSV). Such files are transferred as raw bytes, never re-encoded.
 */
export function decodeSyncText(bytes: Uint8Array): string | undefined {
  try {
    return strictUtf8.decode(bytes);
  } catch {
    return undefined;
  }
}

/** True when `text` parses as a JSON object. */
function isJsonObject(text: string): boolean {
  try {
    const v: unknown = JSON.parse(text.replace(/^\uFEFF/, ''));
    return typeof v === 'object' && v !== null && !Array.isArray(v);
  } catch {
    return false;
  }
}

/**
 * Version string of a remote file: its ETag, or (for servers without ETags) mtime + size.
 * Stored as `SyncFileState.remoteEtag`.
 */
export function remoteVersion(e: Pick<RemoteEntry, 'etag' | 'mtime' | 'size'>): string {
  return e.etag || `m${e.mtime}-s${e.size}`;
}

/** The local file changed while we were syncing it: skip it this time (not an error). */
class LocalChangedError extends Error {
  override name = 'LocalChangedError';
}

type LocalSnap =
  | { kind: 'text'; hash: string; stat: FileStat; text: string }
  | { kind: 'binary'; hash: string; stat: FileStat; bytes?: Uint8Array };

/** Content of the remote file as fetched by `get`. */
interface RemoteContent {
  data: Uint8Array;
  version: string;
  /** ETag usable for If-Match ('' when the server has none). */
  etag: string;
}

function emptyReport(startedAt: number): SyncReport {
  return {
    uploaded: 0,
    downloaded: 0,
    deletedLocal: 0,
    deletedRemote: 0,
    merged: 0,
    conflicts: [],
    conflictCopies: [],
    errors: [],
    skipped: [],
    changes: [],
    startedAt,
    finishedAt: startedAt,
  };
}

function isSyncFileState(v: unknown): v is SyncFileState {
  if (typeof v !== 'object' || v === null) return false;
  const s = v as Partial<SyncFileState>;
  return typeof s.localHash === 'string' && typeof s.remoteEtag === 'string' && typeof s.size === 'number';
}

/** Errors after which continuing with other files is pointless. */
function isFatal(e: unknown): boolean {
  return isWebDavError(e, 'auth') || isWebDavError(e, 'network') || isWebDavError(e, 'cors');
}

const byPath = (a: string, b: string): number => (a < b ? -1 : a > b ? 1 : 0);

/**
 * Group sorted paths whose names differ only in letter case (one file on case-insensitive
 * file systems such as Android shared storage). Inside a group, names that exist locally but
 * no longer on the server come first: after a case-only rename on another device the old
 * local name is trashed before the new name is downloaded, not the other way round (which
 * would trash the freshly downloaded file).
 */
export function groupByCase(paths: readonly VaultPath[], local: ReadonlyMap<VaultPath, unknown>, remote: ReadonlyMap<VaultPath, unknown>): VaultPath[][] {
  const groups = new Map<string, VaultPath[]>();
  for (const p of paths) {
    const key = p.toLowerCase();
    const g = groups.get(key);
    if (g) g.push(p);
    else groups.set(key, [p]);
  }
  const localOnlyFirst = (p: VaultPath): number => (local.has(p) && !remote.has(p) ? 0 : 1);
  return [...groups.values()].map((g) => (g.length > 1 ? g.sort((a, b) => localOnlyFirst(a) - localOnlyFirst(b) || byPath(a, b)) : g));
}

/** Bidirectional vault <-> WebDAV sync with three-way merge. */
export class SyncEngine {
  private readonly vault: Vault;
  private readonly client: SyncRemote;
  private readonly store: LocalStore;
  private readonly device: string;
  private readonly concurrency: number;
  private readonly now: () => number;
  private readonly maxFileSize: number;
  private readonly maxBaseSize: number;
  private running: Promise<SyncReport> | null = null;

  constructor(o: SyncEngineOptions) {
    this.vault = o.vault;
    this.client = o.client;
    this.store = o.store;
    this.device = o.device;
    this.concurrency = Math.max(1, o.concurrency ?? 4);
    this.now = o.now ?? Date.now;
    this.maxFileSize = o.maxFileSize ?? 200 * MB;
    this.maxBaseSize = o.maxBaseSize ?? 2 * MB;
  }

  /** Whether a sync is in progress. */
  get syncing(): boolean {
    return this.running !== null;
  }

  /**
   * Run one sync. Only one runs at a time: a call while syncing returns the running
   * promise. Rejects on connection-level failures (WebDavError 'auth', 'network', 'cors',
   * listing errors), on SyncError, and with the abort reason when `signal` aborts (files
   * already processed stay synced). Per-file failures are reported in `errors`.
   */
  sync(opts: SyncRunOptions = {}): Promise<SyncReport> {
    if (this.running) return this.running;
    const run = this.run(opts).finally(() => {
      this.running = null;
    });
    this.running = run;
    return run;
  }

  /** Forget all sync bookkeeping: the next sync behaves like the first one (nothing is deleted). */
  async resetState(): Promise<void> {
    const keys = [...(await this.store.keys(SYNC_STATE_PREFIX)), ...(await this.store.keys(SYNC_BASE_PREFIX)), SYNC_META_KEY];
    await this.store.deleteMany(keys);
  }

  /** Stored sync state of one path (for diagnostics / UI). */
  async stateOf(path: VaultPath): Promise<SyncFileState | undefined> {
    const v = await this.store.get<unknown>(SYNC_STATE_PREFIX + normalizePath(path));
    return isSyncFileState(v) ? v : undefined;
  }

  // -------------------------------------------------------------------------
  // Run
  // -------------------------------------------------------------------------

  private async run(opts: SyncRunOptions): Promise<SyncReport> {
    const { signal } = opts;
    const report = emptyReport(this.now());
    const progress = (p: SyncProgress): void => {
      try {
        opts.onProgress?.(p);
      } catch (e) {
        console.warn('[sync] progress callback failed', e);
      }
    };
    signal?.throwIfAborted();
    progress({ phase: 'connecting', done: 0, total: 0 });
    await this.checkRemoteIdentity();

    progress({ phase: 'listing', done: 0, total: 0 });
    const remote = await this.listRemote(signal);
    const local = this.listLocal();
    const states = await this.loadStates();
    signal?.throwIfAborted();
    if (!opts.allowEmptySide) this.guardEmptySides(local, remote, states);

    const paths = [...new Set([...local.keys(), ...remote.keys(), ...states.keys()])].sort(byPath);
    let done = 0;
    progress({ phase: 'syncing', done, total: paths.length });
    await runPool(
      groupByCase(paths, local, remote),
      async (group) => {
        // Names differing only in letter case are one file on case-insensitive file systems:
        // handle them one after another (see groupByCase), never in parallel.
        for (const path of group) {
          if (signal?.aborted) return;
          try {
            await this.syncPath(path, local.get(path), remote.get(path), states.get(path), report);
          } catch (e) {
            if (isFatal(e)) throw e;
            if (!(e instanceof LocalChangedError) && !(isAbortError(e) && signal?.aborted)) {
              report.errors.push({ path, message: errorMessage(e) });
            }
          }
          done++;
          progress({ phase: 'syncing', done, total: paths.length, path });
        }
      },
      { concurrency: this.concurrency, signal },
    );
    signal?.throwIfAborted();
    report.finishedAt = this.now();
    progress({ phase: 'done', done, total: paths.length });
    return report;
  }

  /** Reset the bookkeeping when the account / folder changed since the last sync. */
  private async checkRemoteIdentity(): Promise<void> {
    const meta = await this.store.get<{ remote?: unknown }>(SYNC_META_KEY);
    const identity = this.client.identity;
    if (meta?.remote === identity) return;
    if (meta) await this.resetState();
    await this.store.set(SYNC_META_KEY, { remote: identity });
  }

  private async listRemote(signal: AbortSignal | undefined): Promise<Map<VaultPath, RemoteEntry>> {
    let entries: RemoteEntry[];
    try {
      entries = await this.client.list({ skipDir: skipRemoteDir, ...(signal ? { signal } : {}) });
    } catch (e) {
      if (!isWebDavError(e, 'not-found')) throw e;
      // First sync into a folder that does not exist yet.
      await this.client.ensureBase();
      entries = [];
    }
    const out = new Map<VaultPath, RemoteEntry>();
    for (const e of entries) if (e.kind === 'file' && isSyncablePath(e.path)) out.set(e.path, e);
    return out;
  }

  private listLocal(): Map<VaultPath, FileStat> {
    const out = new Map<VaultPath, FileStat>();
    for (const st of this.vault.files()) if (st.kind === 'file' && isSyncablePath(st.path)) out.set(st.path, st);
    const config = this.vault.stat(CONFIG_PATH);
    if (config?.kind === 'file') out.set(CONFIG_PATH, config);
    return out;
  }

  private async loadStates(): Promise<Map<VaultPath, SyncFileState>> {
    const out = new Map<VaultPath, SyncFileState>();
    for (const [key, value] of await this.store.entries<unknown>(SYNC_STATE_PREFIX)) {
      const path = key.slice(SYNC_STATE_PREFIX.length);
      if (isSyncFileState(value) && isSyncablePath(path)) out.set(path, value);
    }
    return out;
  }

  /**
   * Refuse to mirror a side that suddenly lost every file synced before (wrong folder, wiped
   * server, vault storage lost): that would delete everything on the other side. Files created
   * since (today's journal, a default config) do not count: a vault the app has just
   * re-created must not wipe the server.
   */
  private guardEmptySides(local: Map<VaultPath, FileStat>, remote: Map<VaultPath, RemoteEntry>, states: Map<VaultPath, SyncFileState>): void {
    let tracked = 0;
    let localHas = false;
    let remoteHas = false;
    for (const path of states.keys()) {
      if (path === CONFIG_PATH) continue;
      tracked++;
      if (local.has(path)) localHas = true;
      if (remote.has(path)) remoteHas = true;
    }
    if (tracked < EMPTY_SIDE_GUARD_MIN) return;
    if (!localHas && remoteHas) {
      throw new SyncError('local-empty', `None of the ${tracked} files synced before exist locally; refusing to delete them on the server`);
    }
    if (!remoteHas && localHas) {
      throw new SyncError('remote-empty', `None of the ${tracked} files synced before exist on the server; refusing to delete them locally`);
    }
  }

  // -------------------------------------------------------------------------
  // One path
  // -------------------------------------------------------------------------

  private async syncPath(path: VaultPath, localStat: FileStat | undefined, remote: RemoteEntry | undefined, state: SyncFileState | undefined, report: SyncReport): Promise<void> {
    if ((localStat && localStat.size > this.maxFileSize) || (remote && remote.size > this.maxFileSize)) {
      report.skipped.push(path);
      return;
    }
    let snap: LocalSnap | null = null;
    if (localStat) {
      try {
        snap = await this.readLocal(path, localStat, state);
      } catch (e) {
        // Deleted or renamed since the listing: handled by the next sync.
        if (isNotFoundError(e)) throw new LocalChangedError(`${path} disappeared locally during sync`);
        throw e;
      }
    }
    const action = planSync({
      local: snap !== null,
      remote: remote !== undefined,
      known: state !== undefined,
      localChanged: snap !== null && (state === undefined || snap.hash !== state.localHash),
      remoteChanged: remote !== undefined && (state === undefined || remoteVersion(remote) !== state.remoteEtag),
    });
    switch (action) {
      case 'noop':
        return this.refreshState(path, snap!, state!);
      case 'upload':
        return this.upload(path, snap!, remote, state, report);
      case 'download':
        return this.download(path, snap, remote, report);
      case 'reconcile':
        return this.reconcile(path, snap!, remote, state, report, 0);
      case 'deleteRemote':
        return this.deleteRemote(path, remote!, report);
      case 'deleteLocal':
        return this.deleteLocal(path, snap!, report);
      case 'forget':
        return this.forget(path);
    }
  }

  /** Read (or, for unchanged binaries, skip reading) the local file and hash it. */
  private async readLocal(path: VaultPath, stat: FileStat, state: SyncFileState | undefined): Promise<LocalSnap> {
    if (isSyncTextPath(path)) {
      const text = await this.vault.read(path);
      if (!text.includes('\uFFFD')) return { kind: 'text', hash: contentHash(text), stat, text };
      // Maybe not UTF-8 (the vault decoded it lossily): check the real bytes.
      const bytes = await this.vault.readBinary(path);
      if (decodeSyncText(bytes) !== undefined) return { kind: 'text', hash: contentHash(text), stat, text };
      return { kind: 'binary', hash: contentHash(bytes), stat, bytes };
    }
    if (state && state.size === stat.size && state.mtime === stat.mtime) {
      return { kind: 'binary', hash: state.localHash, stat };
    }
    const bytes = await this.vault.readBinary(path);
    return { kind: 'binary', hash: contentHash(bytes), stat, bytes };
  }

  private async localBytes(path: VaultPath, snap: LocalSnap): Promise<Uint8Array> {
    if (snap.kind === 'text') return encodeUtf8(snap.text);
    return snap.bytes ?? (await this.vault.readBinary(path));
  }

  /** Throw LocalChangedError if the local file differs from `snap` (null = did not exist). */
  private async assertLocalUnchanged(path: VaultPath, snap: LocalSnap | null): Promise<void> {
    const current = this.vault.stat(path);
    if (!snap) {
      if (current) throw new LocalChangedError(`${path} was created locally during sync`);
      // The vault knows no file at this path, but the disk has one: on a case-insensitive file
      // system another note whose name differs only in letter case (or a file created by
      // another program and not seen yet). Never overwrite it.
      const onDisk = await this.vault.fs.stat(path);
      if (onDisk) {
        const other = this.vault.files().find((f) => f.path !== path && f.path.toLowerCase() === path.toLowerCase());
        throw new Error(
          other
            ? `${path}: a local file whose name differs only in letter case exists (${other.path}); rename one of them`
            : `${path}: a local file exists that the vault has not seen yet; will retry`,
        );
      }
      return;
    }
    if (!current || current.kind !== 'file') throw new LocalChangedError(`${path} was removed locally during sync`);
    if (snap.kind === 'text') {
      if (contentHash(await this.vault.read(path)) !== snap.hash) throw new LocalChangedError(`${path} changed locally during sync`);
    } else if (current.size !== snap.stat.size || current.mtime !== snap.stat.mtime) {
      throw new LocalChangedError(`${path} changed locally during sync`);
    }
  }

  private async fetchRemote(path: VaultPath, listed: RemoteEntry | undefined): Promise<RemoteContent> {
    const { data, etag } = await this.client.get(path, listed ? { size: listed.size } : {});
    // Servers without ETags in listings: keep the listing-based version scheme.
    let version = listed && !listed.etag ? remoteVersion(listed) : etag || (listed ? remoteVersion(listed) : '');
    if (!version) version = await this.versionAfterPut(path, undefined);
    return { data, version, etag: etag || listed?.etag || '' };
  }

  /** Remote version after a successful PUT / GET (from the response, else a PROPFIND). */
  private async versionAfterPut(path: VaultPath, etag: string | undefined): Promise<string> {
    if (etag) return etag;
    try {
      const st = await this.client.stat(path);
      return st ? remoteVersion(st) : '';
    } catch {
      return '';
    }
  }

  /**
   * Persist the synced state of `path`. `stat` is the local stat the hash belongs to
   * (default: the current one, right after our own write); `baseText` becomes the merge base.
   */
  private async saveState(path: VaultPath, hash: string, version: string, baseText: string | undefined, stat?: FileStat): Promise<void> {
    const st = stat ?? this.vault.stat(path);
    const state: SyncFileState = {
      localHash: hash,
      remoteEtag: version,
      size: st?.size ?? (baseText !== undefined ? utf8Length(baseText) : 0),
      ...(st ? { mtime: st.mtime } : {}),
      syncedAt: this.now(),
    };
    const keepBase = baseText !== undefined && isSyncTextPath(path) && utf8Length(baseText) <= this.maxBaseSize;
    await this.store.setMany([
      [SYNC_STATE_PREFIX + path, state],
      [SYNC_BASE_PREFIX + path, keepBase ? baseText : undefined],
    ]);
  }

  private async forget(path: VaultPath): Promise<void> {
    await this.store.deleteMany([SYNC_STATE_PREFIX + path, SYNC_BASE_PREFIX + path]);
  }

  private record(report: SyncReport, path: VaultPath, action: SyncChangeAction): void {
    report.changes.push({ path, action });
    switch (action) {
      case 'upload':
        report.uploaded++;
        break;
      case 'download':
        report.downloaded++;
        break;
      case 'merge':
        report.merged++;
        break;
      case 'conflict':
        report.conflicts.push(path);
        break;
      case 'deleteLocal':
        report.deletedLocal++;
        break;
      case 'deleteRemote':
        report.deletedRemote++;
        break;
    }
  }

  /** Nothing changed; keep the binary quick check (size + mtime) up to date. */
  private async refreshState(path: VaultPath, snap: LocalSnap, state: SyncFileState): Promise<void> {
    if (snap.stat.size === state.size && snap.stat.mtime === state.mtime) return;
    const next: SyncFileState = { ...state, size: snap.stat.size, mtime: snap.stat.mtime };
    await this.store.set(SYNC_STATE_PREFIX + path, next);
  }

  private async upload(path: VaultPath, snap: LocalSnap, remote: RemoteEntry | undefined, state: SyncFileState | undefined, report: SyncReport): Promise<void> {
    const bytes = await this.localBytes(path, snap);
    const opts: PutOptions = { mtime: snap.stat.mtime };
    if (!remote) opts.ifNoneMatch = true;
    else if (remote.etag) opts.ifMatch = remote.etag;
    let etag: string | undefined;
    try {
      ({ etag } = await this.client.put(path, bytes, opts));
    } catch (e) {
      // Someone changed / created the remote file since we listed it: compare and merge.
      if (isWebDavError(e, 'precondition')) return this.reconcile(path, snap, undefined, state, report, 1);
      throw e;
    }
    const version = await this.versionAfterPut(path, etag);
    await this.saveState(path, snap.hash, version, snap.kind === 'text' ? snap.text : undefined, snap.stat);
    this.record(report, path, 'upload');
  }

  private async download(path: VaultPath, snap: LocalSnap | null, remote: RemoteEntry | undefined, report: SyncReport): Promise<void> {
    let content: RemoteContent;
    try {
      content = await this.fetchRemote(path, remote);
    } catch (e) {
      // Deleted on the server since we listed it: the next sync sees the deletion.
      if (isWebDavError(e, 'not-found')) return;
      throw e;
    }
    const text = isSyncTextPath(path) ? decodeSyncText(content.data) : undefined;
    const hash = text !== undefined ? contentHash(text) : contentHash(content.data);
    if (snap && snap.hash === hash) {
      // Same content (e.g. ETag changed without a content change): just remember it.
      await this.saveState(path, hash, content.version, text, snap.stat);
      return;
    }
    await this.assertLocalUnchanged(path, snap);
    if (text !== undefined) await this.vault.write(path, text, 'sync');
    else await this.vault.writeBinary(path, content.data, 'sync');
    await this.saveState(path, hash, content.version, text);
    this.record(report, path, 'download');
  }

  /**
   * Both sides exist and (may) differ: fetch the remote file and merge. Clean merges are
   * written locally and uploaded; real conflicts keep the local file, save the remote one as
   * a conflict copy and upload the local file over the remote one.
   */
  private async reconcile(
    path: VaultPath,
    snap: LocalSnap,
    listed: RemoteEntry | undefined,
    state: SyncFileState | undefined,
    report: SyncReport,
    attempt: number,
  ): Promise<void> {
    if (attempt >= MAX_MERGE_ATTEMPTS) throw new SyncError('remote-busy', `${path} keeps changing on the server; will retry on the next sync`);
    let remote: RemoteContent;
    try {
      remote = await this.fetchRemote(path, listed);
    } catch (e) {
      if (!isWebDavError(e, 'not-found')) throw e;
      // Gone on the server meanwhile: upload ours as a new file.
      return this.upload(path, snap, undefined, state, report);
    }
    const retry = async (): Promise<void> => {
      const current = this.vault.stat(path);
      if (!current) throw new LocalChangedError(`${path} was removed locally during sync`);
      const fresh = await this.readLocal(path, current, undefined);
      return this.reconcile(path, fresh, undefined, state, report, attempt + 1);
    };

    const remoteText = snap.kind === 'text' ? decodeSyncText(remote.data) : undefined;
    if (snap.kind === 'binary' || remoteText === undefined) {
      if (contentHash(remote.data) === snap.hash) return this.saveState(path, snap.hash, remote.version, undefined, snap.stat);
      return this.conflict(path, snap, remote, report, retry);
    }

    if (remoteText === snap.text) return this.saveState(path, snap.hash, remote.version, snap.text, snap.stat);
    const merged = await this.mergeText(path, snap.text, remoteText, state);
    if (merged === null) return this.conflict(path, snap, remote, report, retry);

    await this.assertLocalUnchanged(path, snap);
    if (merged !== snap.text) await this.vault.write(path, merged, 'sync');
    let version = remote.version;
    if (merged !== remoteText) {
      try {
        const res = await this.client.put(path, encodeUtf8(merged), remote.etag ? { ifMatch: remote.etag } : {});
        version = await this.versionAfterPut(path, res.etag);
      } catch (e) {
        if (isWebDavError(e, 'precondition')) return retry();
        throw e;
      }
    }
    await this.saveState(path, contentHash(merged), version, merged);
    this.record(report, path, merged === remoteText ? 'download' : merged === snap.text ? 'upload' : 'merge');
  }

  /** Clean merge of local and remote text, or null for a real conflict. */
  private async mergeText(path: VaultPath, local: string, remote: string, state: SyncFileState | undefined): Promise<string | null> {
    // No history: merge against an empty base (keeps the union of both versions).
    const base = state ? await this.store.get<unknown>(SYNC_BASE_PREFIX + path) : '';
    if (path === CONFIG_PATH) {
      // The vault config is JSON: a line union (first sync) or a line merge can produce invalid
      // JSON (missing commas, duplicate keys), so merge key by key unless the line merge is fine.
      if (typeof base !== 'string' || base === '') return mergeJson3('', local, remote);
      const result = merge3(base, local, remote);
      if (result.clean && isJsonObject(result.text)) return result.text;
      return mergeJson3(base, local, remote);
    }
    if (typeof base !== 'string') return null;
    const result = merge3(base, local, remote);
    return result.clean ? result.text : null;
  }

  private async conflict(path: VaultPath, snap: LocalSnap, remote: RemoteContent, report: SyncReport, retry: () => Promise<void>): Promise<void> {
    // 1. Keep the remote version safe locally first (once: an earlier run that failed later
    //    may already have saved this very version).
    const remoteContent: string | Uint8Array = (isSyncTextPath(path) ? decodeSyncText(remote.data) : undefined) ?? remote.data;
    const existing = await this.findConflictCopy(path, remoteContent);
    const copy = existing ?? (await this.vault.create(conflictCopyPath(path, this.device, this.now()), remoteContent, 'sync'));
    if (!existing) report.conflictCopies.push(copy);
    // 2. The local version wins at the original path.
    let version: string;
    try {
      const res = await this.client.put(path, await this.localBytes(path, snap), remote.etag ? { ifMatch: remote.etag } : {});
      version = await this.versionAfterPut(path, res.etag);
    } catch (e) {
      if (isWebDavError(e, 'precondition')) return retry();
      throw e;
    }
    await this.saveState(path, snap.hash, version, snap.kind === 'text' ? snap.text : undefined, snap.stat);
    this.record(report, path, 'conflict');
    // An existing copy is a normal local file of this run (or the next one): synced as such.
    if (existing) return;
    // 3. Upload the copy right away so other devices see both versions (else: next sync).
    try {
      const copyBytes = typeof remoteContent === 'string' ? encodeUtf8(remoteContent) : remoteContent;
      const res = await this.client.put(copy, copyBytes, { ifNoneMatch: true });
      const copyVersion = await this.versionAfterPut(copy, res.etag);
      await this.saveState(copy, contentHash(remoteContent), copyVersion, typeof remoteContent === 'string' ? remoteContent : undefined);
    } catch (e) {
      if (isFatal(e)) throw e;
      console.warn('[sync] conflict copy will be uploaded on the next sync', copy, e);
    }
  }

  /** An existing conflict copy of `path` whose content equals `content`, if any. */
  private async findConflictCopy(path: VaultPath, content: string | Uint8Array): Promise<VaultPath | undefined> {
    const size = typeof content === 'string' ? utf8Length(content) : content.byteLength;
    const hash = contentHash(content);
    for (const f of this.vault.files()) {
      if (f.kind !== 'file' || f.size !== size || parseBrulionConflict(f.path)?.original !== path) continue;
      try {
        const current = typeof content === 'string' ? await this.vault.read(f.path) : await this.vault.readBinary(f.path);
        if (contentHash(current) === hash) return f.path;
      } catch (e) {
        if (!isNotFoundError(e)) throw e;
      }
    }
    return undefined;
  }

  private async deleteRemote(path: VaultPath, remote: RemoteEntry, report: SyncReport): Promise<void> {
    try {
      await this.client.delete(path, remote.etag ? { ifMatch: remote.etag } : {});
    } catch (e) {
      // Changed on the server since we listed it: the modification wins, restore it locally.
      if (isWebDavError(e, 'precondition')) return this.download(path, null, undefined, report);
      throw e;
    }
    await this.forget(path);
    this.record(report, path, 'deleteRemote');
  }

  private async deleteLocal(path: VaultPath, snap: LocalSnap, report: SyncReport): Promise<void> {
    await this.assertLocalUnchanged(path, snap);
    await this.vault.delete(path, 'sync');
    await this.forget(path);
    this.record(report, path, 'deleteLocal');
  }
}
