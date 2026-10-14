/** Small path helpers used by the storage adapters and the Vault (all pure). */
import type { VaultPath } from '../types';

/** Ancestor directories, outermost first: "a/b/c.md" -> ["a", "a/b"]; "c.md" -> []. */
export function ancestors(path: VaultPath): VaultPath[] {
  const out: VaultPath[] = [];
  let i = path.indexOf('/');
  while (i !== -1) {
    out.push(path.slice(0, i));
    i = path.indexOf('/', i + 1);
  }
  return out;
}

/** True if `path` equals `dir` or lies inside it. `dir === ''` (root) contains everything. */
export function isWithin(path: VaultPath, dir: VaultPath): boolean {
  return dir === '' || path === dir || path.startsWith(`${dir}/`);
}

/** True if `path` lies strictly inside `dir`. */
export function isStrictlyWithin(path: VaultPath, dir: VaultPath): boolean {
  return dir === '' ? path !== '' : path.startsWith(`${dir}/`);
}

/** Re-root `path` from `from` to `to`: rebase("a/x/y.md", "a", "b") -> "b/x/y.md". */
export function rebase(path: VaultPath, from: VaultPath, to: VaultPath): VaultPath {
  if (path === from) return to;
  const rest = path.slice(from.length + 1);
  return to === '' ? rest : `${to}/${rest}`;
}

/** Insert `suffix` before the extension of the last segment ("a/b.md", " 2" -> "a/b 2.md"). */
export function insertBeforeExtension(path: VaultPath, suffix: string, isDir = false): VaultPath {
  if (isDir) return path + suffix;
  const slash = path.lastIndexOf('/');
  const dot = path.lastIndexOf('.');
  if (dot <= slash + 1) return path + suffix;
  return path.slice(0, dot) + suffix + path.slice(dot);
}
