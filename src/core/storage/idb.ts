/**
 * Shared IndexedDB connection handling (via `idb`). Connections are opened lazily, cached
 * per database name and dropped when another tab needs to upgrade the schema or the browser
 * terminates the connection, so the next call simply reopens.
 */
import { openDB, type DBSchema, type IDBPDatabase, type IDBPTransaction, type StoreNames } from 'idb';

type UpgradeFn<S extends DBSchema> = (db: IDBPDatabase<S>, oldVersion: number) => void;

// Values are IDBPDatabase<S> for the schema each caller declares; the map itself cannot be typed per key.
const connections = new Map<string, Promise<IDBPDatabase<unknown>>>();

/** Open (or reuse) a database connection. */
export function openCachedDb<S extends DBSchema>(name: string, version: number, upgrade: UpgradeFn<S>): Promise<IDBPDatabase<S>> {
  const cached = connections.get(name);
  if (cached) return cached as unknown as Promise<IDBPDatabase<S>>;
  if (typeof indexedDB === 'undefined') return Promise.reject(new Error('IndexedDB is not available'));
  const evict = (): void => {
    connections.delete(name);
  };
  const promise = openDB<S>(name, version, {
    upgrade(db, oldVersion) {
      upgrade(db, oldVersion);
    },
    blocking() {
      // Another connection wants a newer schema: let it proceed, reopen on next use.
      evict();
      void promise.then((db) => db.close()).catch(() => {});
    },
    terminated: evict,
  });
  promise.catch(evict);
  connections.set(name, promise as unknown as Promise<IDBPDatabase<unknown>>);
  return promise;
}

/** Close and forget a cached connection (tests, deleting databases). */
export async function closeCachedDb(name: string): Promise<void> {
  const cached = connections.get(name);
  connections.delete(name);
  if (cached) (await cached.catch(() => null))?.close();
}

/**
 * Run `fn` inside one transaction and resolve once it has committed. If `fn` throws, the
 * transaction is aborted (nothing it wrote is kept) and the error is rethrown.
 */
export async function inTransaction<S extends DBSchema, N extends StoreNames<S>[], M extends IDBTransactionMode, T>(
  db: IDBPDatabase<S>,
  stores: N,
  mode: M,
  fn: (tx: IDBPTransaction<S, N, M>) => Promise<T>,
): Promise<T> {
  const tx = db.transaction(stores, mode);
  try {
    const result = await fn(tx);
    await tx.done;
    return result;
  } catch (e) {
    tx.done.catch(() => {});
    try {
      tx.abort();
    } catch {
      // Already committed or aborted.
    }
    throw e;
  }
}
