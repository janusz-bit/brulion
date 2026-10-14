/**
 * Which vault files are treated as text (content cached, hashed, snapshotted into history)
 * versus binary (attachments, compared by size/mtime only).
 */
import { extname } from '../paths';
import type { VaultPath } from '../types';

const TEXT_EXTENSIONS = new Set([
  '.md',
  '.markdown',
  '.txt',
  '.text',
  '.json',
  '.canvas',
  '.excalidraw',
  '.base',
  '.csv',
  '.tsv',
  '.css',
  '.html',
  '.htm',
  '.xml',
  '.svg',
  '.yml',
  '.yaml',
  '.toml',
  '.ini',
  '.ics',
  '.opml',
  '.org',
  '.tex',
  '.rst',
  '.adoc',
  '.log',
  '.js',
  '.mjs',
  '.ts',
]);

/** True for files whose content Brulion handles as UTF-8 text. */
export function isTextPath(path: VaultPath): boolean {
  return TEXT_EXTENSIONS.has(extname(path));
}
