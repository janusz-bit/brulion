/**
 * Storage layer: FileSystemAdapter implementations (memory, IndexedDB, File System Access),
 * device-local key/value storage, the Vault (caches, trash, external-change detection) and
 * the local version History.
 */
export { MemoryFs, type MemoryFsOptions, type ExternalChangeOptions } from './memoryFs';
export { IdbFs, IDB_FS_DB, type IdbFsOptions } from './idbFs';
export {
  FsAccessFs,
  diffSnapshots,
  type FsAccessFsOptions,
  type FsaDirectoryHandle,
  type FsaFileHandle,
  type FsaHandle,
  type FsaFile,
  type FsaWritable,
  type FsaPermissionState,
  type FsaPermissionDescriptor,
} from './fsAccessFs';
export {
  saveDirectoryHandle,
  loadDirectoryHandle,
  deleteDirectoryHandle,
  listDirectoryHandles,
  verifyPermission,
  HANDLES_DB,
} from './handles';
export { LocalStore, LOCAL_DB } from './localStore';
export { Vault, TRASH_DIR, trashStamp, type ChangeSource, type VaultChange, type VaultOptions, type TrashInfo } from './vault';
export { History, type HistoryEntry, type HistoryOptions } from './history';
export { contentHash, hashBytes, utf8Length, encodeUtf8, decodeUtf8 } from './hash';
export { CONFIG_PATH, parseVaultConfig, mergeVaultConfig, serializeVaultConfig, type ParsedVaultConfig } from './config';
export { FsError, isFsError, isNotFoundError, type FsErrorCode } from './errors';
export { isTextPath } from './textFiles';
export { KeyedLock, type LockSpec } from './locks';
