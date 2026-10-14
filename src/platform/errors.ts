/**
 * Errors raised by platform implementations. UI code can branch on `code` (e.g. show a
 * "grant access" button for `permission-denied`, which must be retried from a user gesture).
 */

/** Machine-readable reason of a {@link PlatformError}. */
export type PlatformErrorCode =
  /** The operation or vault location kind is not available on this platform. */
  | 'unsupported'
  /** A saved File System Access handle is gone (browser data cleared, other profile). */
  | 'handle-missing'
  /** The user (or browser) refused access; retry from a user gesture. */
  | 'permission-denied'
  /** The vault location is malformed (empty path, unknown kind). */
  | 'invalid-location'
  /** A URL that must not be opened externally (javascript:, data:, unparsable). */
  | 'invalid-url';

/** Error thrown by `src/platform` for platform-level failures (not file-system errors). */
export class PlatformError extends Error {
  override name = 'PlatformError';
  readonly code: PlatformErrorCode;

  constructor(code: PlatformErrorCode, message: string, options?: { cause?: unknown }) {
    super(message, options);
    this.code = code;
  }
}

/** True when `e` is a {@link PlatformError} (optionally with the given code). */
export function isPlatformError(e: unknown, code?: PlatformErrorCode): e is PlatformError {
  return e instanceof PlatformError && (code === undefined || e.code === code);
}

/** Name of an error-like value (`DOMException`, `Error`), or '' when it has none. */
export function errorName(e: unknown): string {
  if (typeof e !== 'object' || e === null) return '';
  const name = (e as { name?: unknown }).name;
  return typeof name === 'string' ? name : '';
}

/** Message of an error-like value; strings (Tauri IPC rejections) are returned as is. */
export function errorMessage(e: unknown): string {
  if (typeof e === 'string') return e;
  if (e instanceof Error) return e.message;
  if (typeof e === 'object' && e !== null) {
    const msg = (e as { message?: unknown }).message;
    if (typeof msg === 'string') return msg;
  }
  return String(e);
}
