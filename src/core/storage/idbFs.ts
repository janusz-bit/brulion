/**
 * IndexedDB-backed FileSystemAdapter (browser vaults without a real folder).
 *
 * Database `brulion-fs`, two object stores keyed by `[vaultId, path]`:
 *  - `files`: `{ vaultId, path, kind, size, mtime }` — metadata only, so `list()` stays cheap
 *    even with large attachments;
 *  - `data`:  `{ vaultId, path, data: string | Uint8Array }` — file contents.
 * Directories are explicit records (from `mkdir`) or implied by the files below them. Like on
 * disk, a folder stays when its last entry is removed or moved away (it becomes explicit).
 * Every operation runs in a single transaction, so writes, recursive renames and removes are
 * atomic. Changes are broadcast to other tabs (BroadcastChannel) and reported by `watch()`.
 */
import type { DBSchema, IDBPDatabase, IDBPTransaction } from 'idb';
import { dirname, normalizePath } from '../paths';
import type { FileStat, FileSystemAdapter, FsEvent, VaultPath } from '../types';
import { FsError, requireEntryPath } from './errors';
import { decodeUtf8, encodeUtf8, utf8Length } from './hash';
import { closeCachedDb, inTransaction, openCachedDb, withConnection } from './idb';
import { ancestors, isStrictlyWithin, rebase } from './pathUtil';

export const IDB_FS_DB = 'brulion-fs';
const DB_VERSION = 1;

interface FileMetaRecord {
  vaultId: string;
  path: VaultPath;
  kind: 'file' | 'dir';
  size: number;
  mtime: number;
}

interface FileDataRecord {
  vaultId: string;
  path: VaultPath;
  data: string | Uint8Array;
}

interface FsSchema extends DBSchema {
  files: { key: [string, string]; value: FileMetaRecord };
  data: { key: [string, string]; value: FileDataRecord };
}

type Stores = ('files' | 'data')[];
type RwTx = IDBPTransaction<FsSchema, Stores, 'readwrite'>;
type AnyTx = IDBPTransaction<FsSchema, Stores, IDBTransactionMode>;

function openFsDb(): Promise<IDBPDatabase<FsSchema>> {
  return openCachedDb<FsSchema>(IDB_FS_DB, DB_VERSION, (db) => {
    if (!db.objectStoreNames.contains('files')) db.createObjectStore('files', { keyPath: ['vaultId', 'path'] });
    if (!db.objectStoreNames.contains('data')) db.createObjectStore('data', { keyPath: ['vaultId', 'path'] });
  });
}

/** Run `fn` on the shared connection, reopening it once if the browser closed or lost it. */
function withFsDb<T>(fn: (db: IDBPDatabase<FsSchema>) => Promise<T>): Promise<T> {
  return withConnection(IDB_FS_DB, openFsDb, fn);
}

/** Key range covering every path of a vault: [id, ""] .. [id, []] (arrays sort after strings). */
function vaultRange(vaultId: string): IDBKeyRange {
  return IDBKeyRange.bound([vaultId, ''], [vaultId, []]);
}

/** Key range covering exactly the paths strictly inside `dir` ("dir/" <= p < "dir0"). */
function subtreeRange(vaultId: string, dir: VaultPath): IDBKeyRange {
  return IDBKeyRange.bound([vaultId, `${dir}/`], [vaultId, `${dir}0`], false, true);
}

/** Largest `mtime` of `records` (0 when empty). A loop, not `Math.max(...)`: huge folders would overflow the stack. */
export function newestMtime(records: readonly { mtime: number }[]): number {
  let max = 0;
  for (const r of records) if (r.mtime > max) max = r.mtime;
  return max;
}

function toStat(r: FileMetaRecord): FileStat {
  return { path: r.path, kind: r.kind, size: r.kind === 'file' ? r.size : 0, mtime: r.mtime };
}

export interface IdbFsOptions {
  /** Clock for modification times. Default `Date.now`. */
  now?: () => number;
  /** Broadcast changes to other tabs and receive theirs in `watch()`. Default true. */
  broadcast?: boolean;
}

interface BroadcastMessage {
  sender: string;
  events: FsEvent[];
}

let instanceCounter = 0;

/** FileSystemAdapter storing a vault inside IndexedDB. */
export class IdbFs implements FileSystemAdapter {
  readonly kind = 'idb' as const;
  readonly label: string;
  readonly vaultId: string;
  private readonly now: () => number;
  private readonly broadcast: boolean;
  private readonly instanceId: string;
  private sender: BroadcastChannel | null = null;

  constructor(vaultId: string, label?: string, opts: IdbFsOptions = {}) {
    if (!vaultId) throw new FsError('EINVAL', vaultId, 'Vault id must not be empty');
    this.vaultId = vaultId;
    this.label = label ?? `IndexedDB (${vaultId})`;
    this.now = opts.now ?? Date.now;
    this.broadcast = opts.broadcast ?? true;
    this.instanceId = `${Date.now().toString(36)}-${(instanceCounter++).toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
  }

  /** Delete every file of a vault from IndexedDB. */
  static async deleteVault(vaultId: string): Promise<void> {
    await withFsDb((db) => inTransaction(db, ['files', 'data'], 'readwrite', async (tx) => {
      await Promise.all([tx.objectStore('files').delete(vaultRange(vaultId)), tx.objectStore('data').delete(vaultRange(vaultId))]);
    }));
  }

  /** Close the shared database connection (tests). */
  static async closeDatabase(): Promise<void> {
    await closeCachedDb(IDB_FS_DB);
  }

  // -------------------------------------------------------------------------
  // FileSystemAdapter
  // -------------------------------------------------------------------------

  async list(): Promise<FileStat[]> {
    const records = await withFsDb((db) => db.getAll('files', vaultRange(this.vaultId)));
    const out = new Map<VaultPath, FileStat>();
    for (const r of records) out.set(r.path, toStat(r));
    // Implied directories: every ancestor of a stored entry; mtime = newest descendant.
    for (const r of records) {
      for (const dir of ancestors(r.path)) {
        const existing = out.get(dir);
        if (!existing) out.set(dir, { path: dir, kind: 'dir', size: 0, mtime: r.mtime });
        else if (existing.kind === 'dir' && r.mtime > existing.mtime) existing.mtime = r.mtime;
      }
    }
    return [...out.values()].sort((a, b) => (a.path < b.path ? -1 : a.path > b.path ? 1 : 0));
  }

  async stat(path: VaultPath): Promise<FileStat | null> {
    const p = normalizePath(path);
    if (p === '') return { path: '', kind: 'dir', size: 0, mtime: 0 };
    return withFsDb((db) => inTransaction(db, ['files'], 'readonly', (tx) => this.statIn(tx, p)));
  }

  async readText(path: VaultPath): Promise<string> {
    const data = await this.readData(path);
    return typeof data === 'string' ? data : decodeUtf8(data);
  }

  async readBinary(path: VaultPath): Promise<Uint8Array> {
    const data = await this.readData(path);
    return typeof data === 'string' ? encodeUtf8(data) : new Uint8Array(data);
  }

  writeText(path: VaultPath, content: string): Promise<FileStat> {
    return this.writeData(path, content, utf8Length(content));
  }

  writeBinary(path: VaultPath, data: Uint8Array): Promise<FileStat> {
    return this.writeData(path, data.slice(), data.byteLength);
  }

  async remove(path: VaultPath): Promise<void> {
    const p = requireEntryPath(path);
    const removed = await withFsDb((db) => inTransaction(db, ['files', 'data'], 'readwrite', async (tx) => {
      const files = tx.objectStore('files');
      const keys = [...(await files.getAllKeys(subtreeRange(this.vaultId, p)))];
      const own = await files.getKey([this.vaultId, p]);
      if (own) keys.push(own);
      if (keys.length > 0) await this.keepParent(tx, p);
      await Promise.all([
        files.delete([this.vaultId, p]),
        files.delete(subtreeRange(this.vaultId, p)),
        tx.objectStore('data').delete([this.vaultId, p]),
        tx.objectStore('data').delete(subtreeRange(this.vaultId, p)),
      ]);
      return keys.map((k) => k[1]);
    }));
    this.post(removed.map((r) => ({ type: 'delete', path: r })));
  }

  async rename(from: VaultPath, to: VaultPath): Promise<void> {
    const a = requireEntryPath(from);
    const b = requireEntryPath(to);
    if (isStrictlyWithin(b, a)) throw new FsError('EINVAL', b, 'Cannot move a directory into itself');
    const moved = await withFsDb((db) => inTransaction(db, ['files', 'data'], 'readwrite', async (tx) => {
      const src = await this.statIn(tx, a);
      if (!src) throw new FsError('ENOENT', a);
      if (a === b) return false;
      const dest = await this.statIn(tx, b);
      if (dest) {
        const bothFiles = src.kind === 'file' && dest.kind === 'file';
        const dirOverEmptyDir = src.kind === 'dir' && dest.kind === 'dir' && !(await this.hasChildren(tx, b));
        if (!bothFiles && !dirOverEmptyDir) throw new FsError('EEXIST', b);
        await this.deleteIn(tx, b);
      }
      await this.assertParentsAreDirs(tx, b);
      await this.keepParent(tx, a);
      await this.moveIn(tx, a, b);
      return true;
    }));
    if (moved) this.post([{ type: 'rename', path: b, oldPath: a }]);
  }

  async mkdir(path: VaultPath): Promise<void> {
    const p = normalizePath(path);
    if (p === '') return;
    const created = await withFsDb((db) => inTransaction(db, ['files', 'data'], 'readwrite', async (tx) => {
      const existing = await this.statIn(tx, p);
      if (existing?.kind === 'file') throw new FsError('EEXIST', p);
      await this.assertParentsAreDirs(tx, p);
      if (existing) return false;
      await tx.objectStore('files').put({ vaultId: this.vaultId, path: p, kind: 'dir', size: 0, mtime: this.now() });
      return true;
    }));
    if (created) this.post([{ type: 'create', path: p }]);
  }

  /**
   * Reports changes made to the same vault by other IdbFs instances (other tabs/windows),
   * delivered through BroadcastChannel. Own writes are not reported.
   */
  watch(onEvents: (events: FsEvent[]) => void): () => void {
    if (typeof BroadcastChannel === 'undefined') return () => {};
    const channel = new BroadcastChannel(this.channelName());
    channel.onmessage = (ev: MessageEvent<BroadcastMessage>) => {
      const msg = ev.data;
      if (!msg || msg.sender === this.instanceId || !Array.isArray(msg.events) || msg.events.length === 0) return;
      try {
        onEvents(msg.events);
      } catch (e) {
        console.warn('[storage] IdbFs watcher callback failed', e);
      }
    };
    return () => channel.close();
  }

  /** Close the broadcast channel used to notify other tabs. */
  close(): void {
    this.sender?.close();
    this.sender = null;
  }

  // -------------------------------------------------------------------------
  // Internals
  // -------------------------------------------------------------------------

  private channelName(): string {
    return `brulion-fs:${this.vaultId}`;
  }

  private post(events: FsEvent[]): void {
    if (!this.broadcast || events.length === 0 || typeof BroadcastChannel === 'undefined') return;
    try {
      if (!this.sender) {
        this.sender = new BroadcastChannel(this.channelName());
        // Node keeps the process alive while a channel is open; browsers have no unref().
        (this.sender as unknown as { unref?: () => void }).unref?.();
      }
      const msg: BroadcastMessage = { sender: this.instanceId, events };
      this.sender.postMessage(msg);
    } catch (e) {
      console.warn('[storage] IdbFs broadcast failed', e);
    }
  }

  private async readData(path: VaultPath): Promise<string | Uint8Array> {
    const p = requireEntryPath(path);
    return withFsDb((db) => inTransaction(db, ['files', 'data'], 'readonly', async (tx) => {
      const rec = await tx.objectStore('data').get([this.vaultId, p]);
      if (rec) return rec.data;
      const st = await this.statIn(tx, p);
      throw new FsError(st?.kind === 'dir' ? 'EISDIR' : 'ENOENT', p);
    }));
  }

  private async writeData(path: VaultPath, data: string | Uint8Array, size: number): Promise<FileStat> {
    const p = requireEntryPath(path);
    const { stat, created } = await withFsDb((db) => inTransaction(db, ['files', 'data'], 'readwrite', async (tx) => {
      const existing = await this.statIn(tx, p);
      if (existing?.kind === 'dir') throw new FsError('EISDIR', p);
      await this.assertParentsAreDirs(tx, p);
      const meta: FileMetaRecord = { vaultId: this.vaultId, path: p, kind: 'file', size, mtime: this.now() };
      await Promise.all([tx.objectStore('files').put(meta), tx.objectStore('data').put({ vaultId: this.vaultId, path: p, data })]);
      return { stat: toStat(meta), created: !existing };
    }));
    this.post([{ type: created ? 'create' : 'modify', path: p }]);
    return stat;
  }

  private async statIn(tx: AnyTx, p: VaultPath): Promise<FileStat | null> {
    const files = tx.objectStore('files');
    const rec = await files.get([this.vaultId, p]);
    if (rec) return toStat(rec);
    const children = await files.getAll(subtreeRange(this.vaultId, p));
    if (children.length === 0) return null;
    return { path: p, kind: 'dir', size: 0, mtime: newestMtime(children) };
  }

  /**
   * Make the parent folder of `p` an explicit record (if it is only implied), so it does not
   * vanish when `p` is removed or moved away.
   */
  private async keepParent(tx: RwTx, p: VaultPath): Promise<void> {
    const parent = dirname(p);
    if (parent === '') return;
    const files = tx.objectStore('files');
    if (await files.getKey([this.vaultId, parent])) return;
    await files.put({ vaultId: this.vaultId, path: parent, kind: 'dir', size: 0, mtime: this.now() });
  }

  private async hasChildren(tx: AnyTx, p: VaultPath): Promise<boolean> {
    return (await tx.objectStore('files').count(subtreeRange(this.vaultId, p))) > 0;
  }

  private async assertParentsAreDirs(tx: AnyTx, p: VaultPath): Promise<void> {
    const files = tx.objectStore('files');
    for (const dir of ancestors(p)) {
      const rec = await files.get([this.vaultId, dir]);
      if (rec?.kind === 'file') throw new FsError('ENOTDIR', dir);
    }
  }

  private async deleteIn(tx: RwTx, p: VaultPath): Promise<void> {
    const files = tx.objectStore('files');
    const data = tx.objectStore('data');
    await Promise.all([
      files.delete([this.vaultId, p]),
      files.delete(subtreeRange(this.vaultId, p)),
      data.delete([this.vaultId, p]),
      data.delete(subtreeRange(this.vaultId, p)),
    ]);
  }

  /** Move the entry at `a` (and its subtree) to `b`, preserving mtimes. */
  private async moveIn(tx: RwTx, a: VaultPath, b: VaultPath): Promise<void> {
    const files = tx.objectStore('files');
    const data = tx.objectStore('data');
    const own = await files.get([this.vaultId, a]);
    const metas = await files.getAll(subtreeRange(this.vaultId, a));
    if (own) metas.push(own);
    const blobs = await data.getAll(subtreeRange(this.vaultId, a));
    const ownData = await data.get([this.vaultId, a]);
    if (ownData) blobs.push(ownData);
    await this.deleteIn(tx, a);
    await Promise.all([
      ...metas.map((m) => files.put({ ...m, path: rebase(m.path, a, b) })),
      ...blobs.map((d) => data.put({ ...d, path: rebase(d.path, a, b) })),
    ]);
  }
}
