/**
 * `@/core/sync` — Nextcloud/WebDAV sync (client, engine with three-way merge, scheduler) and
 * Syncthing helpers (REST client, conflict copies, `.stignore` template).
 * See docs/ARCHITECTURE.md.
 */
export { WebDavClient, type WebDavClientOptions, type ListOptions, type GetOptions } from './webdav';
export {
  nextcloudDavUrl,
  encodePath,
  joinUrl,
  normalizeEtag,
  quoteEtag,
  basicAuth,
  base64Encode,
  getHeader,
  hrefPathname,
  relativeTo,
  urlPathname,
} from './url';
export { parseMultistatus, parseStatusLine, PROPFIND_BODY, type DavResource } from './xml';
export { merge3, mergeJson3, unionLines, splitText, joinLines, type MergeHunk, type MergeResult, type SplitText } from './merge';
export { diffLines, diffComm, diff3Regions, DIFF_BUDGET, type DiffHunk, type Diff3Region, type CommPart } from './diff';
export {
  SyncEngine,
  planSync,
  isSyncTextPath,
  isSyncablePath,
  remoteVersion,
  decodeSyncText,
  groupByCase,
  SYNC_STATE_PREFIX,
  SYNC_BASE_PREFIX,
  SYNC_META_KEY,
  type SyncEngineOptions,
  type SyncRunOptions,
  type SyncRemote,
  type SyncFacts,
  type SyncAction,
} from './engine';
export { SyncScheduler, type SyncSchedulerOptions, type SchedulerTimers, type SchedulerEventTarget, type SchedulableEngine } from './scheduler';
export {
  findSyncthingConflicts,
  findConflicts,
  parseBrulionConflict,
  conflictCopyPath,
  conflictStamp,
  sanitizeDeviceName,
} from './conflicts';
export {
  SyncthingClient,
  SyncthingError,
  STIGNORE_TEMPLATE,
  SYNCTHING_DEFAULT_URL,
  completionPercent,
  findFolderForPath,
  normalizeFsPath,
  guessHome,
  type SyncthingClientOptions,
} from './syncthing';
export {
  WebDavError,
  SyncError,
  isWebDavError,
  isSyncError,
  isAbortError,
  codeForStatus,
  classifyTransportError,
  type SyncErrorCode,
} from './errors';
export { runPool, type PoolOptions } from './pool';
export type {
  RemoteEntry,
  WebDavErrorCode,
  WebDavTestResult,
  PutOptions,
  SyncFileState,
  SyncChangeAction,
  SyncReport,
  SyncProgress,
  SyncStatus,
  SyncStatusState,
  ConflictCopy,
  SyncthingFolder,
  SyncthingStatus,
} from './types';
