/**
 * Platform implementation for the Tauri shell (NixOS/Linux desktop and Android).
 *
 * Every `@tauri-apps/*` module is loaded lazily (see ./tauri-ipc), and this file itself is
 * only imported dynamically by `getPlatform()` when running inside Tauri.
 *
 * Platform differences:
 * - `pickVaultFolder()`: desktop shows the native folder dialog. **Android returns null**:
 *   the system picker yields `content://` URIs that the Rust file commands cannot use, so
 *   the UI asks for a path on shared storage (e.g. `/storage/emulated/0/Brulion`, a
 *   Syncthing folder) in a text field and builds `{ kind: 'tauri', path, name }` itself,
 *   after `storageAccess.request()` granted "All files access".
 * - `saveFile()`: the save dialog (desktop path or Android `content://` URI) and
 *   `write_export`. On Android, when the dialog is unavailable, the file is written to
 *   `Eksport/` inside the default vault folder ({@link TauriPlatform.exportToVaultFolder}).
 * - `pickFiles()`: desktop uses the native dialog and reads the files through `fs_list` /
 *   `fs_read_binary`; Android uses the WebView's `<input type=file>`.
 * - `desktop` (desktop only): close-to-tray, showing the window and the quit handshake
 *   (`brulion://quit-requested` -> flush -> `quit_app`), see ./tauri-desktop.
 */
import type { InvokeArgs } from '@tauri-apps/api/core';
import type {
  FileSystemAdapter,
  HttpTransport,
  PickedFile,
  Platform,
  PlatformKind,
  VaultLocation,
} from '@/core/types';
import type { MemoryFs } from '@/core/storage';
import { extname, joinPath, normalizePath, sanitizeFileName } from '@/core/paths';
import { bytesToBase64, toBytes } from './bytes';
import { PlatformError } from './errors';
import { createFetchTransport } from './http';
import { createTauriNotificationService, type AndroidNotificationService, type TauriDesktopNotificationService } from './notifications-tauri';
import { TauriDesktopShell } from './tauri-desktop';
import { TauriFs, toFsError } from './tauri-fs';
import { androidInvoke, loadDialogPlugin, loadHttpPlugin, loadOpenerPlugin, loadOsPlugin, tauriInvoke } from './tauri-ipc';
import { openBrowserVault } from './web';
import { pickFilesWithInput, type PickFilesOptions } from './web-files';
import { checkExternalUrl } from './url';

/** On Android, binary IPC bodies above this size are sent as base64 (see the IPC contract). */
export const ANDROID_BASE64_THRESHOLD = 16 * 1024;

/** Folder (inside the default vault) used for exports on Android when no save dialog exists. */
export const ANDROID_EXPORT_FOLDER = 'Eksport';

/** Name of the default vault location. */
export const DEFAULT_TAURI_VAULT_NAME = 'Brulion';

/** How many picked files are read concurrently. */
const READ_CONCURRENCY = 8;

const OS_NAMES: Record<string, string> = {
  linux: 'Linux',
  windows: 'Windows',
  macos: 'macOS',
  freebsd: 'FreeBSD',
  android: 'Android',
  ios: 'iOS',
};

export interface TauriPlatformOptions {
  /** Force Android / desktop behaviour (default: detected through plugin-os). */
  android?: boolean;
  /** OS name as reported by plugin-os `platform()` (default: detected). */
  os?: string;
}

/**
 * The OS Tauri runs on (`linux`, `android`, ...), from plugin-os; falls back to the user
 * agent when the plugin is unavailable.
 */
export async function detectTauriOs(): Promise<string> {
  try {
    const { platform } = await loadOsPlugin();
    return platform();
  } catch (e) {
    console.warn('[platform] plugin-os unavailable, guessing the OS from the user agent', e);
    return typeof navigator !== 'undefined' && /android/i.test(navigator.userAgent) ? 'android' : 'linux';
  }
}

/** True for absolute POSIX or Windows paths. */
export function isAbsolutePath(p: string): boolean {
  return p.startsWith('/') || /^[A-Za-z]:[\\/]/.test(p) || p.startsWith('\\\\');
}

/** Split an absolute native path into its parent directory and base name. */
export function splitNativePath(p: string): { dir: string; base: string } {
  const trimmed = p.length > 1 ? p.replace(/[\\/]+$/, '') : p;
  const i = Math.max(trimmed.lastIndexOf('/'), trimmed.lastIndexOf('\\'));
  if (i < 0) return { dir: '', base: trimmed };
  const dir = i === 0 ? trimmed.slice(0, 1) : /^[A-Za-z]:$/.test(trimmed.slice(0, i)) ? trimmed.slice(0, i + 1) : trimmed.slice(0, i);
  return { dir, base: trimmed.slice(i + 1) };
}

/** Dialog filters from `accept` entries (extensions only; MIME types are ignored on desktop). */
export function dialogFilters(accept: readonly string[] | undefined): { name: string; extensions: string[] }[] | undefined {
  const exts = (accept ?? [])
    .map((a) => a.trim())
    .filter((a) => a !== '' && !a.includes('/'))
    .map((a) => a.replace(/^\*?\./, '').toLowerCase())
    .filter((a, i, all) => a !== '' && all.indexOf(a) === i);
  return exts.length > 0 ? [{ name: exts.map((e) => `.${e}`).join(', '), extensions: exts }] : undefined;
}

/**
 * `accept` for the Android WebView file input. The Tauri (wry) file chooser turns `accept`
 * into an Android intent of MIME types: extensions unknown to Android's MimeTypeMap (`.enex`,
 * often `.md`) are dropped — an empty result crashes the chooser — and content providers
 * label `.md` files inconsistently. So filters that contain any extension are not passed at
 * all (every file can be picked; importers check the content); MIME-only filters are kept.
 */
export function androidAccept(accept: readonly string[] | undefined): string[] | undefined {
  const entries = (accept ?? []).map((a) => a.trim()).filter((a) => a !== '');
  if (entries.length === 0 || entries.some((a) => !a.includes('/'))) return undefined;
  return entries;
}

/**
 * Save-dialog filters: by extension; on Android a MIME type is accepted too (the system
 * picker filters by MIME type), on desktop a name without extension gets no filter.
 */
export function saveFilters(ext: string, mime: string, android: boolean): { filters?: { name: string; extensions: string[] }[] } {
  if (ext) return { filters: [{ name: ext.toUpperCase(), extensions: [ext] }] };
  if (android && mime.includes('/')) return { filters: [{ name: mime, extensions: [mime] }] };
  return {};
}

async function mapLimited<T, R>(items: readonly T[], limit: number, fn: (item: T) => Promise<R>): Promise<R[]> {
  const out: R[] = [];
  let next = 0;
  const worker = async (): Promise<void> => {
    while (next < items.length) {
      const i = next++;
      out[i] = await fn(items[i]!);
    }
  };
  await Promise.all(Array.from({ length: Math.min(limit, items.length) }, worker));
  return out;
}

/** Read one file outside any vault through `fs_read_binary` (root = its folder). */
async function readNativeFile(root: string, path: string): Promise<Uint8Array> {
  let raw: unknown;
  try {
    raw = await tauriInvoke<unknown>('fs_read_binary', { root, path });
  } catch (e) {
    throw toFsError(e, path);
  }
  const bytes = toBytes(raw);
  if (!bytes) throw new Error(`fs_read_binary returned an invalid answer for ${path}`);
  return bytes;
}

/**
 * Read picked files (absolute paths) into PickedFile[] named by their base names (NFC, like
 * the relative paths of picked directories; the file is read under its on-disk name).
 */
export async function readNativeFiles(paths: readonly string[]): Promise<PickedFile[]> {
  return mapLimited(paths, READ_CONCURRENCY, async (abs) => {
    const { dir, base } = splitNativePath(abs);
    return { path: base.normalize('NFC'), data: await readNativeFile(dir, base) };
  });
}

/**
 * Read picked directories recursively. Paths are relative to the picked directory; with
 * several directories they are prefixed with the directory's name.
 */
export async function readNativeDirectories(dirs: readonly string[]): Promise<PickedFile[]> {
  const out: PickedFile[] = [];
  for (const dir of dirs) {
    let listing: unknown;
    try {
      listing = await tauriInvoke<unknown>('fs_list', { root: dir });
    } catch (e) {
      throw toFsError(e, dir);
    }
    const files = (Array.isArray(listing) ? listing : [])
      .filter((s): s is { path: string; kind: string } => typeof s === 'object' && s !== null && typeof (s as { path?: unknown }).path === 'string')
      .filter((s) => s.kind === 'file')
      .map((s) => s.path);
    const prefix = dirs.length > 1 ? splitNativePath(dir).base : '';
    const read = await mapLimited(files, READ_CONCURRENCY, async (rel) => ({
      path: joinPath(prefix, normalizePath(rel)),
      data: await readNativeFile(dir, rel),
    }));
    out.push(...read);
  }
  return out;
}

/** HttpTransport over `@tauri-apps/plugin-http` (native requests: no CORS). */
export function createTauriHttpTransport(): HttpTransport {
  return async (req) => {
    const { fetch } = await loadHttpPlugin();
    return createFetchTransport((input, init) => fetch(input, init), { corsHint: false })(req);
  };
}

function grantedOf(v: unknown): boolean {
  return typeof v === 'object' && v !== null && (v as { granted?: unknown }).granted === true;
}

/** Tauri implementation of {@link Platform}. */
export class TauriPlatform implements Platform {
  readonly kind: PlatformKind;
  readonly name: string;
  readonly vaultKinds: VaultLocation['kind'][] = ['tauri'];
  readonly notifications: TauriDesktopNotificationService | AndroidNotificationService;
  readonly http: HttpTransport;
  readonly storageAccess?: { granted(): Promise<boolean>; request(): Promise<void> };
  /**
   * Desktop only: tray / window controls and the quit handshake (register the UI's flush of
   * pending writes with `desktop.onQuitRequested`). Undefined on Android.
   */
  readonly desktop?: TauriDesktopShell;
  /** Running on Android. */
  readonly android: boolean;
  private readonly memoryVaults = new Map<string, MemoryFs>();

  constructor(android: boolean, os: string = android ? 'android' : 'linux') {
    this.android = android;
    this.kind = android ? 'tauri-android' : 'tauri-desktop';
    this.name = android ? 'Android' : (OS_NAMES[os] ?? 'Komputer');
    this.notifications = createTauriNotificationService(android);
    this.http = createTauriHttpTransport();
    if (!android) this.desktop = new TauriDesktopShell();
    if (android) {
      this.storageAccess = {
        granted: async () => grantedOf(await androidInvoke('has_all_files_access')),
        request: async () => {
          await androidInvoke<null>('request_all_files_access');
        },
      };
    }
  }

  /** Desktop: native folder dialog. Android: always null (see module docs). */
  async pickVaultFolder(): Promise<VaultLocation | null> {
    if (this.android) return null;
    const { open } = await loadDialogPlugin();
    const selected: unknown = await open({ directory: true, multiple: false });
    const path = Array.isArray(selected) ? selected[0] : selected;
    if (typeof path !== 'string' || path === '') return null;
    return { kind: 'tauri', path, name: splitNativePath(path).base || path };
  }

  /** `~/Brulion` on desktop; on Android shared storage when allowed, else app storage. */
  async defaultVaultLocation(): Promise<VaultLocation> {
    const path = await tauriInvoke<string>('default_vault_dir');
    return { kind: 'tauri', path, name: DEFAULT_TAURI_VAULT_NAME };
  }

  /**
   * Open a vault. A `tauri` folder is created when missing and added to the asset-protocol
   * scope (images in notes). `idb` / `memory` vaults work too (the WebView has IndexedDB).
   */
  async openFileSystem(location: VaultLocation): Promise<FileSystemAdapter> {
    if (location.kind === 'tauri') {
      const root = (location.path ?? '').trim();
      if (!root || !isAbsolutePath(root)) {
        throw new PlatformError('invalid-location', `The vault folder must be an absolute path: "${root}"`);
      }
      const fs = this.vaultFs(root);
      await fs.mkdir('');
      tauriInvoke<null>('allow_vault_assets', { root }).catch((e: unknown) => console.warn('[platform] allow_vault_assets failed', e));
      return fs;
    }
    const browserFs = openBrowserVault(location, this.memoryVaults);
    if (browserFs) return browserFs;
    throw new PlatformError('unsupported', `Vault location "${location.kind}" is not available in the app`);
  }

  async openExternal(url: string): Promise<void> {
    const { openUrl } = await loadOpenerPlugin();
    await openUrl(checkExternalUrl(url));
  }

  /** Save dialog + `write_export`; cancelling the dialog does nothing. */
  async saveFile(name: string, data: Uint8Array, mime: string): Promise<void> {
    const fileName = sanitizeFileName(name);
    let target: string | null;
    try {
      const { save } = await loadDialogPlugin();
      const ext = extname(fileName).slice(1);
      target = await save({
        defaultPath: fileName,
        ...saveFilters(ext, mime, this.android),
      });
    } catch (e) {
      if (!this.android) throw e;
      console.warn('[platform] save dialog unavailable, exporting into the vault folder', e);
      await this.exportToVaultFolder(fileName, data);
      return;
    }
    if (!target) return;
    await tauriInvoke<null>('write_export', this.binaryBody(data), { headers: { 'x-path': encodeURIComponent(target) } });
  }

  /**
   * Write an export into `Eksport/` of the default vault folder (unique name). Returns the
   * absolute path written. Used on Android when no save dialog is available.
   */
  async exportToVaultFolder(name: string, data: Uint8Array): Promise<string> {
    const root = await tauriInvoke<string>('default_vault_dir');
    const fs = this.vaultFs(root);
    const path = await uniqueVaultPath(fs, joinPath(ANDROID_EXPORT_FOLDER, sanitizeFileName(name)));
    await fs.writeBinary(path, data);
    return `${root.replace(/[\\/]+$/, '')}/${path}`;
  }

  /**
   * Desktop: native dialog (files read via fs commands). Android: `<input type=file>` without
   * extension filters ({@link androidAccept}).
   */
  async pickFiles(opts: PickFilesOptions): Promise<PickedFile[]> {
    if (this.android) return pickFilesWithInput({ ...opts, accept: androidAccept(opts.accept) });
    const { open } = await loadDialogPlugin();
    const filters = opts.directory ? undefined : dialogFilters(opts.accept);
    const selected: unknown = await open({
      multiple: !!opts.multiple,
      directory: !!opts.directory,
      ...(filters ? { filters } : {}),
    });
    const paths = (Array.isArray(selected) ? selected : [selected]).filter((p): p is string => typeof p === 'string' && p !== '');
    if (paths.length === 0) return [];
    return opts.directory ? readNativeDirectories(paths) : readNativeFiles(paths);
  }

  private vaultFs(root: string): TauriFs {
    return new TauriFs(root, { label: root, base64Threshold: this.android ? ANDROID_BASE64_THRESHOLD : Number.POSITIVE_INFINITY });
  }

  private binaryBody(data: Uint8Array): InvokeArgs {
    return this.android && data.byteLength > ANDROID_BASE64_THRESHOLD ? { base64: bytesToBase64(data) } : data;
  }
}

/** First free path: "a.zip", "a 2.zip", "a 3.zip", ... */
async function uniqueVaultPath(fs: FileSystemAdapter, path: string): Promise<string> {
  if (!(await fs.stat(path))) return path;
  const ext = extname(path);
  const stem = ext ? path.slice(0, -ext.length) : path;
  for (let i = 2; i < 10_000; i++) {
    const candidate = `${stem} ${i}${ext}`;
    if (!(await fs.stat(candidate))) return candidate;
  }
  throw new Error(`No free file name for ${path}`);
}

/** Create the Tauri platform (detects Android unless told). */
export async function createTauriPlatform(opts: TauriPlatformOptions = {}): Promise<TauriPlatform> {
  const os = opts.os ?? (opts.android === undefined ? await detectTauriOs() : opts.android ? 'android' : 'linux');
  const android = opts.android ?? os === 'android';
  return new TauriPlatform(android, os);
}
