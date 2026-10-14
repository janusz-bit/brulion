/**
 * Types of the sync module (WebDAV/Nextcloud engine, scheduler, Syncthing helpers).
 * Kept here instead of src/core/types.ts because only the sync module and its UI use them.
 */
import type { VaultPath } from '../types';

/** A file or folder on the WebDAV server, relative to the client's base URL. */
export interface RemoteEntry {
  /** Vault path relative to the base URL (NFC, no leading/trailing slash). */
  path: VaultPath;
  kind: 'file' | 'dir';
  /** Normalised ETag (no quotes, no `W/`); '' when the server sent none. */
  etag: string;
  /** Last modification time in ms since epoch; 0 when unknown. */
  mtime: number;
  /** Size in bytes; 0 for folders or when unknown. */
  size: number;
}

/** Machine-readable WebDAV failure category (the UI translates it). */
export type WebDavErrorCode =
  | 'auth'
  | 'forbidden'
  | 'not-found'
  | 'not-allowed'
  | 'conflict'
  | 'precondition'
  | 'too-large'
  | 'locked'
  | 'insufficient-storage'
  | 'server'
  | 'http'
  | 'network'
  | 'cors'
  | 'parse';

/** Result of `WebDavClient.test()`. */
export interface WebDavTestResult {
  ok: boolean;
  /** Error category when `ok` is false. */
  error?: WebDavErrorCode;
  /** HTTP status (0 for network/CORS failures). */
  status?: number;
  /** Technical details (English) for logs / "details" UI. */
  message?: string;
}

/** Options for `WebDavClient.put`. */
export interface PutOptions {
  /** Only replace the file if its ETag still matches (optimistic locking). */
  ifMatch?: string;
  /** Only create the file if it does not exist yet (`If-None-Match: *`). */
  ifNoneMatch?: boolean;
  /** Modification time (ms) to store on the server (Nextcloud `X-OC-MTime`). */
  mtime?: number;
}

/**
 * Per-path bookkeeping persisted in the LocalStore under `sync:state:<path>`: what both sides
 * looked like after the last successful sync of that path.
 */
export interface SyncFileState {
  /** contentHash of the local file at the last sync. */
  localHash: string;
  /** Normalised remote ETag at the last sync ('' = unknown). */
  remoteEtag: string;
  /** Local size in bytes at the last sync. */
  size: number;
  /** Local mtime at the last sync (quick change check for binary files). */
  mtime?: number;
  /** When the path was last synced (ms). */
  syncedAt: number;
}

/** What the engine did with one path. */
export type SyncChangeAction = 'upload' | 'download' | 'merge' | 'conflict' | 'deleteLocal' | 'deleteRemote';

/** Summary of one `SyncEngine.sync()` run. */
export interface SyncReport {
  /** Files uploaded (new or changed locally, restored on the server). */
  uploaded: number;
  /** Files downloaded (new or changed remotely, restored locally). */
  downloaded: number;
  /** Local files moved to the trash because they were deleted on the server. */
  deletedLocal: number;
  /** Server files deleted because they were deleted locally. */
  deletedRemote: number;
  /** Files changed on both sides and merged automatically. */
  merged: number;
  /** Paths with a real conflict: local kept, remote saved as a conflict copy. */
  conflicts: VaultPath[];
  /** Conflict copies created during this run. */
  conflictCopies: VaultPath[];
  /** Per-file failures; the rest of the sync still ran. */
  errors: { path: VaultPath; message: string }[];
  /** Files skipped on purpose (too large to transfer). */
  skipped: VaultPath[];
  /** Every change made, in completion order (for a details view). */
  changes: { path: VaultPath; action: SyncChangeAction }[];
  startedAt: number;
  finishedAt: number;
}

/** Progress callback payload of `SyncEngine.sync()`. */
export interface SyncProgress {
  phase: 'connecting' | 'listing' | 'syncing' | 'done';
  /** Paths processed so far (phase 'syncing'). */
  done: number;
  /** Paths to check in total (phase 'syncing'); 0 before that. */
  total: number;
  /** The path just processed. */
  path?: VaultPath;
}

/** Scheduler state shown in the UI. */
export type SyncStatusState = 'idle' | 'syncing' | 'error' | 'offline' | 'disabled';

/** Observable status of a `SyncScheduler`. */
export interface SyncStatus {
  state: SyncStatusState;
  /** Finish time (ms) of the last successful sync run. */
  lastSync?: number;
  /** Message of the last failure (cleared by a successful run). */
  lastError?: string;
  /** Error code of the last failure when known (WebDAV/sync error codes). */
  lastErrorCode?: string;
  lastReport?: SyncReport;
  /** Progress of the running sync (state 'syncing'). */
  progress?: SyncProgress;
  /** When the next automatic sync is planned (ms), if any. */
  nextSync?: number;
}

/** A conflict copy found in the vault (Syncthing or Brulion). */
export interface ConflictCopy {
  /** The file the copy belongs to. */
  original: VaultPath;
  /** The conflict copy itself. */
  copy: VaultPath;
  /** Local time of the conflict "YYYY-MM-DDTHH:mm:ss". */
  time: string;
  /** Device that produced the copy (Syncthing short device id or Brulion device name). */
  device: string;
  source: 'syncthing' | 'brulion';
}

/** A Syncthing folder (from `/rest/config/folders`). */
export interface SyncthingFolder {
  id: string;
  label: string;
  path: string;
  paused?: boolean;
}

/** Status of a Syncthing folder (or of the daemon when no folder is selected). */
export interface SyncthingStatus {
  /** Syncthing folder state ('idle', 'scanning', 'syncing', 'error', ...) or 'offline'. */
  state: string;
  needBytes: number;
  globalBytes: number;
  /** Percentage 0-100 of the folder that is in sync. */
  completion: number;
  /** Number of folder errors (including pull errors). */
  errors: number;
  folderId?: string;
  /** Connected remote devices, when known. */
  connected?: number;
  /** Error message when `state === 'offline'`. */
  error?: string;
}
