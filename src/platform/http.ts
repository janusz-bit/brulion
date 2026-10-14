/**
 * `HttpTransport` on top of a fetch-like function. Used with the browser's `fetch` (web) and
 * with `@tauri-apps/plugin-http`'s `fetch` (Tauri: no CORS limits). WebDAV needs arbitrary
 * methods (PROPFIND, MKCOL, MOVE, ...), raw byte bodies and access to all response headers.
 *
 * HTTP error statuses (401, 404, 412, 5xx) are *returned*, not thrown: the caller decides.
 * Only transport failures throw, as an Error named `NetworkError`.
 */
import type { HttpRequest, HttpResponse, HttpTransport } from '@/core/types';
import { toArrayBufferView } from './bytes';
import { errorMessage, errorName } from './errors';
import { redactUrl } from './url';

/** The subset of `fetch` the transport needs. */
export type FetchLike = (input: string, init: RequestInit) => Promise<Response>;

export interface FetchTransportOptions {
  /**
   * Mention CORS in network error messages (browser: a server without CORS headers for this
   * origin fails exactly like an unreachable one). Default true.
   */
  corsHint?: boolean;
  /** Extra RequestInit fields (e.g. `credentials`, `cache`). */
  init?: Omit<RequestInit, 'method' | 'headers' | 'body'>;
}

/** Name of errors thrown by transports when the request did not produce an HTTP response. */
export const NETWORK_ERROR = 'NetworkError';

/** System error code (`ECONNREFUSED`, `ENOTFOUND`, ...) that Node-style fetch implementations attach as `cause.code`. */
function systemErrorCode(e: unknown): string | undefined {
  if (typeof e !== 'object' || e === null) return undefined;
  const cause = (e as { cause?: unknown }).cause;
  if (typeof cause !== 'object' || cause === null) return undefined;
  const code = (cause as { code?: unknown }).code;
  return typeof code === 'string' ? code : undefined;
}

/**
 * Build the `NetworkError` thrown for failed requests. The message names the request
 * (credentials stripped from the URL) and, for browsers, the CORS possibility.
 *
 * Browsers reject `fetch` with a bare `TypeError` both for CORS refusals and unreachable
 * hosts. With `corsHint` such a failure is thrown as a `TypeError` (still named
 * `NetworkError`), so `classifyTransportError` from `@/core/sync` can tell the user about
 * CORS; failures with a system error code (connection refused, DNS) and native transports
 * (`corsHint: false`) produce a plain `Error`.
 */
export function createNetworkError(req: Pick<HttpRequest, 'method' | 'url'>, cause: unknown, corsHint = true): Error {
  const reason = errorMessage(cause);
  const cors = corsHint && systemErrorCode(cause) === undefined;
  const hint = cors
    ? 'the device may be offline, the server unreachable, or the request was blocked by CORS (the server must allow this origin)'
    : 'the device may be offline, or the server is unreachable';
  const message = `Network request failed: ${req.method.toUpperCase()} ${redactUrl(req.url)} — ${hint}${reason ? ` (${reason})` : ''}`;
  const err = cors && cause instanceof TypeError ? new TypeError(message, { cause }) : new Error(message, { cause });
  err.name = NETWORK_ERROR;
  return err;
}

/** True for errors produced by {@link createNetworkError} (or any error named NetworkError). */
export function isNetworkError(e: unknown): boolean {
  return errorName(e) === NETWORK_ERROR;
}

/** Response headers as a plain object with lower-cased names. */
export function headersToRecord(headers: Headers): Record<string, string> {
  const out: Record<string, string> = {};
  headers.forEach((value, key) => {
    out[key.toLowerCase()] = value;
  });
  return out;
}

const BODYLESS = new Set(['GET', 'HEAD']);

/** RequestInit for an HttpRequest (method upper-cased: WebDAV servers expect PROPFIND, not propfind). */
export function toRequestInit(req: HttpRequest, extra: FetchTransportOptions['init'] = {}): RequestInit {
  const method = req.method.toUpperCase();
  const init: RequestInit = { ...extra, method, headers: { ...req.headers } };
  if (req.body !== undefined) {
    if (BODYLESS.has(method)) throw new TypeError(`HTTP ${method} requests cannot have a body`);
    init.body = typeof req.body === 'string' ? req.body : toArrayBufferView(req.body);
  }
  return init;
}

/** Create an HttpTransport from a fetch-like function. */
export function createFetchTransport(fetchFn: FetchLike, opts: FetchTransportOptions = {}): HttpTransport {
  const corsHint = opts.corsHint ?? true;
  return async (req: HttpRequest): Promise<HttpResponse> => {
    const init = toRequestInit(req, opts.init);
    let res: Response;
    try {
      res = await fetchFn(req.url, init);
    } catch (e) {
      if (errorName(e) === 'AbortError') throw e;
      throw createNetworkError(req, e, corsHint);
    }
    let body: Uint8Array;
    try {
      body = new Uint8Array(await res.arrayBuffer());
    } catch (e) {
      // Connection dropped while reading the body: the response passed CORS already.
      if (errorName(e) === 'AbortError') throw e;
      throw createNetworkError(req, e, false);
    }
    return { status: res.status, headers: headersToRecord(res.headers), body };
  };
}

/**
 * The browser transport: `globalThis.fetch` (looked up per call so tests and polyfills can
 * replace it), no cookies (Nextcloud rejects WebDAV requests carrying a session cookie
 * without a CSRF token) and no HTTP cache (sync must always see the server's state).
 */
export function createBrowserHttpTransport(fetchFn?: FetchLike): HttpTransport {
  const f: FetchLike = fetchFn ?? ((input, init) => globalThis.fetch(input, init));
  return createFetchTransport(f, { corsHint: true, init: { credentials: 'omit', cache: 'no-store', redirect: 'follow' } });
}
