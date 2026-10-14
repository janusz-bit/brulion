/**
 * Syncthing helpers: a small REST client for the local Syncthing daemon (status shown in the
 * app, finding the folder that contains the vault) and a recommended `.stignore`.
 */
import type { HttpResponse, HttpTransport } from '../types';
import { errorMessage } from './errors';
import type { SyncthingFolder, SyncthingStatus } from './types';
import { trimTrailingSlashes } from './url';

/** Default address of the Syncthing GUI / REST API. */
export const SYNCTHING_DEFAULT_URL = 'http://127.0.0.1:8384';

/** Constructor options of `SyncthingClient`. */
export interface SyncthingClientOptions {
  /** GUI/REST address (default http://127.0.0.1:8384). */
  url?: string;
  /** API key from Syncthing's settings (GUI -> Actions -> Settings). */
  apiKey: string;
  transport: HttpTransport;
  /** Folder used by `status()` when called without an id. */
  folderId?: string;
  /** Home directory used to expand "~" in folder paths (guessed from the path otherwise). */
  home?: string;
}

/** A failed Syncthing REST call. `status` 0 = not reachable. */
export class SyncthingError extends Error {
  override name = 'SyncthingError';
  readonly status: number;

  constructor(status: number, message: string) {
    super(message);
    this.status = status;
  }
}

const decoder = new TextDecoder('utf-8');

function num(v: unknown): number {
  return typeof v === 'number' && Number.isFinite(v) ? v : 0;
}

function str(v: unknown): string {
  return typeof v === 'string' ? v : '';
}

/** Percentage (0-100, one decimal) of a folder that is in sync. */
export function completionPercent(needBytes: number, globalBytes: number): number {
  if (globalBytes <= 0) return needBytes > 0 ? 0 : 100;
  const pct = (100 * (globalBytes - Math.min(needBytes, globalBytes))) / globalBytes;
  return Math.max(0, Math.min(100, Math.floor(pct * 10) / 10));
}

/**
 * Normalise a local folder path for comparison: backslashes -> '/', duplicate and trailing
 * slashes removed, "~" expanded with `home` when given. Windows drive letters are lower-cased.
 */
export function normalizeFsPath(p: string, home?: string): string {
  let s = p.trim().replace(/\\/g, '/');
  if (home && (s === '~' || s.startsWith('~/'))) s = trimTrailingSlashes(home.replace(/\\/g, '/')) + s.slice(1);
  s = s.replace(/\/{2,}/g, '/');
  if (s.length > 1) s = trimTrailingSlashes(s);
  if (/^[a-z]:/i.test(s)) s = s[0]!.toLowerCase() + s.slice(1);
  return s.normalize('NFC');
}

/** Guess the home directory contained in an absolute path ("/home/jan/..." -> "/home/jan"). */
export function guessHome(absPath: string): string | undefined {
  const s = absPath.replace(/\\/g, '/');
  const m = /^(\/home\/[^/]+|\/Users\/[^/]+|\/root|\/data\/data\/[^/]+|[a-z]:\/Users\/[^/]+)(?:\/|$)/i.exec(s);
  return m ? m[1] : undefined;
}

function isWithinFsPath(path: string, dir: string, caseInsensitive: boolean): boolean {
  const p = caseInsensitive ? path.toLowerCase() : path;
  const d = caseInsensitive ? dir.toLowerCase() : dir;
  return d === '/' ? p.startsWith('/') : p === d || p.startsWith(`${d}/`);
}

/**
 * The Syncthing folder containing `absPath` (longest matching folder path), or null.
 * Folder paths may use "~" (expanded with `home`, or the home guessed from `absPath`).
 */
export function findFolderForPath<F extends Pick<SyncthingFolder, 'path'>>(folders: readonly F[], absPath: string, home?: string): F | null {
  const h = home ?? guessHome(absPath);
  const target = normalizeFsPath(absPath, h);
  const windows = /^[a-z]:/i.test(target);
  let best: F | null = null;
  let bestLen = -1;
  for (const f of folders) {
    if (!f.path) continue;
    const dir = normalizeFsPath(f.path, h);
    if (dir.startsWith('~')) continue;
    if (isWithinFsPath(target, dir, windows) && dir.length > bestLen) {
      best = f;
      bestLen = dir.length;
    }
  }
  return best;
}

/** Client for the Syncthing REST API (header `X-API-Key`). */
export class SyncthingClient {
  readonly url: string;
  private readonly apiKey: string;
  private readonly transport: HttpTransport;
  private readonly folderId: string | undefined;
  private readonly home: string | undefined;

  constructor(o: SyncthingClientOptions) {
    this.url = trimTrailingSlashes((o.url ?? SYNCTHING_DEFAULT_URL).trim() || SYNCTHING_DEFAULT_URL);
    this.apiKey = o.apiKey;
    this.transport = o.transport;
    this.folderId = o.folderId;
    this.home = o.home;
  }

  /** True when the daemon answers `/rest/system/ping` with this API key. */
  async ping(): Promise<boolean> {
    try {
      const data = await this.getJson('/rest/system/ping');
      return typeof data === 'object' && data !== null && (data as { ping?: unknown }).ping === 'pong';
    } catch {
      return false;
    }
  }

  /** Configured folders. Throws SyncthingError when the daemon is unreachable / the key is wrong. */
  async folders(): Promise<SyncthingFolder[]> {
    const data = await this.getJson('/rest/config/folders');
    if (!Array.isArray(data)) throw new SyncthingError(200, 'Unexpected /rest/config/folders response');
    const out: SyncthingFolder[] = [];
    for (const f of data as unknown[]) {
      if (typeof f !== 'object' || f === null) continue;
      const o = f as Record<string, unknown>;
      const id = str(o.id);
      if (!id) continue;
      out.push({ id, label: str(o.label) || id, path: str(o.path), ...(typeof o.paused === 'boolean' ? { paused: o.paused } : {}) });
    }
    return out;
  }

  /** Number of connected remote devices. Throws SyncthingError when unreachable. */
  async connections(): Promise<number> {
    const data = await this.getJson('/rest/system/connections');
    const conns = typeof data === 'object' && data !== null ? (data as { connections?: unknown }).connections : undefined;
    if (typeof conns !== 'object' || conns === null) return 0;
    let n = 0;
    for (const c of Object.values(conns as Record<string, unknown>)) {
      if (typeof c === 'object' && c !== null && (c as { connected?: unknown }).connected === true) n++;
    }
    return n;
  }

  /**
   * Status of a folder (`folderId`, else the one given to the constructor). Without a
   * folder: whether the daemon is reachable plus the connection count. Never throws:
   * failures give `state: 'offline'` with `error`.
   */
  async status(folderId?: string): Promise<SyncthingStatus> {
    const id = folderId ?? this.folderId;
    const connected = this.connections().catch(() => undefined);
    if (!id) {
      const ok = await this.ping();
      const n = await connected;
      return { state: ok ? 'idle' : 'offline', needBytes: 0, globalBytes: 0, completion: ok ? 100 : 0, errors: 0, ...(n !== undefined ? { connected: n } : {}), ...(ok ? {} : { error: 'unreachable' }) };
    }
    try {
      const data = await this.getJson(`/rest/db/status?folder=${encodeURIComponent(id)}`);
      const o = typeof data === 'object' && data !== null ? (data as Record<string, unknown>) : {};
      const needBytes = num(o.needBytes);
      const globalBytes = num(o.globalBytes);
      const n = await connected;
      return {
        state: str(o.state) || 'unknown',
        needBytes,
        globalBytes,
        completion: completionPercent(needBytes, globalBytes),
        errors: num(o.errors) + num(o.pullErrors),
        folderId: id,
        ...(n !== undefined ? { connected: n } : {}),
      };
    } catch (e) {
      await connected;
      return { state: 'offline', needBytes: 0, globalBytes: 0, completion: 0, errors: 0, folderId: id, error: errorMessage(e) };
    }
  }

  /** The configured folder that contains `absPath` (e.g. the vault folder), or null. */
  async findFolderForPath(absPath: string): Promise<SyncthingFolder | null> {
    return findFolderForPath(await this.folders(), absPath, this.home);
  }

  private async getJson(path: string): Promise<unknown> {
    const url = `${this.url}${path}`;
    let res: HttpResponse;
    try {
      res = await this.transport({ method: 'GET', url, headers: { 'X-API-Key': this.apiKey, Accept: 'application/json' } });
    } catch (e) {
      throw new SyncthingError(0, `Syncthing is not reachable at ${this.url}: ${errorMessage(e)}`);
    }
    if (res.status < 200 || res.status >= 300) {
      const hint = res.status === 401 || res.status === 403 ? ' (wrong API key?)' : '';
      throw new SyncthingError(res.status, `GET ${path} failed: HTTP ${res.status}${hint}`);
    }
    try {
      return JSON.parse(decoder.decode(res.body)) as unknown;
    } catch {
      throw new SyncthingError(res.status, `GET ${path}: invalid JSON`);
    }
  }
}

/**
 * Recommended `.stignore` for a Brulion vault shared with Syncthing (comments in Polish).
 * Syncthing does not sync `.stignore` itself: put it on every device.
 */
export const STIGNORE_TEMPLATE = `// Brulion — zalecany plik .stignore dla folderu z notatkami.
// Syncthing nie synchronizuje samego pliku .stignore: skopiuj go na każde urządzenie.
// Prefiks (?d) pozwala Syncthingowi usunąć taki plik, gdy blokuje usunięcie folderu.

// Kosz Brulionu — usunięte notatki zostają lokalnie na każdym urządzeniu.
/.trash

// Pliki systemowe macOS i Windows.
(?d).DS_Store
(?d)Thumbs.db

// Układ okien Obsidiana — każde urządzenie ma własny.
/.obsidian/workspace*.json

// Kopie zapasowe i kosz Logseqa.
/logseq/bak
/logseq/.recycle

// Pliki tymczasowe (także zapisy w toku Brulionu).
*.tmp
(?d).brulion-tmp-*
`;
