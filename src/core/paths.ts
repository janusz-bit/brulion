/**
 * Vault path helpers. Vault paths are POSIX-style, relative, without leading "./" or "/".
 */
import type { VaultPath } from './types';

/** Normalise separators, collapse "." and "..", strip leading/trailing slashes. NFC-normalised. */
export function normalizePath(p: string): VaultPath {
  const parts: string[] = [];
  for (const seg of p.normalize('NFC').replace(/\\/g, '/').split('/')) {
    if (seg === '' || seg === '.') continue;
    if (seg === '..') {
      parts.pop();
      continue;
    }
    parts.push(seg);
  }
  return parts.join('/');
}

export function joinPath(...segments: string[]): VaultPath {
  return normalizePath(segments.filter((s) => s !== '').join('/'));
}

/** "a/b/c.md" -> "a/b"; "c.md" -> "". */
export function dirname(p: VaultPath): string {
  const i = p.lastIndexOf('/');
  return i === -1 ? '' : p.slice(0, i);
}

/** "a/b/c.md" -> "c.md". */
export function basename(p: VaultPath): string {
  const i = p.lastIndexOf('/');
  return i === -1 ? p : p.slice(i + 1);
}

/** "a/b/c.md" -> ".md" (lower-cased); "" when none. */
export function extname(p: VaultPath): string {
  const b = basename(p);
  const i = b.lastIndexOf('.');
  return i <= 0 ? '' : b.slice(i).toLowerCase();
}

export function isMarkdown(p: VaultPath): boolean {
  const e = extname(p);
  return e === '.md' || e === '.markdown';
}

/** "a/b/Page.md" -> "a/b/Page". */
export function stripMdExtension(p: VaultPath): string {
  return p.replace(/\.(md|markdown)$/i, '');
}

/** Page title from path: "a/b/Page.md" -> "Page". */
export function pageTitle(p: VaultPath): string {
  return basename(stripMdExtension(p));
}

const IGNORED_DIRS = new Set(['.brulion', '.trash', '.git', '.obsidian', 'logseq', '.stfolder', '.stversions', 'node_modules', '.github']);

/** True for paths that must never be indexed or treated as notes. */
export function isIgnoredPath(p: VaultPath): boolean {
  const segments = p.split('/');
  for (let i = 0; i < segments.length - 1; i++) {
    if (IGNORED_DIRS.has(segments[i]!)) return true;
  }
  const name = segments[segments.length - 1] ?? '';
  if (IGNORED_DIRS.has(name)) return true;
  return isTempFile(name);
}

/** Temp files created by sync tools / editors while writing. */
export function isTempFile(name: string): boolean {
  return (
    /^~syncthing~.*\.tmp$/.test(name) ||
    /^\.syncthing\..*\.tmp$/.test(name) ||
    /^\.~lock\..*#$/.test(name) ||
    name.endsWith('.part') ||
    name.startsWith('.brulion-tmp-') ||
    name === '.DS_Store' ||
    name === 'Thumbs.db' ||
    name === '.stignore' ||
    name === '.nomedia'
  );
}

const SYNCTHING_CONFLICT = /^(.*)\.sync-conflict-(\d{8})-(\d{6})-([A-Z0-9]{7})(\.[^./]*)?$/;

export interface SyncthingConflictInfo {
  /** The original file the conflict copy belongs to. */
  original: VaultPath;
  /** Conflict time as local "YYYY-MM-DDTHH:mm:ss". */
  time: string;
  /** Short device id that produced the conflict copy. */
  device: string;
}

/** Parse "dir/Note.sync-conflict-20261014-101010-ABCDEFG.md". */
export function parseSyncthingConflict(p: VaultPath): SyncthingConflictInfo | null {
  const dir = dirname(p);
  const m = SYNCTHING_CONFLICT.exec(basename(p));
  if (!m) return null;
  const [, stem, d, t, device, ext] = m;
  const original = joinPath(dir, `${stem}${ext ?? ''}`);
  const time = `${d!.slice(0, 4)}-${d!.slice(4, 6)}-${d!.slice(6, 8)}T${t!.slice(0, 2)}:${t!.slice(2, 4)}:${t!.slice(4, 6)}`;
  return { original, time, device: device! };
}

export function isConflictCopy(p: VaultPath): boolean {
  return parseSyncthingConflict(p) !== null || /\.conflict-[^/]*\.md$/i.test(p);
}

/** Make a file-name-safe page name (keeps Unicode letters, removes path separators etc.). */
export function sanitizeFileName(name: string): string {
  const cleaned = name
    .normalize('NFC')
    // eslint-disable-next-line no-control-regex
    .replace(/[\\/:*?"<>|#^[\]\u0000-\u001f]/g, ' ')
    .replace(/\s+/g, ' ')
    .trim()
    .replace(/^\.+/, '');
  return cleaned.slice(0, 180) || 'Bez tytułu';
}

/** Pick a non-colliding path by appending " 2", " 3", ... before the extension. */
export function uniquePath(p: VaultPath, exists: (p: VaultPath) => boolean): VaultPath {
  if (!exists(p)) return p;
  const ext = extname(p);
  const stem = ext ? p.slice(0, -ext.length) : p;
  for (let i = 2; ; i++) {
    const candidate = `${stem} ${i}${ext}`;
    if (!exists(candidate)) return candidate;
  }
}
