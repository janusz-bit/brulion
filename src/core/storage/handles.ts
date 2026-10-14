/**
 * Persistence of File System Access directory handles (IndexedDB `brulion-handles`), so a
 * folder picked once can be reopened after a reload. Handles are structured-cloneable; the
 * browser may still require a user gesture to re-grant permission (`verifyPermission`).
 */
import type { DBSchema, IDBPDatabase } from 'idb';
import type { FsaDirectoryHandle, FsaPermissionDescriptor, FsaPermissionState } from './fsAccessFs';
import { closeCachedDb, openCachedDb } from './idb';

export const HANDLES_DB = 'brulion-handles';
const STORE = 'handles';

interface HandlesSchema extends DBSchema {
  handles: { key: string; value: FileSystemDirectoryHandle };
}

function openHandlesDb(): Promise<IDBPDatabase<HandlesSchema>> {
  return openCachedDb<HandlesSchema>(HANDLES_DB, 1, (db) => {
    if (!db.objectStoreNames.contains(STORE)) db.createObjectStore(STORE);
  });
}

/** Store a directory handle under `id` (e.g. the vault's handleId). */
export async function saveDirectoryHandle(id: string, h: FileSystemDirectoryHandle): Promise<void> {
  const db = await openHandlesDb();
  await db.put(STORE, h, id);
}

/** Load a previously saved directory handle, or null. */
export async function loadDirectoryHandle(id: string): Promise<FileSystemDirectoryHandle | null> {
  const db = await openHandlesDb();
  return (await db.get(STORE, id)) ?? null;
}

/** Forget a saved directory handle. */
export async function deleteDirectoryHandle(id: string): Promise<void> {
  const db = await openHandlesDb();
  await db.delete(STORE, id);
}

/** Ids of all saved directory handles. */
export async function listDirectoryHandles(): Promise<string[]> {
  const db = await openHandlesDb();
  return (await db.getAllKeys(STORE)).map(String);
}

/** Close the handles database connection (tests). */
export async function closeHandlesDb(): Promise<void> {
  await closeCachedDb(HANDLES_DB);
}

interface PermissionCapable {
  queryPermission?(desc?: FsaPermissionDescriptor): Promise<FsaPermissionState>;
  requestPermission?(desc?: FsaPermissionDescriptor): Promise<FsaPermissionState>;
}

/**
 * Check (and optionally request) permission for a handle. Returns true when access is
 * granted. Handles without the permission API (older browsers, OPFS) count as granted.
 * `request = true` must be called from a user gesture in browsers.
 */
export async function verifyPermission(
  handle: FileSystemDirectoryHandle | FsaDirectoryHandle,
  mode: 'read' | 'readwrite' = 'readwrite',
  request = false,
): Promise<boolean> {
  // The permission methods exist on real handles but are missing from TS's DOM lib.
  const h = handle as unknown as PermissionCapable;
  if (typeof h.queryPermission !== 'function') return true;
  const desc: FsaPermissionDescriptor = { mode };
  try {
    if ((await h.queryPermission(desc)) === 'granted') return true;
    if (request && typeof h.requestPermission === 'function') return (await h.requestPermission(desc)) === 'granted';
  } catch (e) {
    console.warn('[storage] permission check failed', e);
  }
  return false;
}
