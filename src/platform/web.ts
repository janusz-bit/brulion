/**
 * Platform implementation for the browser (PWA): vaults in IndexedDB or in a local folder
 * through the File System Access API (Chromium), `fetch` for WebDAV, the Notification API,
 * downloads for exports and `<input type=file>` for imports.
 *
 * Nothing here touches browser globals at module load; everything is looked up when used
 * (and injectable through {@link WebPlatformDeps} for tests).
 */
import type {
  FileSystemAdapter,
  HttpTransport,
  NotificationService,
  PickedFile,
  Platform,
  PlatformKind,
  VaultLocation,
} from '@/core/types';
import {
  FsAccessFs,
  IdbFs,
  MemoryFs,
  loadDirectoryHandle as loadHandle,
  saveDirectoryHandle as saveHandle,
  verifyPermission as verifyHandlePermission,
} from '@/core/storage';
import { errorName, PlatformError } from './errors';
import { createBrowserHttpTransport } from './http';
import { WebNotificationService } from './notifications-web';
import { checkExternalUrl } from './url';
import { downloadFile, pickFilesWithInput, type PickFilesOptions } from './web-files';

/** Display name of the web platform. */
export const WEB_PLATFORM_NAME = 'Przeglądarka';

/** Location of the vault created on first run in the browser. */
export const DEFAULT_WEB_VAULT: VaultLocation = { kind: 'idb', id: 'default', name: 'Brulion' };

/** `showDirectoryPicker` options we pass (missing from TypeScript's DOM lib). */
interface DirectoryPickerOptions {
  id?: string;
  mode?: 'read' | 'readwrite';
  startIn?: string;
}

/** `window.showDirectoryPicker` (Chromium only). */
export type ShowDirectoryPicker = (options?: DirectoryPickerOptions) => Promise<FileSystemDirectoryHandle>;

/** Injectable dependencies of the web platform (defaults: real browser APIs / storage). */
export interface WebPlatformDeps {
  /** `window.showDirectoryPicker`, or undefined when the File System Access API is missing. */
  showDirectoryPicker?: ShowDirectoryPicker;
  saveDirectoryHandle: (id: string, h: FileSystemDirectoryHandle) => Promise<void>;
  loadDirectoryHandle: (id: string) => Promise<FileSystemDirectoryHandle | null>;
  verifyPermission: (h: FileSystemDirectoryHandle, mode: 'read' | 'readwrite', request: boolean) => Promise<boolean>;
  notifications: NotificationService;
  http: HttpTransport;
  /** Open a URL in a new tab. */
  openWindow: (url: string) => void;
  saveFile: (name: string, data: Uint8Array, mime: string) => void | Promise<void>;
  pickFiles: (opts: PickFilesOptions) => Promise<PickedFile[]>;
  /** Id generator for directory handles. */
  newId: () => string;
}

/** Random id ("fsa-<uuid>"); falls back to Math.random where `crypto.randomUUID` is missing (http:). */
export function newHandleId(): string {
  const c = globalThis.crypto as Crypto | undefined;
  if (c && typeof c.randomUUID === 'function') return `fsa-${c.randomUUID()}`;
  return `fsa-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 10)}`;
}

function browserShowDirectoryPicker(): ShowDirectoryPicker | undefined {
  if (typeof window === 'undefined' || !('showDirectoryPicker' in window)) return undefined;
  const w = window as Window & { showDirectoryPicker?: ShowDirectoryPicker };
  return typeof w.showDirectoryPicker === 'function' ? (options) => w.showDirectoryPicker!(options) : undefined;
}

/** Fill in the browser defaults for dependencies not given (only those are created). */
function resolveDeps(deps: Partial<WebPlatformDeps>): WebPlatformDeps {
  return {
    // An explicit `showDirectoryPicker: undefined` means "API missing".
    showDirectoryPicker: 'showDirectoryPicker' in deps ? deps.showDirectoryPicker : browserShowDirectoryPicker(),
    saveDirectoryHandle: deps.saveDirectoryHandle ?? saveHandle,
    loadDirectoryHandle: deps.loadDirectoryHandle ?? loadHandle,
    verifyPermission: deps.verifyPermission ?? ((h, mode, request) => verifyHandlePermission(h, mode, request)),
    notifications: deps.notifications ?? new WebNotificationService(),
    http: deps.http ?? createBrowserHttpTransport(),
    openWindow:
      deps.openWindow ??
      ((url) => {
        window.open(url, '_blank', 'noopener');
      }),
    saveFile: deps.saveFile ?? ((name, data, mime) => downloadFile(name, data, mime)),
    pickFiles: deps.pickFiles ?? ((opts) => pickFilesWithInput(opts)),
    newId: deps.newId ?? newHandleId,
  };
}

/**
 * Open IndexedDB and in-memory vaults (shared by the web and Tauri platforms: the Tauri
 * WebView has IndexedDB too). Memory vaults are kept per id in `memory` so reopening one
 * returns the same data. Returns null for other kinds.
 */
export function openBrowserVault(location: VaultLocation, memory: Map<string, MemoryFs>): FileSystemAdapter | null {
  switch (location.kind) {
    case 'idb':
      if (!location.id) throw new PlatformError('invalid-location', 'IndexedDB vault without an id');
      return new IdbFs(location.id, `${WEB_PLATFORM_NAME} (IndexedDB): ${location.name || location.id}`);
    case 'memory': {
      let fs = memory.get(location.id);
      if (!fs) {
        fs = new MemoryFs({ label: `${location.name || location.id} (pamięć)` });
        memory.set(location.id, fs);
      }
      return fs;
    }
    default:
      return null;
  }
}

/** Browser implementation of {@link Platform}. */
export class WebPlatform implements Platform {
  readonly kind: PlatformKind = 'web';
  readonly name = WEB_PLATFORM_NAME;
  readonly vaultKinds: VaultLocation['kind'][];
  readonly notifications: NotificationService;
  readonly http: HttpTransport;
  private readonly deps: WebPlatformDeps;
  private readonly memoryVaults = new Map<string, MemoryFs>();

  constructor(deps: Partial<WebPlatformDeps> = {}) {
    this.deps = resolveDeps(deps);
    this.notifications = this.deps.notifications;
    this.http = this.deps.http;
    this.vaultKinds = this.deps.showDirectoryPicker ? ['fsaccess', 'idb'] : ['idb'];
  }

  /**
   * Pick a folder with the File System Access API and remember its handle. Returns null
   * when cancelled. Throws `PlatformError('unsupported')` without the API. Must be called
   * from a user gesture.
   */
  async pickVaultFolder(): Promise<VaultLocation | null> {
    const pick = this.deps.showDirectoryPicker;
    if (!pick) throw new PlatformError('unsupported', 'This browser cannot open local folders (File System Access API missing)');
    let handle: FileSystemDirectoryHandle;
    try {
      handle = await pick({ id: 'brulion-vault', mode: 'readwrite' });
    } catch (e) {
      if (errorName(e) === 'AbortError') return null;
      throw e;
    }
    const handleId = this.deps.newId();
    await this.deps.saveDirectoryHandle(handleId, handle);
    return { kind: 'fsaccess', handleId, name: handle.name };
  }

  async defaultVaultLocation(): Promise<VaultLocation> {
    return { ...DEFAULT_WEB_VAULT };
  }

  /**
   * Open a vault. For `fsaccess` the saved handle is loaded and read/write permission is
   * requested — browsers only show that prompt during a user gesture, so on startup this
   * may throw `PlatformError('permission-denied')`; the UI should then offer a button that
   * calls `openFileSystem` again.
   */
  async openFileSystem(location: VaultLocation): Promise<FileSystemAdapter> {
    const browserFs = openBrowserVault(location, this.memoryVaults);
    if (browserFs) return browserFs;
    if (location.kind === 'fsaccess') return this.openFsAccess(location.handleId, location.name);
    throw new PlatformError('unsupported', `Vault location "${location.kind}" is not available in the browser`);
  }

  async openExternal(url: string): Promise<void> {
    this.deps.openWindow(checkExternalUrl(url));
  }

  async saveFile(name: string, data: Uint8Array, mime: string): Promise<void> {
    await this.deps.saveFile(name, data, mime);
  }

  pickFiles(opts: PickFilesOptions): Promise<PickedFile[]> {
    return this.deps.pickFiles(opts);
  }

  private async openFsAccess(handleId: string, name: string): Promise<FileSystemAdapter> {
    if (!handleId) throw new PlatformError('invalid-location', 'Folder vault without a handle id');
    const handle = await this.deps.loadDirectoryHandle(handleId);
    if (!handle) {
      throw new PlatformError('handle-missing', `The folder "${name}" is no longer known to this browser; pick it again`);
    }
    if (!(await this.deps.verifyPermission(handle, 'readwrite', true))) {
      throw new PlatformError('permission-denied', `No permission to read and write the folder "${name}"`);
    }
    return new FsAccessFs(handle, { label: name || handle.name });
  }
}

/** Create the browser platform. */
export function createWebPlatform(deps: Partial<WebPlatformDeps> = {}): WebPlatform {
  return new WebPlatform(deps);
}
