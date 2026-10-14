/**
 * Link resolution (docs/SYNTAX.md §2): `target` → vault file.
 *
 * Order: 1) exact vault path (with or without `.md`, case-insensitive); 2) path relative to
 * the linking file's folder; 3) base-name match (case-insensitive, Unicode NFC; for targets
 * with folders, a path-suffix match) — several candidates → shortest path, then
 * alphabetical; 4) frontmatter `aliases`. Explicitly relative targets (`./x`, `../x`) try the
 * relative path first; a leading `/` anchors the target at the vault root.
 *
 * {@link NameTable} keeps the lookup maps incrementally. Every file that a target can resolve
 * to shares the target's {@link linkKey} (its lower-cased base name without `.md`), which lets
 * the index re-resolve only the links that a file change can affect.
 */
import { basename, dirname, extname, joinPath, normalizePath, stripMdExtension } from '@/core/paths';
import type { VaultPath } from '@/core/types';
import { compareCode } from './text';

/** Add `value` to the set stored under `key`. */
export function multiAdd<K, V>(map: Map<K, Set<V>>, key: K, value: V): void {
  let set = map.get(key);
  if (!set) map.set(key, (set = new Set()));
  set.add(value);
}

/** Remove `value` from the set under `key` (dropping empty sets). */
export function multiDelete<K, V>(map: Map<K, Set<V>>, key: K, value: V): void {
  const set = map.get(key);
  if (!set) return;
  set.delete(value);
  if (set.size === 0) map.delete(key);
}

/** Add 1 to the count of `value` under `key`. */
export function countAdd<K, V>(map: Map<K, Map<V, number>>, key: K, value: V): void {
  let m = map.get(key);
  if (!m) map.set(key, (m = new Map()));
  m.set(value, (m.get(value) ?? 0) + 1);
}

/** Subtract 1 from the count of `value` under `key` (dropping zero counts and empty maps). */
export function countDelete<K, V>(map: Map<K, Map<V, number>>, key: K, value: V): void {
  const m = map.get(key);
  if (!m) return;
  const n = (m.get(value) ?? 0) - 1;
  if (n > 0) m.set(value, n);
  else m.delete(value);
  if (m.size === 0) map.delete(key);
}

/** Case-insensitive key of a vault path ("Projekty/Dom.md" → "projekty/dom.md"). */
export function pathKey(p: string): string {
  return fastNormalize(p).toLowerCase();
}

/** Name key of a note path: lower-cased title ("Projekty/Dom.md" → "dom"). */
export function noteNameKey(path: VaultPath): string {
  return basename(stripMdExtension(fastNormalize(path))).toLowerCase();
}

/** Name key of an attachment path: lower-cased file name ("assets/Foto.PNG" → "foto.png"). */
export function fileNameKey(path: VaultPath): string {
  return basename(fastNormalize(path)).toLowerCase();
}

/** Key of an alias (NFC, trimmed, lower-cased). */
export function aliasKey(alias: string): string {
  return (NON_ASCII_RE.test(alias) ? alias.normalize('NFC') : alias).trim().toLowerCase();
}

const NON_ASCII_RE = /[\u0080-\uffff]/;

/**
 * Invalidation key of a link target: its base name without `.md`, lower-cased. Every file a
 * target can resolve to has this key as its note/file name key or as the key of one of its
 * aliases (see {@link aliasLinkKey}).
 */
export function linkKey(target: string): string {
  return basename(stripMdExtension(fastNormalize(cleanTarget(target)))).toLowerCase();
}

/** Invalidation key under which an alias can be reached ({@link linkKey} of the alias). */
export function aliasLinkKey(alias: string): string {
  return linkKey(alias);
}

/**
 * Strip what is not part of the file name: surrounding `[[ ]]`, a `|display` part (also the
 * table-escaped `\|`), a `#heading` / `#^block` fragment; trim; backslashes become slashes.
 */
export function cleanTarget(target: string): string {
  let t = target.trim();
  if (t.startsWith('[[') && t.endsWith(']]')) t = t.slice(2, -2);
  const pipe = t.indexOf('|');
  if (pipe !== -1) t = t.slice(0, pipe).replace(/\\$/, '');
  const hash = t.indexOf('#');
  if (hash !== -1) t = t.slice(0, hash);
  t = t.trim();
  return t.indexOf('\\') === -1 ? t : t.replace(/\\/g, '/');
}

/** Does the target carry a URL scheme (`https:`, `mailto:`, …)? Windows drives excluded. */
export function isExternalTarget(target: string): boolean {
  return /^[A-Za-z][A-Za-z0-9+.-]*:/.test(target) && !/^[A-Za-z]:[\\/]/.test(target);
}

/** Pick the preferred path: shortest, then alphabetical (code units — deterministic). */
export function pickBest(paths: Iterable<VaultPath>): VaultPath | null {
  let best: VaultPath | null = null;
  for (const p of paths) {
    if (best === null || p.length < best.length || (p.length === best.length && compareCode(p, best) < 0)) best = p;
  }
  return best;
}

function isMdExt(ext: string): boolean {
  return ext === '.md' || ext === '.markdown';
}

/** Incrementally maintained lookup tables for link resolution. */
export class NameTable {
  /** pathKey → paths (notes and attachments; several only when names differ by case). */
  private readonly byPath = new Map<string, Set<VaultPath>>();
  /** note name key → note paths. */
  private readonly noteNames = new Map<string, Set<VaultPath>>();
  /** attachment file-name key → attachment paths. */
  private readonly fileNames = new Map<string, Set<VaultPath>>();
  /** alias key → note paths. */
  private readonly aliases = new Map<string, Set<VaultPath>>();

  addNote(path: VaultPath, aliases: readonly string[]): void {
    multiAdd(this.byPath, pathKey(path), path);
    multiAdd(this.noteNames, noteNameKey(path), path);
    for (const a of aliases) {
      const k = aliasKey(a);
      if (k) multiAdd(this.aliases, k, path);
    }
  }

  removeNote(path: VaultPath, aliases: readonly string[]): void {
    multiDelete(this.byPath, pathKey(path), path);
    multiDelete(this.noteNames, noteNameKey(path), path);
    for (const a of aliases) multiDelete(this.aliases, aliasKey(a), path);
  }

  addAttachment(path: VaultPath): void {
    multiAdd(this.byPath, pathKey(path), path);
    multiAdd(this.fileNames, fileNameKey(path), path);
  }

  removeAttachment(path: VaultPath): void {
    multiDelete(this.byPath, pathKey(path), path);
    multiDelete(this.fileNames, fileNameKey(path), path);
  }

  clear(): void {
    this.byPath.clear();
    this.noteNames.clear();
    this.fileNames.clear();
    this.aliases.clear();
  }

  /** Resolve `target` written in `from` (see the module doc). Empty target → `from`. */
  resolve(target: string, from?: VaultPath): VaultPath | null {
    const t = cleanTarget(target);
    if (!t) return from ?? null;
    if (t.indexOf('/') === -1 && t.charCodeAt(0) !== 46 /* . */) return this.resolveBare(t, from);
    const norm = fastNormalize(t);
    if (!norm) return null;
    const lower = norm.toLowerCase();
    const explicitRelative = t.charCodeAt(0) === 46 /* . */ && /^\.\.?\//.test(t);
    const fromDir = from ? fastNormalize(dirname(from)) : '';
    if (explicitRelative && from) {
      const rel = joinPath(fromDir, t);
      const hit = this.lookupPath(rel, rel.toLowerCase());
      if (hit) return hit;
    }
    const exact = this.lookupPath(norm, lower);
    if (exact) return exact;
    if (fromDir && !explicitRelative && t.charCodeAt(0) !== 47 /* / */) {
      const rel = norm === t ? `${fromDir}/${norm}` : joinPath(fromDir, t);
      const hit = this.lookupPath(rel, rel.toLowerCase());
      if (hit) return hit;
    }
    const byName = this.lookupName(lower);
    if (byName) return byName;
    if (this.aliases.size === 0) return null;
    const aliased = this.aliases.get(aliasKey(t));
    return aliased ? pickBest(aliased) : null;
  }

  /**
   * Fast path for a target without folders (`[[Page]]`, `![[photo.png]]` — the common case):
   * same order as {@link resolve}, but steps 1-2 only look at the files sharing the name.
   */
  private resolveBare(t: string, from?: VaultPath): VaultPath | null {
    const lower = (NON_ASCII_RE.test(t) ? t.normalize('NFC') : t).toLowerCase();
    const ext = extname(lower);
    const md = isMdExt(ext);
    const notes = this.noteNames.get(md ? stripMdExtension(lower) : lower);
    const files = md ? undefined : this.fileNames.get(lower);
    if (notes || files) {
      // 1) the file at the vault root, 2) the file in the linking file's folder.
      const fromDir = from && from.indexOf('/') !== -1 ? dirKey(from) : '';
      const hit = inDir(notes, '') ?? inDir(files, '') ?? (fromDir ? (inDir(notes, fromDir) ?? inDir(files, fromDir)) : null);
      if (hit) return hit;
      // 3) unique base name (shortest path, then alphabetical).
      const filesFirst = ext !== '' && !md && !/\s/.test(ext);
      const best = filesFirst ? (pickBest(files ?? []) ?? pickBest(notes ?? [])) : (pickBest(notes ?? []) ?? pickBest(files ?? []));
      if (best) return best;
    }
    // 4) aliases.
    if (this.aliases.size === 0) return null;
    const aliased = this.aliases.get(aliasKey(t));
    return aliased ? pickBest(aliased) : null;
  }

  /**
   * Exact path lookup (case-insensitive; `key` = lower-cased `p`), trying `.md` / `.markdown`
   * when the path has no such extension. Prefers the exact spelling among case variants.
   */
  private lookupPath(p: VaultPath, key: string): VaultPath | null {
    let set = this.byPath.get(key);
    if (!set && !key.endsWith('.md') && !key.endsWith('.markdown')) set = this.byPath.get(`${key}.md`) ?? this.byPath.get(`${key}.markdown`);
    if (!set) return null;
    if (set.size === 1) return set.values().next().value ?? null;
    for (const c of set) if (c === p || c === `${p}.md` || c === `${p}.markdown`) return c;
    return pickBest(set);
  }

  /** Base-name match (path-suffix match for targets with folders); `lower` = lower-cased target path. */
  private lookupName(lower: string): VaultPath | null {
    const ext = extname(lower);
    const md = isMdExt(ext);
    const hasDir = lower.includes('/');
    const notes = (): VaultPath | null => {
      const stem = md ? stripMdExtension(lower) : lower;
      const set = this.noteNames.get(hasDir ? basename(stem) : stem);
      if (!set) return null;
      if (!hasDir) return pickBest(set);
      return pickBest([...set].filter((p) => endsWithSegments(stripMdExtension(pathKey(p)), stem)));
    };
    const files = (): VaultPath | null => {
      if (md || this.fileNames.size === 0) return null;
      const set = this.fileNames.get(hasDir ? basename(lower) : lower);
      if (!set) return null;
      if (!hasDir) return pickBest(set);
      return pickBest([...set].filter((p) => endsWithSegments(pathKey(p), lower)));
    };
    // `![[photo.png]]`: attachments first; `[[Page]]` / `[[Dr. Smith]]`: notes first.
    return ext && !md && !/\s/.test(ext) ? (files() ?? notes()) : (notes() ?? files());
  }
}

/** Lower-cased, NFC folder of a path ("" at the root). */
function dirKey(path: VaultPath): string {
  const d = dirname(path);
  return (NON_ASCII_RE.test(d) ? d.normalize('NFC') : d).toLowerCase();
}

/** First path of `set` located directly in folder `dir` (a {@link dirKey}). */
function inDir(set: Set<VaultPath> | undefined, dir: string): VaultPath | null {
  if (!set) return null;
  let found: VaultPath | null = null;
  for (const p of set) {
    if (dirKey(p) !== dir) continue;
    if (found === null || compareCode(p, found) < 0) found = p;
  }
  return found;
}

/** Characters / sequences that need the full {@link normalizePath}. */
const NEEDS_NORMALIZE_RE = /[\\\u0080-\uffff]|\/\/|(?:^|\/)\.\.?(?:\/|$)|^\/|\/$/;

/** {@link normalizePath} with a fast path for plain ASCII relative paths. */
function fastNormalize(p: string): string {
  return NEEDS_NORMALIZE_RE.test(p) ? normalizePath(p) : p;
}

/** Does `path` equal `suffix` or end with "/" + `suffix`? */
function endsWithSegments(path: string, suffix: string): boolean {
  return path === suffix || path.endsWith(`/${suffix}`);
}
