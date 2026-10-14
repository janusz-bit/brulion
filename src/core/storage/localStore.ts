/**
 * Device-local key/value storage (sync bookkeeping, merge bases, history snapshots, UI
 * state). Never synced, never stored in the vault. Backed by IndexedDB database
 * `brulion-local` (one object store, keys `[namespace, key]`) or by an in-memory Map
 * (`LocalStore.memory`, tests and environments without IndexedDB). Values must be
 * structured-cloneable; they are cloned on the way in and out in both backends.
 */
import type { DBSchema, IDBPDatabase } from 'idb';
import { closeCachedDb, inTransaction, openCachedDb } from './idb';

export const LOCAL_DB = 'brulion-local';
const STORE = 'kv';

interface LocalSchema extends DBSchema {
  kv: { key: [string, string]; value: unknown };
}

/** Storage backend scoped to one namespace. */
interface KvBackend {
  get(key: string): Promise<unknown>;
  getMany(keys: readonly string[]): Promise<unknown[]>;
  /** Store `puts` and remove `deletes` atomically (one transaction). */
  write(puts: readonly (readonly [string, unknown])[], deletes: readonly string[]): Promise<void>;
  entries(prefix: string): Promise<[string, unknown][]>;
  keys(prefix: string): Promise<string[]>;
  update(key: string, fn: (old: unknown) => unknown): Promise<unknown>;
  clear(): Promise<void>;
}

function openLocalDb(): Promise<IDBPDatabase<LocalSchema>> {
  return openCachedDb<LocalSchema>(LOCAL_DB, 1, (db) => {
    if (!db.objectStoreNames.contains(STORE)) db.createObjectStore(STORE);
  });
}

/** Smallest string greater than every string starting with `prefix`, or null if none exists. */
function prefixUpperBound(prefix: string): string | null {
  for (let i = prefix.length - 1; i >= 0; i--) {
    const c = prefix.charCodeAt(i);
    if (c < 0xffff) return prefix.slice(0, i) + String.fromCharCode(c + 1);
  }
  return null;
}

function prefixRange(ns: string, prefix: string): IDBKeyRange {
  const upper = prefix === '' ? null : prefixUpperBound(prefix);
  // [ns, []] sorts after every [ns, string] key (arrays sort after strings in IndexedDB).
  return upper === null ? IDBKeyRange.bound([ns, prefix], [ns, []]) : IDBKeyRange.bound([ns, prefix], [ns, upper], false, true);
}

class IdbBackend implements KvBackend {
  constructor(
    private readonly db: IDBPDatabase<LocalSchema>,
    private readonly ns: string,
  ) {}

  get(key: string): Promise<unknown> {
    return this.db.get(STORE, [this.ns, key]);
  }

  getMany(keys: readonly string[]): Promise<unknown[]> {
    return inTransaction(this.db, [STORE], 'readonly', (tx) => Promise.all(keys.map((k) => tx.objectStore(STORE).get([this.ns, k]))));
  }

  async write(puts: readonly (readonly [string, unknown])[], deletes: readonly string[]): Promise<void> {
    if (puts.length === 0 && deletes.length === 0) return;
    await inTransaction(this.db, [STORE], 'readwrite', async (tx) => {
      const store = tx.objectStore(STORE);
      const requests: Promise<unknown>[] = [];
      try {
        for (const k of deletes) requests.push(store.delete([this.ns, k]));
        // put() throws synchronously for values that cannot be cloned.
        for (const [k, v] of puts) requests.push(store.put(v, [this.ns, k]));
      } catch (e) {
        // The transaction gets aborted; the requests already issued then reject with AbortError.
        for (const r of requests) r.catch(() => {});
        throw e;
      }
      await Promise.all(requests);
    });
  }

  async entries(prefix: string): Promise<[string, unknown][]> {
    return inTransaction(this.db, [STORE], 'readonly', async (tx) => {
      const range = prefixRange(this.ns, prefix);
      const store = tx.objectStore(STORE);
      const [keys, values] = await Promise.all([store.getAllKeys(range), store.getAll(range)]);
      const out: [string, unknown][] = [];
      keys.forEach((k, i) => {
        if (k[1].startsWith(prefix)) out.push([k[1], values[i]]);
      });
      return out;
    });
  }

  async keys(prefix: string): Promise<string[]> {
    const keys = await this.db.getAllKeys(STORE, prefixRange(this.ns, prefix));
    return keys.map((k) => k[1]).filter((k) => k.startsWith(prefix));
  }

  update(key: string, fn: (old: unknown) => unknown): Promise<unknown> {
    return inTransaction(this.db, [STORE], 'readwrite', async (tx) => {
      const store = tx.objectStore(STORE);
      const next = fn(await store.get([this.ns, key]));
      if (next === undefined) await store.delete([this.ns, key]);
      else await store.put(next, [this.ns, key]);
      return next;
    });
  }

  async clear(): Promise<void> {
    await this.db.delete(STORE, prefixRange(this.ns, ''));
  }
}

class MemoryBackend implements KvBackend {
  private readonly map = new Map<string, unknown>();

  async get(key: string): Promise<unknown> {
    return structuredClone(this.map.get(key));
  }

  async getMany(keys: readonly string[]): Promise<unknown[]> {
    return keys.map((k) => structuredClone(this.map.get(k)));
  }

  async write(puts: readonly (readonly [string, unknown])[], deletes: readonly string[]): Promise<void> {
    // Clone everything first: a value that cannot be cloned aborts the whole call, like IndexedDB.
    const cloned = puts.map(([k, v]) => [k, structuredClone(v)] as const);
    for (const k of deletes) this.map.delete(k);
    for (const [k, v] of cloned) this.map.set(k, v);
  }

  async entries(prefix: string): Promise<[string, unknown][]> {
    return this.sortedKeys(prefix).map((k) => [k, structuredClone(this.map.get(k))]);
  }

  async keys(prefix: string): Promise<string[]> {
    return this.sortedKeys(prefix);
  }

  async update(key: string, fn: (old: unknown) => unknown): Promise<unknown> {
    const next = fn(structuredClone(this.map.get(key)));
    if (next === undefined) this.map.delete(key);
    else this.map.set(key, structuredClone(next));
    return structuredClone(next);
  }

  async clear(): Promise<void> {
    this.map.clear();
  }

  private sortedKeys(prefix: string): string[] {
    return [...this.map.keys()].filter((k) => k.startsWith(prefix)).sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
  }
}

/** Namespaced device-local key/value store. */
export class LocalStore {
  readonly namespace: string;
  private readonly backend: KvBackend;

  private constructor(namespace: string, backend: KvBackend) {
    this.namespace = namespace;
    this.backend = backend;
  }

  /** Open the IndexedDB-backed store for `namespace`. Rejects when IndexedDB is unavailable. */
  static async open(namespace: string): Promise<LocalStore> {
    const db = await openLocalDb();
    return new LocalStore(namespace, new IdbBackend(db, namespace));
  }

  /** A fresh in-memory store (tests, fallbacks). Each call returns an independent store. */
  static memory(namespace: string): LocalStore {
    return new LocalStore(namespace, new MemoryBackend());
  }

  /** Open IndexedDB, or fall back to memory (with a warning) when it is unavailable. */
  static async openOrMemory(namespace: string): Promise<LocalStore> {
    try {
      return await LocalStore.open(namespace);
    } catch (e) {
      console.warn('[storage] IndexedDB unavailable, using in-memory store', e);
      return LocalStore.memory(namespace);
    }
  }

  /** Close the shared IndexedDB connection (tests). */
  static async closeDatabase(): Promise<void> {
    await closeCachedDb(LOCAL_DB);
  }

  async get<T>(key: string): Promise<T | undefined> {
    return (await this.backend.get(key)) as T | undefined;
  }

  async getMany<T>(keys: readonly string[]): Promise<(T | undefined)[]> {
    return (await this.backend.getMany(keys)) as (T | undefined)[];
  }

  /** Store `value` (structured clone). Setting `undefined` deletes the key. */
  async set(key: string, value: unknown): Promise<void> {
    if (value === undefined) return this.backend.write([], [key]);
    return this.backend.write([[key, value]], []);
  }

  /**
   * Write several entries atomically (one transaction); entries whose value is `undefined`
   * are deleted in the same transaction. If any value cannot be stored nothing is changed.
   */
  async setMany(entries: readonly (readonly [string, unknown])[]): Promise<void> {
    const puts = entries.filter(([, v]) => v !== undefined);
    const deletes = entries.filter(([, v]) => v === undefined).map(([k]) => k);
    return this.backend.write(puts, deletes);
  }

  async delete(key: string): Promise<void> {
    return this.backend.write([], [key]);
  }

  /** Delete several keys atomically. */
  async deleteMany(keys: readonly string[]): Promise<void> {
    return this.backend.write([], keys);
  }

  /** Keys (without namespace) starting with `prefix`, sorted. */
  async keys(prefix = ''): Promise<string[]> {
    return this.backend.keys(prefix);
  }

  /** `[key, value]` pairs for keys starting with `prefix`, sorted by key. */
  async entries<T>(prefix = ''): Promise<[string, T][]> {
    return (await this.backend.entries(prefix)) as [string, T][];
  }

  /**
   * Atomic read-modify-write of one key (a single IndexedDB transaction, so concurrent tabs
   * cannot interleave). `fn` must be synchronous; returning `undefined` deletes the key.
   * Resolves to the stored value.
   */
  async update<T>(key: string, fn: (old: T | undefined) => T | undefined): Promise<T | undefined> {
    return (await this.backend.update(key, fn as (old: unknown) => unknown)) as T | undefined;
  }

  /** Delete every key of this namespace (other namespaces are untouched). */
  async clear(): Promise<void> {
    return this.backend.clear();
  }
}
