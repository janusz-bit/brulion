/**
 * Validation of URLs handed to the OS / a new browser tab. Links come from notes, which may
 * come from anywhere (imports, sync), so script-capable schemes are refused.
 */
import { PlatformError } from './errors';

/** Schemes that can run code or read local data when opened. */
const BLOCKED_SCHEMES = new Set(['javascript:', 'vbscript:', 'data:', 'blob:', 'file:', 'about:', 'chrome:', 'view-source:']);

/**
 * Returns the parsed, normalised URL string or throws `PlatformError('invalid-url')` for
 * unparsable input, relative URLs and blocked schemes (javascript:, data:, file:, ...).
 * Allowed: http(s), mailto, tel, geo, matrix, and other app schemes (obsidian:, logseq:, ...).
 */
export function checkExternalUrl(url: string): string {
  const trimmed = url.trim();
  let parsed: URL;
  try {
    parsed = new URL(trimmed);
  } catch (e) {
    throw new PlatformError('invalid-url', `Not an absolute URL: ${trimmed}`, { cause: e });
  }
  if (BLOCKED_SCHEMES.has(parsed.protocol.toLowerCase())) {
    throw new PlatformError('invalid-url', `Refusing to open a ${parsed.protocol} URL`);
  }
  return parsed.href;
}

/** `url` without user name / password (for error messages and logs). */
export function redactUrl(url: string): string {
  try {
    const u = new URL(url);
    if (!u.username && !u.password) return u.href;
    u.username = '';
    u.password = '';
    return u.href;
  } catch {
    return url.replace(/\/\/[^/@]*@/, '//');
  }
}
