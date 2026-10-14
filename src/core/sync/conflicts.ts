/**
 * Conflict copies: Brulion's own ("Note.conflict-<device>-<yyyyMMdd-HHmmss>.md", made by the
 * WebDAV engine) and Syncthing's ("Note.sync-conflict-<date>-<time>-<DEVICE>.md").
 */
import { basename, dirname, joinPath, parseSyncthingConflict } from '../paths';
import type { FileStat, VaultPath } from '../types';
import type { ConflictCopy } from './types';

const pad2 = (n: number): string => String(n).padStart(2, '0');

/** Local wall-clock stamp "yyyyMMdd-HHmmss". */
export function conflictStamp(ms: number): string {
  const d = new Date(ms);
  return `${d.getFullYear()}${pad2(d.getMonth() + 1)}${pad2(d.getDate())}-${pad2(d.getHours())}${pad2(d.getMinutes())}${pad2(d.getSeconds())}`;
}

/**
 * Device name usable inside a file name: letters (any script), digits, '_' and '-' only;
 * other runs become '-'. "Mój laptop" -> "Mój-laptop". Falls back to "urzadzenie".
 */
export function sanitizeDeviceName(device: string): string {
  const cleaned = device
    .normalize('NFC')
    .replace(/[^\p{L}\p{N}_-]+/gu, '-')
    .replace(/-{2,}/g, '-')
    .replace(/^-+|-+$/g, '')
    .slice(0, 40)
    .replace(/-+$/, '');
  return cleaned || 'urzadzenie';
}

/** Split "dir/Name.ext" into stem "dir/Name" and extension ".ext" ('' when none). */
function splitExtension(path: VaultPath): { stem: string; ext: string } {
  const base = basename(path);
  const dot = base.lastIndexOf('.');
  if (dot <= 0) return { stem: path, ext: '' };
  return { stem: path.slice(0, path.length - (base.length - dot)), ext: base.slice(dot) };
}

/**
 * Path for a conflict copy of `path`: "dir/Name.conflict-<device>-<yyyyMMdd-HHmmss>.md".
 * The caller makes it unique (e.g. `Vault.create` appends " 2").
 */
export function conflictCopyPath(path: VaultPath, device: string, timeMs: number): VaultPath {
  const { stem, ext } = splitExtension(path);
  return `${stem}.conflict-${sanitizeDeviceName(device)}-${conflictStamp(timeMs)}${ext}`;
}

const BRULION_CONFLICT = /^(.*)\.conflict-(.+)-(\d{8})-(\d{6})(?: \d+)?(\.[^./]*)?$/;

/** Parse a Brulion conflict copy name; null for other files. */
export function parseBrulionConflict(path: VaultPath): { original: VaultPath; time: string; device: string } | null {
  const m = BRULION_CONFLICT.exec(basename(path));
  if (!m) return null;
  const [, stem, device, d, t, ext] = m;
  if (!stem) return null;
  const time = `${d!.slice(0, 4)}-${d!.slice(4, 6)}-${d!.slice(6, 8)}T${t!.slice(0, 2)}:${t!.slice(2, 4)}:${t!.slice(4, 6)}`;
  return { original: joinPath(dirname(path), `${stem}${ext ?? ''}`), time, device: device! };
}

/**
 * Conflict copies among `files` (Syncthing "sync-conflict" files and Brulion ".conflict-"
 * copies), sorted by original path, then time.
 */
export function findSyncthingConflicts(files: readonly FileStat[]): ConflictCopy[] {
  const out: ConflictCopy[] = [];
  for (const f of files) {
    if (f.kind !== 'file') continue;
    const st = parseSyncthingConflict(f.path);
    if (st) {
      out.push({ original: st.original, copy: f.path, time: st.time, device: st.device, source: 'syncthing' });
      continue;
    }
    const br = parseBrulionConflict(f.path);
    if (br) out.push({ original: br.original, copy: f.path, time: br.time, device: br.device, source: 'brulion' });
  }
  return out.sort((a, b) => (a.original !== b.original ? (a.original < b.original ? -1 : 1) : a.time < b.time ? -1 : a.time > b.time ? 1 : 0));
}

/** Alias with a neutral name: all conflict copies (Syncthing and Brulion). */
export const findConflicts = findSyncthingConflicts;
