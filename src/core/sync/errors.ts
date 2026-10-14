/**
 * Errors of the sync module: WebDAV failures (typed by HTTP status / transport failure) and
 * engine-level refusals (e.g. a side that suddenly looks empty).
 */
import type { WebDavErrorCode } from './types';

/** A failed WebDAV request. `status` is 0 for network / CORS failures. */
export class WebDavError extends Error {
  override name = 'WebDavError';
  readonly status: number;
  readonly code: WebDavErrorCode;
  /** The request URL, when known. */
  readonly url: string | undefined;

  constructor(code: WebDavErrorCode, status: number, message: string, url?: string) {
    super(message);
    this.code = code;
    this.status = status;
    this.url = url;
  }
}

/** True when `e` is a WebDavError (optionally with the given code). */
export function isWebDavError(e: unknown, code?: WebDavErrorCode): e is WebDavError {
  return e instanceof WebDavError && (code === undefined || e.code === code);
}

/** Map an HTTP status to an error category. */
export function codeForStatus(status: number): WebDavErrorCode {
  switch (status) {
    case 401:
      return 'auth';
    case 403:
      return 'forbidden';
    case 404:
    case 410:
      return 'not-found';
    case 405:
      return 'not-allowed';
    case 409:
      return 'conflict';
    case 412:
      return 'precondition';
    case 413:
      return 'too-large';
    case 423:
      return 'locked';
    case 507:
      return 'insufficient-storage';
    default:
      return status >= 500 ? 'server' : 'http';
  }
}

/** True for an AbortError (DOMException or any error named so). */
export function isAbortError(e: unknown): boolean {
  return typeof e === 'object' && e !== null && (e as { name?: unknown }).name === 'AbortError';
}

function causeCode(e: unknown): string | undefined {
  if (typeof e !== 'object' || e === null) return undefined;
  const cause = (e as { cause?: unknown }).cause;
  if (typeof cause !== 'object' || cause === null) return undefined;
  const code = (cause as { code?: unknown }).code;
  return typeof code === 'string' ? code : undefined;
}

/**
 * Classify an exception thrown by the HTTP transport. In browsers `fetch` rejects with a bare
 * TypeError both for CORS refusals and unreachable hosts; while the browser reports being
 * online that is most likely CORS, so it maps to 'cors'. Node's fetch attaches a `cause` with
 * a system error code (ECONNREFUSED, ENOTFOUND, ...), which is a plain network failure.
 */
export function classifyTransportError(e: unknown, url?: string): WebDavError {
  if (e instanceof WebDavError) return e;
  const message = e instanceof Error ? e.message : String(e);
  const offline = typeof globalThis.navigator !== 'undefined' && globalThis.navigator.onLine === false;
  const code: WebDavErrorCode = e instanceof TypeError && !offline && causeCode(e) === undefined ? 'cors' : 'network';
  return new WebDavError(code, 0, message || code, url);
}

/** Codes of `SyncError`. */
export type SyncErrorCode = 'local-empty' | 'remote-empty' | 'remote-busy';

/**
 * The engine refused to continue because the situation looks like an accident rather than a
 * user's intention (e.g. the server folder is suddenly empty: syncing would trash every note).
 * The UI can offer `SyncEngine.resetState()` to start over.
 */
export class SyncError extends Error {
  override name = 'SyncError';
  readonly code: SyncErrorCode;

  constructor(code: SyncErrorCode, message: string) {
    super(message);
    this.code = code;
  }
}

/** True when `e` is a SyncError (optionally with the given code). */
export function isSyncError(e: unknown, code?: SyncErrorCode): e is SyncError {
  return e instanceof SyncError && (code === undefined || e.code === code);
}

/** Human-readable message of any thrown value. */
export function errorMessage(e: unknown): string {
  if (e instanceof Error) return e.message || e.name;
  return String(e);
}
