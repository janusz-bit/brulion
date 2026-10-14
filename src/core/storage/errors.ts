/**
 * File-system errors shared by all storage adapters. Codes mirror POSIX so that callers can
 * handle errors from the in-memory, IndexedDB, File System Access and Tauri backends alike.
 */
import { normalizePath } from '../paths';
import type { VaultPath } from '../types';

export type FsErrorCode = 'ENOENT' | 'EEXIST' | 'EISDIR' | 'ENOTDIR' | 'EINVAL' | 'ENOTEMPTY';

const DEFAULT_MESSAGES: Record<FsErrorCode, string> = {
  ENOENT: 'No such file or directory',
  EEXIST: 'File or directory already exists',
  EISDIR: 'Is a directory',
  ENOTDIR: 'Not a directory',
  EINVAL: 'Invalid path or argument',
  ENOTEMPTY: 'Directory not empty',
};

/** Error thrown by Brulion storage adapters and the Vault. */
export class FsError extends Error {
  override name = 'FsError';
  readonly code: FsErrorCode;
  readonly path: string;

  constructor(code: FsErrorCode, path: string, message?: string) {
    super(`${code}: ${message ?? DEFAULT_MESSAGES[code]}: ${path}`);
    this.code = code;
    this.path = path;
  }
}

/** True when `e` is an FsError with the given code. */
export function isFsError(e: unknown, code?: FsErrorCode): e is FsError {
  return e instanceof FsError && (code === undefined || e.code === code);
}

/**
 * True for "file not found" errors from any backend: FsError ENOENT, DOMException
 * `NotFoundError` (File System Access API) and native error strings (Tauri / Rust io errors).
 */
export function isNotFoundError(e: unknown): boolean {
  if (e instanceof FsError) return e.code === 'ENOENT';
  if (typeof e === 'object' && e !== null && (e as { name?: unknown }).name === 'NotFoundError') return true;
  const msg = typeof e === 'string' ? e : e instanceof Error ? e.message : '';
  return /ENOENT|not found|no such file|cannot find|os error 2\b/i.test(msg);
}

/**
 * Normalise a path given to an adapter operation that needs a real entry (not the root).
 * Throws EINVAL for the empty path / root.
 */
export function requireEntryPath(path: string): VaultPath {
  const p = normalizePath(path);
  if (p === '') throw new FsError('EINVAL', path, 'Path must not be empty');
  return p;
}
