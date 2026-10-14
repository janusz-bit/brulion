/**
 * Link target helpers: wiki link parsing, display texts, attachment kinds and URL classification.
 */
import { basename, stripMdExtension } from '@/core/paths';

/** Parts of a wiki link / embed `[[target#heading|display]]` / `[[target#^block]]`. */
export type WikiTarget = {
  /** Page name or path as written (empty for same-page links). */
  target: string;
  heading?: string;
  blockId?: string;
  display?: string;
};

/**
 * Parse the inside of `[[…]]`. `\|` (pipe escaped inside a table) separates the alias too.
 * Returns null for empty links (`[[]]`, `[[|x]]`, `[[#]]`).
 */
export function parseWikiTarget(inner: string): WikiTarget | null {
  let left = inner;
  let display: string | undefined;
  const pipe = inner.indexOf('|');
  if (pipe !== -1) {
    left = inner.slice(0, pipe);
    if (left.endsWith('\\')) left = left.slice(0, -1);
    display = inner.slice(pipe + 1).trim();
  }
  const hash = left.indexOf('#');
  const target = (hash === -1 ? left : left.slice(0, hash)).trim();
  const frag = hash === -1 ? '' : left.slice(hash + 1).trim();
  if (!target && !frag) return null;
  const out: WikiTarget = { target };
  if (display) out.display = display;
  if (frag.startsWith('^')) {
    const id = frag.slice(1).trim();
    if (id) out.blockId = id;
  } else if (frag) {
    out.heading = frag;
  }
  if (!out.target && !out.heading && !out.blockId) return null;
  return out;
}

/** Text shown for a wiki link: the alias, else `Page › Heading` / `Page › ^id` / `Page`. */
export function wikiDisplay(t: WikiTarget): string {
  if (t.display) return t.display;
  const base = t.target ? stripMdExtension(t.target) : '';
  const frag = t.heading ?? (t.blockId ? `^${t.blockId}` : '');
  if (!frag) return base;
  return base ? `${base} › ${frag}` : frag;
}

/** Kinds of non-note files an embed can show. */
export type AttachmentKind = 'image' | 'audio' | 'video' | 'pdf' | 'file';

const IMAGE_EXT = new Set(['png', 'jpg', 'jpeg', 'gif', 'webp', 'svg', 'avif', 'bmp']);
const AUDIO_EXT = new Set(['mp3', 'wav', 'm4a', 'ogg', 'oga', 'flac', 'opus', 'aac', '3gp']);
const VIDEO_EXT = new Set(['mp4', 'webm', 'ogv', 'mov', 'mkv', 'm4v']);
const NOTE_EXT = new Set(['md', 'markdown']);

/** Lower-case file extension without the dot, only when it looks like one (`[a-z0-9]{1,8}`). */
export function fileExtension(path: string): string {
  const name = basename(path.split('#')[0] ?? path);
  const dot = name.lastIndexOf('.');
  if (dot <= 0) return '';
  const ext = name.slice(dot + 1).toLowerCase();
  return /^[a-z0-9]{1,8}$/.test(ext) ? ext : '';
}

/**
 * Attachment kind of a target by its extension, or null for notes (no extension or `.md`).
 * Unknown extensions are generic files.
 */
export function attachmentKind(path: string): AttachmentKind | null {
  const ext = fileExtension(path);
  if (!ext || NOTE_EXT.has(ext)) return null;
  if (IMAGE_EXT.has(ext)) return 'image';
  if (AUDIO_EXT.has(ext)) return 'audio';
  if (VIDEO_EXT.has(ext)) return 'video';
  if (ext === 'pdf') return 'pdf';
  return 'file';
}

/** Image size from an embed alias: `100` (width) or `100x200`. */
export function parseDimensions(alias: string | undefined): { width: number; height?: number } | null {
  const m = /^\s*(\d{1,5})(?:\s*x\s*(\d{1,5}))?\s*$/i.exec(alias ?? '');
  if (!m) return null;
  const width = Number(m[1]);
  if (!(width > 0)) return null;
  const height = m[2] !== undefined ? Number(m[2]) : undefined;
  return height !== undefined && height > 0 ? { width, height } : { width };
}

const SCHEME_RE = /^([A-Za-z][A-Za-z0-9+.-]*):/;
const WINDOWS_DRIVE_RE = /^[A-Za-z]:[\\/]/;

/** URL scheme in lower case (`https`, `mailto`), or null for relative references. */
export function urlScheme(href: string): string | null {
  const s = href.trim();
  if (WINDOWS_DRIVE_RE.test(s)) return null;
  const m = SCHEME_RE.exec(s);
  return m ? m[1]!.toLowerCase() : null;
}

/** Absolute http(s) URL (opened in a new tab). */
export function isWebUrl(href: string): boolean {
  const scheme = urlScheme(href);
  return scheme === 'http' || scheme === 'https';
}

/** Decode %XX escapes; malformed sequences are kept as written. */
export function safeDecodeUri(s: string): string {
  if (s.indexOf('%') === -1) return s;
  try {
    return decodeURIComponent(s);
  } catch {
    return s.replace(/%[0-9A-Fa-f]{2}/g, (m) => {
      try {
        return decodeURIComponent(m);
      } catch {
        return m;
      }
    });
  }
}

/** Split `path#fragment` into target, heading and block id (`#^id`). */
export function splitFragment(ref: string): { target: string; heading?: string; blockId?: string } {
  const hash = ref.indexOf('#');
  if (hash === -1) return { target: ref };
  const target = ref.slice(0, hash);
  const frag = ref.slice(hash + 1);
  if (frag.startsWith('^')) return frag.length > 1 ? { target, blockId: frag.slice(1) } : { target };
  return frag ? { target, heading: frag } : { target };
}
