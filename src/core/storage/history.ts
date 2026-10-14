/**
 * Device-local version history of notes (never synced). Snapshots are stored in a
 * LocalStore: an index per path (`hist/idx/<path>` -> entries, oldest first) and the
 * content per snapshot (`hist/c/<id>`).
 *
 * Policy:
 *  - identical consecutive content is stored once;
 *  - at most one snapshot per path per 5 minutes for ordinary reasons ('user', 'system');
 *    'sync', 'import' and 'external' snapshots are always kept (they precede changes that
 *    did not come from the user typing), and the first ordinary snapshot after one of those
 *    is kept too;
 *  - pruning keeps at most 50 snapshots per path and drops snapshots older than 90 days,
 *    but always keeps the 5 newest.
 * Snapshots are kept in the order they were taken (not sorted by timestamp), so a device clock
 * that is set back (NTP correction, no real-time clock) neither reorders them nor makes prune
 * drop the newest ones; such a clock also never suppresses snapshots via the throttle.
 */
import { normalizePath } from '../paths';
import type { VaultPath } from '../types';
import { contentHash, utf8Length } from './hash';
import type { LocalStore } from './localStore';
import { KeyedLock } from './locks';

/** Snapshot metadata as returned by `History.list`. */
export interface HistoryEntry {
  id: string;
  /** Snapshot time, ms since epoch. */
  time: number;
  /** UTF-8 size of the content in bytes. */
  size: number;
  /** Why the snapshot was taken: the ChangeSource of the change that replaced it, or 'restore'. */
  reason: string;
}

interface StoredEntry extends HistoryEntry {
  hash: string;
}

export interface HistoryOptions {
  /** Clock (ms since epoch). Default `Date.now`. */
  now?: () => number;
  /** Minimum time between ordinary snapshots of one path. Default 5 minutes. */
  throttleMs?: number;
  /** Maximum snapshots kept per path. Default 50. */
  maxPerPath?: number;
  /** Snapshots older than this are pruned. Default 90 days. */
  maxAgeMs?: number;
  /** Newest snapshots always kept regardless of age. Default 5. */
  minKeep?: number;
}

const INDEX_PREFIX = 'hist/idx/';
const CONTENT_PREFIX = 'hist/c/';
const MINUTE = 60_000;
const DAY = 24 * 60 * MINUTE;
/** Content without an index entry younger than this is left alone by prune (in-flight snapshot). */
const ORPHAN_GRACE_MS = 10 * MINUTE;
const ALWAYS_KEEP = new Set(['sync', 'import', 'external']);

const indexKey = (path: VaultPath): string => INDEX_PREFIX + path;
const contentKey = (id: string): string => CONTENT_PREFIX + id;

function isStoredEntry(v: unknown): v is StoredEntry {
  if (typeof v !== 'object' || v === null) return false;
  const e = v as Record<string, unknown>;
  return typeof e.id === 'string' && typeof e.time === 'number' && typeof e.size === 'number' && typeof e.reason === 'string' && typeof e.hash === 'string';
}

/** Defensive read of a stored index, oldest first as stored (malformed data -> empty / filtered). */
function sanitizeEntries(v: unknown): StoredEntry[] {
  return Array.isArray(v) ? v.filter(isStoredEntry) : [];
}

/** Time encoded in a snapshot id (base-36 prefix), or NaN. */
function idTime(id: string): number {
  return parseInt(id.split('-')[0] ?? '', 36);
}

let idCounter = 0;

/** Per-path version history kept on this device. */
export class History {
  private readonly store: LocalStore;
  private readonly now: () => number;
  private readonly throttleMs: number;
  private readonly maxPerPath: number;
  private readonly maxAgeMs: number;
  private readonly minKeep: number;
  /**
   * Serialises operations per path within this instance. The index is always read from the
   * store (no in-memory copy), so instances sharing a store (two tabs) never act on stale data.
   */
  private readonly locks = new KeyedLock();

  constructor(store: LocalStore, opts: HistoryOptions = {}) {
    this.store = store;
    this.now = opts.now ?? Date.now;
    this.throttleMs = opts.throttleMs ?? 5 * MINUTE;
    this.maxPerPath = opts.maxPerPath ?? 50;
    this.maxAgeMs = opts.maxAgeMs ?? 90 * DAY;
    this.minKeep = opts.minKeep ?? 5;
  }

  /**
   * Record `content` as a version of `path`. `reason` is the ChangeSource of the change
   * about to replace it ('user', 'sync', 'external', 'import', 'system') or e.g. 'restore'.
   */
  snapshot(path: VaultPath, content: string, reason: string): Promise<void> {
    const p = normalizePath(path);
    return this.locks.run([p], () => this.doSnapshot(p, content, reason));
  }

  /** Snapshots of `path`, newest first (without content). */
  async list(path: VaultPath): Promise<HistoryEntry[]> {
    const p = normalizePath(path);
    await this.locks.wait([p]);
    const entries = await this.load(p);
    return entries
      .slice()
      .reverse()
      .map(({ id, time, size, reason }) => ({ id, time, size, reason }));
  }

  /** Content of a snapshot, or undefined if it does not exist (anymore). */
  async get(id: string): Promise<string | undefined> {
    const v = await this.store.get<unknown>(contentKey(id));
    return typeof v === 'string' ? v : undefined;
  }

  /** Paths that have at least one snapshot (also deleted notes), sorted. */
  async paths(): Promise<VaultPath[]> {
    return (await this.store.keys(INDEX_PREFIX)).map((k) => k.slice(INDEX_PREFIX.length));
  }

  /** Move the history of `from` to `to` (notes keep their history across renames). */
  rename(from: VaultPath, to: VaultPath): Promise<void> {
    const a = normalizePath(from);
    const b = normalizePath(to);
    if (a === b) return Promise.resolve();
    return this.locks.run([a, b], async () => {
      const moving = await this.load(a);
      if (moving.length === 0) return;
      const existing = await this.load(b);
      const merged = [...existing, ...moving].sort((x, y) => x.time - y.time);
      await this.store.setMany([
        [indexKey(b), merged],
        [indexKey(a), undefined],
      ]);
    });
  }

  /** Delete all snapshots of `path`. */
  remove(path: VaultPath): Promise<void> {
    const p = normalizePath(path);
    return this.locks.run([p], async () => {
      const entries = await this.load(p);
      await this.store.deleteMany([indexKey(p), ...entries.map((e) => contentKey(e.id))]);
    });
  }

  /**
   * Apply the retention policy to every path: keep at most `maxPerPath`, drop snapshots
   * older than `maxAgeMs`, but always keep the `minKeep` newest. Also removes orphaned
   * content left behind by interrupted snapshots.
   */
  async prune(): Promise<void> {
    const now = this.now();
    for (const p of await this.paths()) {
      await this.locks.run([p], async () => {
        const entries = await this.load(p);
        const kept = this.retain(entries, now);
        const dropped = entries.filter((e) => !kept.includes(e));
        if (dropped.length === 0) return;
        const droppedIds = new Set(dropped.map((e) => e.id));
        await this.store.update<unknown>(indexKey(p), (old) => {
          const next = sanitizeEntries(old).filter((e) => !droppedIds.has(e.id));
          return next.length > 0 ? next : undefined;
        });
        await this.store.deleteMany(dropped.map((e) => contentKey(e.id)));
      });
    }
    // Live ids are read from the indexes as they are now, not as they were when pruning
    // started: a rename (or another tab) may have moved entries to a path not visited above.
    const contentKeys = await this.store.keys(CONTENT_PREFIX);
    const liveIds = new Set<string>();
    for (const [, v] of await this.store.entries<unknown>(INDEX_PREFIX)) for (const e of sanitizeEntries(v)) liveIds.add(e.id);
    const orphans = contentKeys
      .map((k) => k.slice(CONTENT_PREFIX.length))
      .filter((id) => !liveIds.has(id) && !(now - idTime(id) < ORPHAN_GRACE_MS));
    if (orphans.length > 0) await this.store.deleteMany(orphans.map(contentKey));
  }

  // -------------------------------------------------------------------------

  private async load(p: VaultPath): Promise<StoredEntry[]> {
    return sanitizeEntries(await this.store.get<unknown>(indexKey(p)));
  }

  /** Entries to keep under the retention policy (input oldest first, output oldest first). */
  private retain(entries: StoredEntry[], now: number): StoredEntry[] {
    const n = entries.length;
    return entries.filter((e, i) => {
      const rank = n - 1 - i; // 0 = newest
      if (rank < this.minKeep) return true;
      if (rank >= this.maxPerPath) return false;
      return now - e.time <= this.maxAgeMs;
    });
  }

  private newId(time: number): string {
    idCounter = (idCounter + 1) % 1_679_616; // 36^4
    return `${time.toString(36)}-${idCounter.toString(36).padStart(4, '0')}${Math.random().toString(36).slice(2, 6)}`;
  }

  private async doSnapshot(p: VaultPath, content: string, reason: string): Promise<void> {
    const entries = await this.load(p);
    const newest = entries[entries.length - 1];
    const hash = contentHash(content);
    const time = this.now();
    if (newest && newest.hash === hash) return;
    // A negative age means the clock was set back: do not throttle (it could last for days).
    const age = newest === undefined ? Infinity : time - newest.time;
    const throttled = !ALWAYS_KEEP.has(reason) && newest !== undefined && !ALWAYS_KEEP.has(newest.reason) && age >= 0 && age < this.throttleMs;
    if (throttled) return;
    const entry: StoredEntry = { id: this.newId(time), time, size: utf8Length(content), reason, hash };
    // Content first: an interrupted snapshot leaves only an orphan (cleaned by prune), never a dangling entry.
    await this.store.set(contentKey(entry.id), content);
    let dropped: StoredEntry[] = [];
    await this.store.update<unknown>(indexKey(p), (old) => {
      const list = [...sanitizeEntries(old).filter((e) => e.id !== entry.id), entry];
      const kept = this.retain(list, time);
      dropped = list.filter((e) => !kept.includes(e));
      return kept;
    });
    if (dropped.length > 0) await this.store.deleteMany(dropped.map((e) => contentKey(e.id)));
  }
}
