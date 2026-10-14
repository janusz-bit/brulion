/**
 * Journal (daily note) paths (docs/SYNTAX.md §1): `<journalsFolder>/<journalFileFormat>.md`.
 */
import { isLocalDate, parseLocal, toLocalDate } from '@/core/dates';
import { isMarkdown, joinPath, normalizePath, stripMdExtension } from '@/core/paths';
import { DEFAULT_VAULT_CONFIG, type LocalDate, type Locale, type VaultConfig, type VaultPath } from '../types';
import { formatDate, parseDateStrict } from './dateformat';

/** Formats also recognised when reading journal names (Brulion/Obsidian default, Logseq default). */
const FALLBACK_FORMATS = ['yyyy-MM-dd', 'yyyy_MM_dd'];

function journalFormat(config: VaultConfig): string {
  return config.journalFileFormat || DEFAULT_VAULT_CONFIG.journalFileFormat;
}

/** Journals folder; a missing key falls back to the default (`''` = vault root is kept). */
function journalsFolder(config: VaultConfig): string {
  return config.journalsFolder ?? DEFAULT_VAULT_CONFIG.journalsFolder;
}

/**
 * Vault path of the daily note for `date`, e.g. `journals/2026-10-14.md`. The file name uses
 * `config.journalFileFormat` (date-fns tokens; Moment.js formats are converted; may contain
 * `/` for nested folders). Invalid formats fall back to `yyyy-MM-dd`.
 */
export function journalPathForDate(date: LocalDate, config: VaultConfig): VaultPath {
  const d = parseLocal(date);
  const name =
    formatDate(d, journalFormat(config)) ?? formatDate(d, DEFAULT_VAULT_CONFIG.journalFileFormat) ?? date;
  return joinPath(journalsFolder(config), `${name}.md`);
}

/**
 * Date of a daily note path, or null when `path` is not a journal page: it must be a Markdown
 * file inside `config.journalsFolder` (case-insensitive) whose name parses strictly with
 * `journalFileFormat` (or the common `yyyy-MM-dd` / `yyyy_MM_dd`). Month and weekday names
 * may be English (what {@link journalPathForDate} writes) or Polish.
 */
export function journalDateFromPath(path: VaultPath, config: VaultConfig): LocalDate | null {
  const p = normalizePath(path);
  if (!isMarkdown(p)) return null;
  const folder = normalizePath(journalsFolder(config));
  let rel = p;
  if (folder) {
    if (!p.toLowerCase().startsWith(folder.toLowerCase() + '/')) return null;
    rel = p.slice(folder.length + 1);
  }
  rel = stripMdExtension(rel);
  if (!/\d/.test(rel)) return null;
  const formats = [journalFormat(config), ...FALLBACK_FORMATS.filter((f) => f !== journalFormat(config))];
  const locales: Locale[] = /\p{L}/u.test(rel) ? ['en', 'pl'] : ['en'];
  for (const fmt of formats) {
    for (const locale of locales) {
      const d = parseDateStrict(rel, fmt, locale);
      if (d) {
        const local = toLocalDate(d);
        if (isLocalDate(local)) return local;
      }
    }
  }
  return null;
}
