/**
 * WebDAV client (Nextcloud, ownCloud, any RFC 4918 server) on top of an injected
 * HttpTransport (fetch in the browser, plugin-http in Tauri). Paths are vault paths relative
 * to the base URL; every segment is percent-encoded.
 */
import { normalizePath } from '../paths';
import type { HttpRequest, HttpResponse, HttpTransport, VaultPath } from '../types';
import { classifyTransportError, codeForStatus, isAbortError, isWebDavError, WebDavError } from './errors';
import { runPool } from './pool';
import type { PutOptions, RemoteEntry, WebDavTestResult } from './types';
import { basicAuth, getHeader, hrefPathname, joinUrl, normalizeEtag, quoteEtag, relativeTo, trimTrailingSlashes, urlPathname } from './url';
import { PROPFIND_BODY, parseMultistatus, type DavResource } from './xml';

/** Constructor options of `WebDavClient`. */
export interface WebDavClientOptions {
  /** Folder URL all paths are relative to, e.g. from `nextcloudDavUrl()`. */
  baseUrl: string;
  username: string;
  password: string;
  transport: HttpTransport;
  /** Parallel PROPFIND requests while listing (default 4). */
  listConcurrency?: number;
  /**
   * Base time limit per request in ms (default 60 s; 0 = none). Transports have no timeout of
   * their own, and a stalled connection (e.g. a phone switching networks) would otherwise
   * hang the sync forever. Uploads and downloads get extra time for their size.
   */
  timeoutMs?: number;
  /** Slowest transfer rate still accepted, for the size-based extra time (default 16 KiB/s). */
  minBytesPerSecond?: number;
}

/** Options of `WebDavClient.get`. */
export interface GetOptions {
  /** Expected size in bytes (e.g. from the listing); extends the timeout for big files. */
  size?: number;
}

/** Options of `WebDavClient.list`. */
export interface ListOptions {
  /** Return true to not descend into a folder (it is still listed itself). */
  skipDir?: (path: VaultPath) => boolean;
  /** Stop listing further folders when aborted (rejects with the abort reason). */
  signal?: AbortSignal;
}

const textDecoder = new TextDecoder('utf-8');

/** Downloads of unknown size get this many times the base timeout. */
const UNKNOWN_SIZE_FACTOR = 10;

function describe(method: string, status: number, url: string): string {
  return `${method} ${url} failed: HTTP ${status}`;
}

/** WebDAV client with Basic auth. All methods throw `WebDavError` on failure. */
export class WebDavClient {
  readonly baseUrl: string;
  readonly username: string;
  private readonly authorization: string;
  private readonly transport: HttpTransport;
  private readonly listConcurrency: number;
  private readonly timeoutMs: number;
  private readonly minBytesPerSecond: number;
  private readonly basePathname: string;
  /** Folders known to exist on the server ('' = base). */
  private readonly knownDirs = new Set<string>();
  /** Normalised path -> name as stored on the server, when they differ (NFD names). */
  private readonly remoteNames = new Map<VaultPath, string>();

  constructor(o: WebDavClientOptions) {
    this.baseUrl = trimTrailingSlashes(o.baseUrl.trim());
    this.username = o.username;
    this.authorization = basicAuth(o.username, o.password);
    this.transport = o.transport;
    this.listConcurrency = o.listConcurrency ?? 4;
    this.timeoutMs = o.timeoutMs ?? 60_000;
    this.minBytesPerSecond = Math.max(1, o.minBytesPerSecond ?? 16 * 1024);
    this.basePathname = urlPathname(this.baseUrl);
  }

  /** Identifies the remote folder + account (the engine resets its state when it changes). */
  get identity(): string {
    return `${this.username}@${this.baseUrl}`;
  }

  /**
   * Check that the base folder is reachable with these credentials (PROPFIND Depth 0).
   * Never throws: failures are reported as `{ ok: false, error, status, message }`, e.g.
   * 'auth' (401), 'forbidden' (403), 'not-found' (404), 'network', 'cors'.
   */
  async test(): Promise<WebDavTestResult> {
    try {
      await this.propfind('', 0);
      return { ok: true };
    } catch (e) {
      if (isWebDavError(e)) return { ok: false, error: e.code, status: e.status, message: e.message };
      return { ok: false, error: 'network', status: 0, message: e instanceof Error ? e.message : String(e) };
    }
  }

  /**
   * Recursive listing of everything below the base folder (files and folders, not the base
   * itself). Uses one PROPFIND Depth 1 per folder (Nextcloud refuses Depth: infinity).
   * Throws WebDavError 'not-found' when the base folder does not exist.
   */
  async list(opts: ListOptions = {}): Promise<RemoteEntry[]> {
    const out: RemoteEntry[] = [];
    const seen = new Set<string>(['']);
    this.knownDirs.clear();
    this.remoteNames.clear();
    await runPool<{ raw: string; isBase: boolean }>(
      [{ raw: '', isBase: true }],
      async ({ raw, isBase }, push) => {
        opts.signal?.throwIfAborted();
        let resources: DavResource[];
        try {
          resources = await this.propfindRaw(raw, 1);
        } catch (e) {
          // A sub-folder deleted while we were listing: skip it.
          if (!isBase && isWebDavError(e, 'not-found')) return;
          throw e;
        }
        this.knownDirs.add(normalizePath(raw));
        const requestUrl = joinUrl(this.baseUrl, raw, true);
        for (const res of resources) {
          const rawRel = this.relativeRaw(res.href, requestUrl);
          if (rawRel === null) continue;
          const path = normalizePath(rawRel);
          if (path === '' || path === normalizePath(raw) || seen.has(path)) continue;
          seen.add(path);
          if (rawRel !== path) this.remoteNames.set(path, rawRel);
          out.push({ path, kind: res.isCollection ? 'dir' : 'file', etag: res.etag, mtime: res.mtime, size: res.isCollection ? 0 : res.size });
          if (res.isCollection) {
            this.knownDirs.add(path);
            if (!opts.skipDir?.(path)) push({ raw: rawRel, isBase: false });
          }
        }
      },
      { concurrency: this.listConcurrency, signal: opts.signal },
    );
    opts.signal?.throwIfAborted();
    return out.sort((a, b) => (a.path < b.path ? -1 : a.path > b.path ? 1 : 0));
  }

  /** Properties of one file or folder (PROPFIND Depth 0); null when it does not exist. */
  async stat(path: VaultPath): Promise<RemoteEntry | null> {
    const p = normalizePath(path);
    let resources: DavResource[];
    try {
      resources = await this.propfind(p, 0);
    } catch (e) {
      if (isWebDavError(e, 'not-found')) return null;
      throw e;
    }
    const res = resources[0];
    if (!res) return null;
    return { path: p, kind: res.isCollection ? 'dir' : 'file', etag: res.etag, mtime: res.mtime, size: res.isCollection ? 0 : res.size };
  }

  /** Download a file. Throws WebDavError 'not-found' when missing. */
  async get(path: VaultPath, opts: GetOptions = {}): Promise<{ data: Uint8Array; etag: string }> {
    const url = this.url(path);
    const timeout = opts.size !== undefined ? this.timeoutFor(opts.size) : this.timeoutMs * UNKNOWN_SIZE_FACTOR;
    const res = await this.request({ method: 'GET', url }, timeout);
    this.expect(res, 'GET', url, [200]);
    return { data: res.body, etag: normalizeEtag(getHeader(res.headers, 'etag') ?? getHeader(res.headers, 'oc-etag')) };
  }

  /**
   * Upload a file, creating missing parent folders. `ifMatch` / `ifNoneMatch` make the write
   * conditional (WebDavError 'precondition' on HTTP 412). Returns the new ETag when the
   * server reports one.
   */
  async put(path: VaultPath, data: Uint8Array, opts: PutOptions = {}): Promise<{ etag?: string }> {
    const p = normalizePath(path);
    if (p === '') throw new WebDavError('http', 0, 'Cannot PUT the base folder');
    await this.ensureParents(p, false);
    let res = await this.sendPut(p, data, opts);
    if (res.status === 409) {
      // Parent folder missing after all (deleted by someone else): create it and retry once.
      await this.ensureParents(p, true);
      res = await this.sendPut(p, data, opts);
    }
    this.expect(res, 'PUT', this.url(p), [200, 201, 204]);
    const etag = normalizeEtag(getHeader(res.headers, 'oc-etag') ?? getHeader(res.headers, 'etag'));
    return etag ? { etag } : {};
  }

  /** Delete a file or folder. Missing paths are fine. `ifMatch` guards against deleting a changed file. */
  async delete(path: VaultPath, opts: { ifMatch?: string } = {}): Promise<void> {
    const p = normalizePath(path);
    if (p === '') throw new WebDavError('http', 0, 'Refusing to DELETE the base folder');
    const url = this.url(p);
    const headers: Record<string, string> = {};
    if (opts.ifMatch) headers['If-Match'] = quoteEtag(opts.ifMatch);
    const res = await this.request({ method: 'DELETE', url, headers });
    if (res.status === 404) return;
    this.expect(res, 'DELETE', url, [200, 202, 204]);
    this.remoteNames.delete(p);
  }

  /** Create a folder ('' = the base folder). Succeeds when it already exists (HTTP 405). */
  async mkcol(path: VaultPath): Promise<void> {
    const p = normalizePath(path);
    const url = this.url(p, true);
    const res = await this.request({ method: 'MKCOL', url });
    if (res.status !== 405) this.expect(res, 'MKCOL', url, [200, 201]);
    this.knownDirs.add(p);
  }

  /**
   * Make sure the base folder exists, creating missing ancestors of the base URL as needed
   * (MKCOL answers 409 when the parent is missing; 405 means "already exists").
   */
  async ensureBase(): Promise<void> {
    await this.mkcolUrl(`${this.baseUrl}/`, 0);
    this.knownDirs.add('');
  }

  // -------------------------------------------------------------------------
  // Internals
  // -------------------------------------------------------------------------

  private url(path: VaultPath, dir = false): string {
    const p = normalizePath(path);
    return joinUrl(this.baseUrl, this.remoteNames.get(p) ?? p, dir);
  }

  /** Raw (un-normalised) relative path of an href, or null when outside the base folder. */
  private relativeRaw(href: string, requestUrl: string): string | null {
    return relativeTo(hrefPathname(href, requestUrl), this.basePathname);
  }

  /** Time limit for a request transferring `bytes` (0 = no limit). */
  private timeoutFor(bytes: number): number {
    if (!(this.timeoutMs > 0)) return 0;
    return this.timeoutMs + Math.ceil((Math.max(0, bytes) * 1000) / this.minBytesPerSecond);
  }

  private async request(req: HttpRequest, timeoutMs = this.timeoutMs): Promise<HttpResponse> {
    const headers: Record<string, string> = {
      Authorization: this.authorization,
      // Nextcloud then answers 401 without WWW-Authenticate (no browser login popup).
      'X-Requested-With': 'XMLHttpRequest',
      ...req.headers,
    };
    let timer: ReturnType<typeof setTimeout> | undefined;
    try {
      const sent = this.transport({ ...req, headers });
      if (!(timeoutMs > 0) || !Number.isFinite(timeoutMs)) return await sent;
      // The transport cannot be cancelled; a late answer is simply ignored (the next sync
      // compares ETags, so a PUT that still went through is recognised).
      const timedOut = new Promise<never>((_, reject) => {
        timer = setTimeout(() => {
          reject(new WebDavError('network', 0, `${req.method} ${req.url} timed out after ${Math.round(timeoutMs / 1000)} s`, req.url));
        }, timeoutMs);
      });
      return await Promise.race([sent, timedOut]);
    } catch (e) {
      if (isAbortError(e)) throw e;
      throw classifyTransportError(e, req.url);
    } finally {
      if (timer !== undefined) clearTimeout(timer);
    }
  }

  private expect(res: HttpResponse, method: string, url: string, ok: number[]): void {
    if (ok.includes(res.status)) return;
    throw new WebDavError(codeForStatus(res.status), res.status, describe(method, res.status, url), url);
  }

  private propfind(path: VaultPath, depth: 0 | 1): Promise<DavResource[]> {
    return this.propfindRaw(this.remoteNames.get(normalizePath(path)) ?? path, depth);
  }

  private async propfindRaw(rawPath: string, depth: 0 | 1): Promise<DavResource[]> {
    const url = joinUrl(this.baseUrl, rawPath, true);
    const res = await this.request({
      method: 'PROPFIND',
      url,
      headers: { Depth: String(depth), 'Content-Type': 'application/xml; charset=utf-8' },
      body: PROPFIND_BODY,
    });
    this.expect(res, 'PROPFIND', url, [200, 207]);
    return parseMultistatus(textDecoder.decode(res.body));
  }

  private sendPut(p: VaultPath, data: Uint8Array, opts: PutOptions): Promise<HttpResponse> {
    const headers: Record<string, string> = { 'Content-Type': 'application/octet-stream' };
    if (opts.ifMatch) headers['If-Match'] = quoteEtag(opts.ifMatch);
    if (opts.ifNoneMatch) headers['If-None-Match'] = '*';
    if (opts.mtime !== undefined && Number.isFinite(opts.mtime)) headers['X-OC-MTime'] = String(Math.floor(opts.mtime / 1000));
    return this.request({ method: 'PUT', url: this.url(p), headers, body: data }, this.timeoutFor(data.byteLength));
  }

  /** MKCOL every parent folder of `p` not known to exist (`force`: ignore what we know). */
  private async ensureParents(p: VaultPath, force: boolean): Promise<void> {
    const segments = p.split('/').slice(0, -1);
    let dir = '';
    for (const seg of segments) {
      dir = dir ? `${dir}/${seg}` : seg;
      if (!force && this.knownDirs.has(dir)) continue;
      const url = this.url(dir, true);
      const res = await this.request({ method: 'MKCOL', url });
      if (res.status === 409 && !force) {
        // A folder above is missing although we thought it existed: start over from the top.
        return this.ensureParents(p, true);
      }
      // 405: already exists (fine).
      if (res.status !== 405) this.expect(res, 'MKCOL', url, [200, 201]);
      this.knownDirs.add(dir);
    }
  }

  /** MKCOL `url`; on 409 create its parent first (bounded by the URL depth). */
  private async mkcolUrl(url: string, depth: number): Promise<void> {
    const res = await this.request({ method: 'MKCOL', url });
    if (res.status === 405 || res.status === 200 || res.status === 201) return;
    if (res.status === 409 && depth < 32) {
      const parent = parentUrl(url);
      if (parent !== null) {
        await this.mkcolUrl(parent, depth + 1);
        const retry = await this.request({ method: 'MKCOL', url });
        if (retry.status === 405) return;
        this.expect(retry, 'MKCOL', url, [200, 201]);
        return;
      }
    }
    this.expect(res, 'MKCOL', url, [200, 201]);
  }
}

/** "https://h/a/b/" -> "https://h/a/"; null at the root. */
function parentUrl(url: string): string | null {
  const m = /^([a-z][a-z0-9+.-]*:\/\/[^/]+)(\/.*)$/i.exec(url);
  const origin = m ? m[1]! : '';
  const path = trimTrailingSlashes(m ? m[2]! : url);
  const i = path.lastIndexOf('/');
  if (i <= 0) return null;
  return `${origin}${path.slice(0, i)}/`;
}
