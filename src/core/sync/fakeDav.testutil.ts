/**
 * In-memory WebDAV server (Nextcloud flavour) implementing HttpTransport, for tests.
 * Supports PROPFIND (Depth 0/1; "infinity" is refused like Nextcloud does), GET, PUT
 * (If-Match / If-None-Match, 409 when the parent is missing), DELETE (If-Match), MKCOL
 * (405 if it exists, 409 if the parent is missing) and Basic auth. Hooks allow injecting
 * failures and concurrent changes.
 */
import type { HttpRequest, HttpResponse, HttpTransport } from '../types';
import { base64Encode } from './url';

type Node = { kind: 'dir'; mtime: number; etag: string } | { kind: 'file'; data: Uint8Array; mtime: number; etag: string };

export interface FakeDavOptions {
  /** DAV root path of the user (default "/remote.php/dav/files/jan"). */
  root?: string;
  username?: string;
  password?: string;
  origin?: string;
  /** Namespace prefix used in responses: 'd' (Nextcloud), 'D' (Apache) or '' (default ns). */
  prefix?: string;
  /** Hrefs as absolute paths (default) or full URLs. */
  hrefStyle?: 'path' | 'url';
  /** Send ETag headers on PUT responses (default true). */
  putEtag?: boolean;
  /** Pretend the server has no ETags at all (no getetag, no ETag headers). Default false. */
  noEtags?: boolean;
  /** Clock (ms). */
  now?: () => number;
}

export interface LoggedRequest {
  method: string;
  path: string;
  headers: Record<string, string>;
}

/** A hook may return a response (or throw) to replace the normal handling. */
export type FakeDavHook = (req: HttpRequest, path: string) => HttpResponse | undefined | void;

const enc = new TextEncoder();
const dec = new TextDecoder();

function decodePath(pathname: string): string {
  return pathname
    .split('/')
    .map((s) => {
      try {
        return decodeURIComponent(s);
      } catch {
        return s;
      }
    })
    .join('/');
}

function encodeHref(path: string): string {
  return path
    .split('/')
    .map((s) => (s === '' ? '' : encodeURIComponent(s)))
    .join('/');
}

function header(req: HttpRequest, name: string): string | undefined {
  const l = name.toLowerCase();
  for (const [k, v] of Object.entries(req.headers ?? {})) if (k.toLowerCase() === l) return v;
  return undefined;
}

function unquote(etag: string): string {
  return etag.trim().replace(/^W\//, '').replace(/^"(.*)"$/, '$1');
}

function res(status: number, body: string | Uint8Array = '', headers: Record<string, string> = {}): HttpResponse {
  return { status, headers, body: typeof body === 'string' ? enc.encode(body) : body };
}

function escapeXml(s: string): string {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

export class FakeDavServer {
  readonly root: string;
  readonly origin: string;
  readonly username: string;
  readonly password: string;
  readonly nodes = new Map<string, Node>();
  readonly log: LoggedRequest[] = [];
  readonly hooks: FakeDavHook[] = [];
  private readonly prefix: string;
  private readonly hrefStyle: 'path' | 'url';
  private readonly putEtag: boolean;
  private readonly noEtags: boolean;
  private readonly now: () => number;
  private etagCounter = 0;

  constructor(o: FakeDavOptions = {}) {
    this.root = o.root ?? '/remote.php/dav/files/jan';
    this.origin = o.origin ?? 'https://chmura.example.pl';
    this.username = o.username ?? 'jan';
    this.password = o.password ?? 'hasło-źdźbło';
    this.prefix = o.prefix ?? 'd';
    this.hrefStyle = o.hrefStyle ?? 'path';
    this.putEtag = o.putEtag ?? true;
    this.noEtags = o.noEtags ?? false;
    this.now = o.now ?? (() => Date.UTC(2026, 9, 14, 8, 0, 0));
    // The user's root and its ancestors exist.
    let p = '';
    for (const seg of this.root.split('/').filter(Boolean)) {
      p += `/${seg}`;
      this.nodes.set(p, { kind: 'dir', mtime: this.now(), etag: this.nextEtag() });
    }
  }

  /** URL of a folder below the user root (for WebDavClient.baseUrl). */
  url(folder = ''): string {
    return `${this.origin}${this.root}${folder ? `/${encodeHref(folder)}` : ''}`;
  }

  /** The HttpTransport serving this server. */
  readonly transport: HttpTransport = async (req) => this.handle(req);

  /** Absolute decoded server path of a path relative to the user root. */
  abs(rel: string): string {
    return rel ? `${this.root}/${rel}` : this.root;
  }

  /** Create a file directly on the server (as another client would), parents included. */
  putFile(rel: string, content: string | Uint8Array): void {
    const p = this.abs(rel);
    this.mkdirs(p.slice(0, p.lastIndexOf('/')));
    this.nodes.set(p, { kind: 'file', data: typeof content === 'string' ? enc.encode(content) : content, mtime: this.now(), etag: this.nextEtag() });
  }

  /** Remove a file or folder directly on the server. */
  remove(rel: string): void {
    const p = this.abs(rel);
    // Deleting the current entry while iterating a Map is safe.
    for (const k of this.nodes.keys()) if (k === p || k.startsWith(`${p}/`)) this.nodes.delete(k);
  }

  /** Text of a file below the user root, undefined if missing. */
  text(rel: string): string | undefined {
    const n = this.nodes.get(this.abs(rel));
    return n?.kind === 'file' ? dec.decode(n.data) : undefined;
  }

  bytes(rel: string): Uint8Array | undefined {
    const n = this.nodes.get(this.abs(rel));
    return n?.kind === 'file' ? n.data : undefined;
  }

  etag(rel: string): string | undefined {
    return this.nodes.get(this.abs(rel))?.etag;
  }

  /** Change a file's ETag without changing its content (Nextcloud does that sometimes). */
  touchEtag(rel: string): void {
    const n = this.nodes.get(this.abs(rel));
    if (n) n.etag = this.nextEtag();
  }

  /** Files below `folder` (relative to the user root), path -> text. */
  files(folder = ''): Record<string, string> {
    const base = this.abs(folder);
    const out: Record<string, string> = {};
    for (const [k, n] of [...this.nodes.entries()].sort(([a], [b]) => (a < b ? -1 : 1))) {
      if (n.kind === 'file' && k.startsWith(`${base}/`)) out[k.slice(base.length + 1)] = dec.decode(n.data);
    }
    return out;
  }

  requests(method?: string): LoggedRequest[] {
    return method ? this.log.filter((r) => r.method === method) : this.log;
  }

  private nextEtag(): string {
    this.etagCounter++;
    return `e${this.etagCounter.toString(16).padStart(6, '0')}`;
  }

  private mkdirs(p: string): void {
    let cur = '';
    for (const seg of p.split('/').filter(Boolean)) {
      cur += `/${seg}`;
      if (!this.nodes.has(cur)) this.nodes.set(cur, { kind: 'dir', mtime: this.now(), etag: this.nextEtag() });
    }
  }

  private parent(p: string): string {
    return p.slice(0, p.lastIndexOf('/'));
  }

  private handle(req: HttpRequest): HttpResponse {
    const url = new URL(req.url);
    const path = decodePath(url.pathname).replace(/\/+$/, '') || '/';
    this.log.push({ method: req.method, path, headers: { ...req.headers } });
    for (const hook of this.hooks) {
      const r = hook(req, path);
      if (r) return r;
    }
    const expected = `Basic ${base64Encode(enc.encode(`${this.username}:${this.password}`))}`;
    if (header(req, 'authorization') !== expected) return res(401, 'Unauthorized');
    if (!path.startsWith(this.root)) return res(404);
    switch (req.method) {
      case 'PROPFIND':
        return this.propfind(req, path);
      case 'GET':
        return this.get(path);
      case 'PUT':
        return this.put(req, path);
      case 'DELETE':
        return this.del(req, path);
      case 'MKCOL':
        return this.mkcol(path);
      default:
        return res(405);
    }
  }

  private propfind(req: HttpRequest, path: string): HttpResponse {
    const depth = header(req, 'depth') ?? 'infinity';
    if (depth === 'infinity') return res(403, 'Depth infinity not allowed');
    const node = this.nodes.get(path);
    if (!node) return res(404);
    const items: [string, Node][] = [[path, node]];
    if (depth === '1' && node.kind === 'dir') {
      for (const [k, n] of this.nodes) if (this.parent(k) === path) items.push([k, n]);
    }
    const p = this.prefix ? `${this.prefix}:` : '';
    const ns = this.prefix ? `xmlns:${this.prefix}="DAV:"` : 'xmlns="DAV:"';
    const body = items
      .map(([k, n]) => {
        const hrefPath = encodeHref(k) + (n.kind === 'dir' ? '/' : '');
        const href = this.hrefStyle === 'url' ? `${this.origin}${hrefPath}` : hrefPath;
        const modified = new Date(n.mtime).toUTCString();
        const etag = this.noEtags ? '' : `<${p}getetag>&quot;${n.etag}&quot;</${p}getetag>`;
        const found =
          n.kind === 'dir'
            ? `<${p}getlastmodified>${modified}</${p}getlastmodified><${p}resourcetype><${p}collection/></${p}resourcetype>${etag}`
            : `<${p}getlastmodified>${modified}</${p}getlastmodified><${p}getcontentlength>${n.data.byteLength}</${p}getcontentlength><${p}resourcetype/>${etag}`;
        const missing = n.kind === 'dir' ? `<${p}propstat><${p}prop><${p}getcontentlength/></${p}prop><${p}status>HTTP/1.1 404 Not Found</${p}status></${p}propstat>` : '';
        return `<${p}response><${p}href>${escapeXml(href)}</${p}href><${p}propstat><${p}prop>${found}</${p}prop><${p}status>HTTP/1.1 200 OK</${p}status></${p}propstat>${missing}</${p}response>`;
      })
      .join('');
    const xml = `<?xml version="1.0"?>\n<${p}multistatus ${ns} xmlns:s="http://sabredav.org/ns" xmlns:oc="http://owncloud.org/ns">${body}</${p}multistatus>`;
    return res(207, xml, { 'Content-Type': 'application/xml; charset=utf-8' });
  }

  private get(path: string): HttpResponse {
    const node = this.nodes.get(path);
    if (!node) return res(404);
    if (node.kind === 'dir') return res(405);
    const headers: Record<string, string> = { 'Content-Length': String(node.data.byteLength) };
    if (!this.noEtags) headers.ETag = `"${node.etag}"`;
    return res(200, node.data.slice(), headers);
  }

  private put(req: HttpRequest, path: string): HttpResponse {
    const node = this.nodes.get(path);
    const parent = this.nodes.get(this.parent(path));
    if (!parent || parent.kind !== 'dir') return res(409, 'Parent missing');
    if (node?.kind === 'dir') return res(405);
    const ifMatch = header(req, 'if-match');
    if (ifMatch !== undefined && (!node || unquote(ifMatch) !== node.etag)) return res(412);
    const ifNoneMatch = header(req, 'if-none-match');
    if (ifNoneMatch === '*' && node) return res(412);
    const body = req.body === undefined ? new Uint8Array() : typeof req.body === 'string' ? enc.encode(req.body) : req.body.slice();
    const etag = this.nextEtag();
    this.nodes.set(path, { kind: 'file', data: body, mtime: this.now(), etag });
    const headers: Record<string, string> = this.putEtag && !this.noEtags ? { ETag: `"${etag}"`, 'OC-ETag': `"${etag}"` } : {};
    return res(node ? 204 : 201, '', headers);
  }

  private del(req: HttpRequest, path: string): HttpResponse {
    const node = this.nodes.get(path);
    if (!node) return res(404);
    const ifMatch = header(req, 'if-match');
    if (ifMatch !== undefined && unquote(ifMatch) !== node.etag) return res(412);
    for (const k of this.nodes.keys()) if (k === path || k.startsWith(`${path}/`)) this.nodes.delete(k);
    return res(204);
  }

  private mkcol(path: string): HttpResponse {
    if (this.nodes.has(path)) return res(405, 'Already exists');
    const parent = this.nodes.get(this.parent(path));
    if (!parent || parent.kind !== 'dir') return res(409, 'Parent missing');
    this.nodes.set(path, { kind: 'dir', mtime: this.now(), etag: this.nextEtag() });
    return res(201);
  }
}
