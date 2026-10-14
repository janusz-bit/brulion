/**
 * URL, header and encoding helpers for WebDAV (all pure).
 */
import { normalizePath } from '../paths';
import type { VaultPath } from '../types';

const SCHEME_RE = /^[a-z][a-z0-9+.-]*:\/\//i;

/** Percent-encode every segment of a vault path ("Notatki/Zażółć.md" -> "Notatki/Za%C5%BC...md"). */
export function encodePath(path: string): string {
  return path
    .split('/')
    .filter((s) => s !== '')
    .map((s) => encodeURIComponent(s))
    .join('/');
}

/** Decode one URL path segment; malformed escapes are kept as written. */
export function decodeSegment(segment: string): string {
  try {
    return decodeURIComponent(segment);
  } catch {
    return segment;
  }
}

/** Remove trailing slashes ("https://h/a//" -> "https://h/a"). */
export function trimTrailingSlashes(s: string): string {
  return s.replace(/\/+$/, '');
}

/**
 * WebDAV URL of a Nextcloud user folder:
 * `nextcloudDavUrl('cloud.example.com/', 'jan', 'Notatki/Brulion')` ->
 * `https://cloud.example.com/remote.php/dav/files/jan/Notatki/Brulion`.
 *
 * The server may be given with or without scheme (default https), with a sub-path
 * ("https://example.com/nextcloud"), or as a full DAV / web UI URL (the `/remote.php/...` or
 * `/index.php/...` part is dropped). User name and folder segments are percent-encoded.
 */
export function nextcloudDavUrl(server: string, user: string, folder: string): string {
  let s = server.trim();
  if (!SCHEME_RE.test(s)) s = `https://${s.replace(/^\/+/, '')}`;
  // Drop query / fragment and anything from a known Nextcloud entry point on.
  s = s.replace(/[?#].*$/, '');
  s = s.replace(/\/(?:remote\.php|index\.php)(?:\/.*)?$/i, '');
  // Web UI pages pasted from the address bar: ".../apps/files/", ".../login".
  s = trimTrailingSlashes(s).replace(/\/(?:apps\/[^/]+|login)$/i, '');
  s = trimTrailingSlashes(s);
  const userPart = encodeURIComponent(user.trim().normalize('NFC'));
  const folderPart = encodePath(normalizePath(folder.trim()));
  return `${s}/remote.php/dav/files/${userPart}${folderPart ? `/${folderPart}` : ''}`;
}

/** Decoded path part of a URL ("https://h/a%20b/" -> "/a b/"); works for absolute paths too. */
export function urlPathname(url: string): string {
  let pathname: string;
  if (SCHEME_RE.test(url)) {
    try {
      pathname = new URL(url).pathname;
    } catch {
      pathname = url.replace(SCHEME_RE, '').replace(/^[^/]*/, '');
    }
  } else {
    pathname = url;
  }
  pathname = pathname.replace(/[?#].*$/, '');
  return pathname.split('/').map(decodeSegment).join('/');
}

/**
 * Decoded path of a multistatus `href`, which may be a full URL, an absolute path or a path
 * relative to the request URL.
 */
export function hrefPathname(href: string, requestUrl: string): string {
  const h = href.trim();
  if (SCHEME_RE.test(h) || h.startsWith('/')) return urlPathname(h);
  const dir = urlPathname(requestUrl).replace(/[^/]*$/, '');
  return `${dir}${h.split('/').map(decodeSegment).join('/')}`;
}

/**
 * Path of `pathname` relative to `basePathname` (both decoded), compared segment by segment
 * (NFC, so NFD names from macOS clients match; case-insensitive fallback for proxies that
 * change the case of the prefix). The result keeps the server's spelling of the remaining
 * segments. Returns null when it lies outside the base or contains `.`/`..` segments.
 */
export function relativeTo(pathname: string, basePathname: string): string | null {
  const p = pathname.split('/').filter((x) => x !== '');
  const b = basePathname.split('/').filter((x) => x !== '');
  if (p.length < b.length) return null;
  const nfc = (x: string): string => x.normalize('NFC');
  const exact = b.every((seg, i) => nfc(seg) === nfc(p[i]!));
  if (!exact && !b.every((seg, i) => nfc(seg).toLowerCase() === nfc(p[i]!).toLowerCase())) return null;
  const rest = p.slice(b.length);
  if (rest.some((seg) => seg === '..' || seg === '.')) return null;
  return rest.join('/');
}

/** Join a base URL and an (unencoded) vault path; `dir` adds a trailing slash. */
export function joinUrl(baseUrl: string, path: VaultPath, dir = false): string {
  const base = trimTrailingSlashes(baseUrl);
  const encoded = encodePath(path);
  const url = encoded ? `${base}/${encoded}` : base;
  return dir ? `${url}/` : url;
}

/** Case-insensitive header lookup. */
export function getHeader(headers: Record<string, string> | undefined, name: string): string | undefined {
  if (!headers) return undefined;
  const direct = headers[name];
  if (direct !== undefined) return direct;
  const lower = name.toLowerCase();
  for (const [k, v] of Object.entries(headers)) if (k.toLowerCase() === lower) return v;
  return undefined;
}

/** Normalise an ETag for storage/comparison: trims, drops `W/` and surrounding quotes. */
export function normalizeEtag(etag: string | undefined | null): string {
  if (!etag) return '';
  return etag
    .trim()
    .replace(/^W\//i, '')
    .replace(/^"(.*)"$/, '$1')
    .replace(/^&quot;(.*)&quot;$/, '$1');
}

/** ETag in the quoted form required by `If-Match`. */
export function quoteEtag(etag: string): string {
  return `"${normalizeEtag(etag)}"`;
}

const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

/** Standard padded base64 of raw bytes. */
export function base64Encode(bytes: Uint8Array): string {
  let out = '';
  let i = 0;
  for (; i + 2 < bytes.length; i += 3) {
    const n = (bytes[i]! << 16) | (bytes[i + 1]! << 8) | bytes[i + 2]!;
    out += B64[(n >> 18) & 63]! + B64[(n >> 12) & 63]! + B64[(n >> 6) & 63]! + B64[n & 63]!;
  }
  const rest = bytes.length - i;
  if (rest === 1) {
    const n = bytes[i]! << 16;
    out += `${B64[(n >> 18) & 63]!}${B64[(n >> 12) & 63]!}==`;
  } else if (rest === 2) {
    const n = (bytes[i]! << 16) | (bytes[i + 1]! << 8);
    out += `${B64[(n >> 18) & 63]!}${B64[(n >> 12) & 63]!}${B64[(n >> 6) & 63]!}=`;
  }
  return out;
}

/** `Authorization` header value for HTTP Basic auth (UTF-8 user name and password). */
export function basicAuth(username: string, password: string): string {
  return `Basic ${base64Encode(new TextEncoder().encode(`${username}:${password}`))}`;
}
