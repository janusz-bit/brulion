/**
 * Link rewriting when a page is renamed or moved (docs/SYNTAX.md §2).
 *
 * The links rewritten are exactly those {@link parseNote} reports (so never inside code,
 * comments or HTML blocks): wiki links and embeds (`[[Old]]`, `[[Old|alias]]`,
 * `[[Old#Heading]]`, `![[Old#^id]]`, `[[folder/Old]]`) and relative Markdown links
 * (`[x](Old.md)`, `[x](folder/Old%20Name.md#Heading)`). Alias, heading and block parts are kept.
 */
import { basename, dirname, joinPath, normalizePath, stripMdExtension } from '@/core/paths';
import type { Link, VaultPath } from '../types';
import { parseMdLink } from './inline';
import { parseNote } from './parse';

/** Comparison key: NFC, lower-case, `\` → `/`, trimmed. */
function key(s: string): string {
  return s.normalize('NFC').replace(/\\/g, '/').trim().toLowerCase();
}

/** What to match and what to write instead. */
interface RenameSpec {
  oldName: string;
  newName: string;
  oldPath?: VaultPath;
  oldNoExt?: string;
  newNoExt?: string;
  /** The page keeps its folder (a plain rename): path-qualified links keep their written prefix. */
  sameDir: boolean;
}

function makeSpec(oldName: string, newName: string, oldPath?: VaultPath, newPath?: VaultPath): RenameSpec {
  const spec: RenameSpec = { oldName, newName, sameDir: true };
  if (oldPath) {
    spec.oldPath = normalizePath(oldPath);
    spec.oldNoExt = stripMdExtension(spec.oldPath);
    spec.newNoExt = newPath ? stripMdExtension(normalizePath(newPath)) : joinPath(dirname(spec.oldPath), newName);
    spec.sameDir = key(dirname(spec.oldNoExt)) === key(dirname(spec.newNoExt));
  }
  return spec;
}

/** Index after the last path separator of a written link target (0 when there is none). */
function afterLastSlash(s: string): number {
  return Math.max(s.lastIndexOf('/'), s.lastIndexOf('\\')) + 1;
}

/**
 * New target for a path-qualified link that points at the renamed page: on a plain rename
 * only the last segment changes (so `b/Old` or `../b/Old` keep their written, possibly
 * relative, prefix); after a move the full new vault path is written.
 */
function renamedPath(written: string, spec: RenameSpec): string {
  if (spec.sameDir) return written.slice(0, afterLastSlash(written)) + basename(spec.newNoExt ?? spec.newName);
  return spec.newNoExt ?? spec.newName;
}

/** New wiki target for `target`, or null when it does not point at the renamed page. */
function newWikiTarget(target: string, spec: RenameSpec): string | null {
  const t = key(target);
  if (!t) return null;
  const trimmed = target.trim();
  const ext = /\.md$/i.test(trimmed) ? trimmed.slice(-3) : '';
  const bare = ext ? t.slice(0, -3) : t;
  if (bare === key(spec.oldName)) return spec.newName + ext;
  if (spec.oldNoExt && spec.newNoExt && bare.includes('/')) {
    const old = key(spec.oldNoExt);
    if (bare === old || old.endsWith('/' + bare)) return renamedPath(ext ? trimmed.slice(0, -3) : trimmed, spec) + ext;
  }
  return null;
}

/** URL-encode what would break a Markdown link destination (spaces, parentheses). */
function encodeDest(path: string): string {
  return path.replace(/ /g, '%20').replace(/\(/g, '%28').replace(/\)/g, '%29');
}

/** New Markdown link path for a decoded relative `target`, or null. */
function newMdTarget(target: string, spec: RenameSpec): string | null {
  const t = key(target).replace(/^\.\//, '');
  if (!t) return null;
  const ext = /\.md$/i.exec(target.trim())?.[0];
  if (!ext && /\.[a-z0-9]{1,5}$/i.test(t)) return null; // another file type
  const bare = ext ? t.slice(0, -ext.length) : t;
  if (!bare.includes('/')) return bare === key(spec.oldName) ? spec.newName + (ext ?? '') : null;
  if (spec.oldNoExt && spec.newNoExt) {
    const norm = key(normalizePath(bare));
    const old = key(spec.oldNoExt);
    if (norm === old || old.endsWith('/' + norm)) return spec.newNoExt + (ext ?? '');
  }
  return null;
}

/** Rewrite one wiki link / embed source (`[[…]]`, `![[…]]`). */
function rewriteWiki(src: string, spec: RenameSpec): string | null {
  const open = src.startsWith('!') ? 3 : 2;
  const inner = src.slice(open, -2);
  let end = inner.length;
  const hash = inner.indexOf('#');
  const pipe = inner.indexOf('|');
  if (hash !== -1) end = hash;
  if (pipe !== -1 && pipe < end) end = inner[pipe - 1] === '\\' ? pipe - 1 : pipe;
  const targetPart = inner.slice(0, end);
  const target = newWikiTarget(targetPart, spec);
  if (target === null) return null;
  const lead = targetPart.length - targetPart.trimStart().length;
  const trail = targetPart.length - targetPart.trimEnd().length;
  const newPart = targetPart.slice(0, lead) + target + targetPart.slice(targetPart.length - trail);
  return src.slice(0, open) + newPart + inner.slice(end) + ']]';
}

/** Rewrite one Markdown link source (`[x](dest)` / `![x](dest)`). */
function rewriteMarkdown(src: string, link: Link, spec: RenameSpec): string | null {
  if (/^[A-Za-z][A-Za-z0-9+.-]*:/.test(link.target) && !/^[A-Za-z]:[\\/]/.test(link.target)) return null;
  const target = newMdTarget(link.target, spec);
  if (target === null) return null;
  const parts = parseMdLink(src, src.startsWith('!') ? 1 : 0);
  if (!parts) return null;
  const dest = src.slice(parts.destFrom, parts.destTo);
  const hash = dest.indexOf('#');
  const pathPart = hash === -1 ? dest : dest.slice(0, hash);
  const fragment = hash === -1 ? '' : dest.slice(hash);
  const encode = (p: string): string => (parts.angle ? p : encodeDest(p));
  // A plain rename (or a bare-name link) keeps the written folder prefix, e.g. "../x/".
  const keepPrefix = spec.sameDir || afterLastSlash(pathPart) === 0;
  const newPath = keepPrefix ? pathPart.slice(0, afterLastSlash(pathPart)) + encode(basename(target)) : encode(target);
  return src.slice(0, parts.destFrom) + newPath + fragment + src.slice(parts.destTo);
}

/**
 * Rewrite links to a renamed page. `oldName`/`newName` are page names (file base names
 * without `.md`) matched case-insensitively; with `oldPath` (and `newPath`) path-qualified
 * links (`[[folder/Old]]`, `[x](folder/Old.md)`) are rewritten to the new vault path.
 * Everything except the link targets is preserved byte for byte.
 */
export function renameLinksInContent(
  content: string,
  oldName: string,
  newName: string,
  oldPath?: VaultPath,
  newPath?: VaultPath,
): string {
  if (!oldName.trim() || !newName.trim()) return content; // never write empty link targets
  if (oldName === newName && (!oldPath || !newPath || oldPath === newPath)) return content;
  const spec = makeSpec(oldName, newName, oldPath, newPath);
  const note = parseNote('', content);
  let out = '';
  let last = 0;
  for (const link of note.links) {
    if (link.from < last) continue;
    const src = content.slice(link.from, link.to);
    let rewritten: string | null = null;
    if (link.kind === 'wiki' || link.kind === 'embed') rewritten = rewriteWiki(src, spec);
    else if (link.kind === 'markdown') rewritten = rewriteMarkdown(src, link, spec);
    if (rewritten === null || rewritten === src) continue;
    out += content.slice(last, link.from) + rewritten;
    last = link.to;
  }
  return last === 0 ? content : out + content.slice(last);
}
