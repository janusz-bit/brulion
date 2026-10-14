/**
 * Transclusions and attachments: `![[Page]]`, `![[Page#Heading]]`, `![[Page#^id]]`,
 * `!((id))`, `![[photo.png|300]]`, `![alt](path)`. Resolution helpers are pure (embeds.test.ts);
 * `EmbedWidget` renders the result (Markdown through render.ts, sanitised).
 */
import type { EditorView } from '@codemirror/view';
import { basename, dirname, extname, joinPath, normalizePath, pageTitle, stripMdExtension } from '@/core/paths';
import type { Block, VaultConfig, VaultPath } from '@/core/types';
import { editorContext, type EditorContext } from '../context';
import { h, icon, sourceSpan, type PreviewIcon } from './dom';
import { pageHref, plainInline, renderMarkdownSafe, tagHref, type RenderContext } from './render';
import { PreviewWidget, remeasure } from './widgets';

export interface EmbedTarget {
  /** Page / file part ('' for same-page targets). */
  page: string;
  heading?: string;
  blockId?: string;
  /** Text after `|`. */
  alias?: string;
}

/** Split `Page#Heading|alias` / `Page#^id`. */
export function parseEmbedTarget(raw: string): EmbedTarget {
  const pipe = raw.indexOf('|');
  const target = (pipe >= 0 ? raw.slice(0, pipe) : raw).trim();
  const alias = pipe >= 0 ? raw.slice(pipe + 1).trim() : undefined;
  const hash = target.indexOf('#');
  const page = (hash >= 0 ? target.slice(0, hash) : target).trim();
  const out: EmbedTarget = { page };
  if (hash >= 0) {
    const frag = target.slice(hash + 1).trim();
    if (frag.startsWith('^')) out.blockId = frag.slice(1);
    else if (frag) out.heading = frag;
  }
  if (alias) out.alias = alias;
  return out;
}

export type MediaKind = 'image' | 'audio' | 'video' | 'pdf' | 'file' | 'note';

const IMAGE = new Set(['.png', '.jpg', '.jpeg', '.gif', '.webp', '.svg', '.bmp', '.avif', '.ico']);
const AUDIO = new Set(['.mp3', '.wav', '.ogg', '.oga', '.m4a', '.flac', '.aac', '.opus', '.weba']);
const VIDEO = new Set(['.mp4', '.webm', '.mov', '.mkv', '.ogv', '.m4v']);

/** What an embed target is, from its extension (no extension or `.md` = a note). */
export function mediaKind(name: string): MediaKind {
  const ext = extname(name).toLowerCase();
  if (!ext || ext === '.md') return 'note';
  if (IMAGE.has(ext)) return 'image';
  if (AUDIO.has(ext)) return 'audio';
  if (VIDEO.has(ext)) return 'video';
  if (ext === '.pdf') return 'pdf';
  return 'file';
}

/** `![[x.png|300]]` / `|300x200` sizes; null when the alias is not a size. */
export function imageSize(alias: string | undefined): { width: number; height?: number } | null {
  const m = /^(\d{1,4})(?:x(\d{1,4}))?$/.exec(alias?.trim() ?? '');
  if (!m) return null;
  return m[2] ? { width: Number(m[1]), height: Number(m[2]) } : { width: Number(m[1]) };
}

/**
 * Candidate vault paths of an attachment link, most specific first: as written (vault path),
 * relative to the note's folder, in the attachments folder, then by bare name there.
 */
export function assetCandidates(target: string, from: VaultPath, config: Pick<VaultConfig, 'attachmentsFolder'>): VaultPath[] {
  let t = target.trim();
  try {
    t = decodeURI(t);
  } catch {
    // keep as written
  }
  t = t.replace(/^\.\//, '');
  const out: VaultPath[] = [];
  const add = (p: string) => {
    const n = normalizePath(p);
    if (n && !out.includes(n)) out.push(n);
  };
  add(t.replace(/^\/+/, ''));
  const dir = dirname(from);
  if (dir && dir !== '.') add(joinPath(dir, t));
  if (config.attachmentsFolder) {
    add(joinPath(config.attachmentsFolder, t));
    add(joinPath(config.attachmentsFolder, basename(t)));
  }
  return out;
}

/** Lines of a heading's section (heading line included, up to the next heading of the same or higher level). */
export function headingSection(content: string, heading: string): string | null {
  const lines = content.split('\n');
  const wanted = heading.trim().toLowerCase();
  let start = -1;
  let level = 0;
  let fence: string | null = null;
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i]!;
    const f = /^\s*(`{3,}|~{3,})/.exec(line);
    if (f) {
      if (fence === null) fence = f[1]![0]!;
      else if (f[1]![0] === fence) fence = null;
      continue;
    }
    if (fence !== null) continue;
    const m = /^ {0,3}(#{1,6})[ \t]+(.*?)[ \t#]*$/.exec(line);
    if (!m) continue;
    if (start < 0) {
      if (m[2]!.trim().toLowerCase() === wanted) {
        start = i;
        level = m[1]!.length;
      }
    } else if (m[1]!.length <= level) {
      return lines.slice(start, i).join('\n').trimEnd();
    }
  }
  return start < 0 ? null : lines.slice(start).join('\n').trimEnd();
}

/** Remove the common leading indentation of lines. */
export function dedent(text: string): string {
  const lines = text.split('\n');
  let min = Infinity;
  for (const l of lines) {
    if (!l.trim()) continue;
    min = Math.min(min, /^[ \t]*/.exec(l)![0].length);
  }
  if (!Number.isFinite(min) || min === 0) return text;
  return lines.map((l) => l.slice(Math.min(min, /^[ \t]*/.exec(l)![0].length))).join('\n');
}

/** A block with its children (lines `block.line..block.endLine`), dedented. */
export function blockSource(content: string, block: Pick<Block, 'line' | 'endLine'>): string {
  const lines = content.split('\n').slice(block.line, block.endLine + 1);
  return dedent(lines.join('\n')).replace(/[ \t]\^[A-Za-z0-9-]+[ \t]*$/m, '').trimEnd();
}

/** Strip YAML frontmatter from a note's content. */
export function withoutFrontmatter(content: string): string {
  return content.replace(/^---\r?\n[\s\S]*?\r?\n(?:---|\.\.\.)[ \t]*(?:\r?\n|$)/, '');
}

// ---------------------------------------------------------------------------------------------
// Embed model (computed by the decoration builder, rendered by the widget)
// ---------------------------------------------------------------------------------------------

export type EmbedSpec =
  | {
      kind: 'note';
      /** Header text: page title (› heading). */
      title: string;
      /** Link target opened by the header. */
      target: string;
      /** Markdown to render (null: target missing). */
      markdown: string | null;
      /** Vault path of the embedded note (for relative links). */
      path: VaultPath | null;
      missingText: string;
      openLabel: string;
    }
  | {
      kind: 'image' | 'audio' | 'video' | 'pdf' | 'file';
      name: string;
      src: string | null;
      alt: string;
      width?: number;
      height?: number;
      missingText: string;
      openLabel: string;
    };

/** Embed of a note / section / block, resolved through the editor context. */
export function noteEmbedSpec(ctx: EditorContext | null, target: EmbedTarget, labels: { missing: string; open: string }): EmbedSpec {
  const pagePart = target.page;
  const titleBase = pagePart ? pageTitle(pagePart) : ctx ? pageTitle(ctx.path) : '';
  const suffix = target.heading ? ` › ${target.heading}` : target.blockId ? ` › ^${target.blockId}` : '';
  const linkTarget = `${pagePart}${target.heading ? `#${target.heading}` : target.blockId ? `#^${target.blockId}` : ''}`;
  const base = { kind: 'note' as const, title: (target.alias || titleBase) + (target.alias ? '' : suffix), target: linkTarget || (ctx?.path ?? ''), openLabel: labels.open };
  if (!ctx) return { ...base, markdown: null, path: null, missingText: labels.missing };
  let path: VaultPath | null;
  try {
    path = pagePart ? ctx.workspace.index.resolveLink(pagePart, ctx.path) : ctx.path;
  } catch {
    path = null;
  }
  const markdown = path ? embedMarkdown(ctx, path, target.heading, target.blockId) : null;
  return { ...base, markdown, path, missingText: labels.missing };
}

/** `!((id))`: the block (with children) from anywhere in the vault. */
export function blockEmbedSpec(ctx: EditorContext | null, id: string, labels: { missing: string; open: string }): EmbedSpec {
  const ref = ctx?.workspace.index.blockById(id) ?? null;
  const content = ref ? ctx!.workspace.index.content(ref.path) : undefined;
  const markdown = ref && content !== undefined ? blockSource(content, ref.block) : null;
  const title = ref ? `${pageTitle(ref.path)} › ${plainInline(ref.block.text, 60)}` : `((${id}))`;
  return {
    kind: 'note',
    title,
    target: ref ? `${ref.path.replace(/\.md$/i, '')}#^${id}` : '',
    markdown,
    path: ref?.path ?? null,
    missingText: labels.missing,
    openLabel: labels.open,
  };
}

/** Attachment embed: resolves the file and its URL. */
export function attachmentSpec(
  ctx: EditorContext | null,
  name: string,
  alias: string | undefined,
  labels: { missing: string; open: string },
): EmbedSpec {
  const kind = mediaKind(name);
  const k = kind === 'note' ? 'file' : kind;
  let src: string | null = null;
  if (/^(https?:|data:image\/)/i.test(name)) src = name;
  else if (ctx) {
    for (const p of assetCandidates(name, ctx.path, ctx.workspace.config)) {
      const url = ctx.assetUrl(p);
      if (url) {
        src = url;
        break;
      }
    }
  }
  const size = imageSize(alias);
  const spec: EmbedSpec = { kind: k, name: basename(name) || name, src, alt: size ? '' : (alias ?? ''), missingText: labels.missing, openLabel: labels.open };
  if (size) {
    spec.width = size.width;
    if (size.height) spec.height = size.height;
  }
  return spec;
}

function sameSpec(a: EmbedSpec, b: EmbedSpec): boolean {
  if (a.kind !== b.kind) return false;
  if (a.kind === 'note' && b.kind === 'note') {
    return a.title === b.title && a.target === b.target && a.markdown === b.markdown && a.path === b.path && a.missingText === b.missingText;
  }
  if (a.kind !== 'note' && b.kind !== 'note') {
    return a.name === b.name && a.src === b.src && a.alt === b.alt && a.width === b.width && a.height === b.height && a.missingText === b.missingText;
  }
  return false;
}

const MEDIA_ICON: Record<string, PreviewIcon> = { image: 'image', audio: 'music', video: 'video', pdf: 'file', file: 'file' };

/**
 * The Markdown of an embed target: a block with its children, a heading's section or the whole
 * note without frontmatter; null when the note / block / heading does not exist.
 */
export function embedMarkdown(ctx: EditorContext, path: VaultPath, heading?: string, blockId?: string): string | null {
  const index = ctx.workspace.index;
  const content = index.content(path);
  if (content === undefined) return null;
  if (blockId) {
    const block = index.note(path)?.blockIds[blockId] ?? (index.blockById(blockId)?.path === path ? index.blockById(blockId)!.block : undefined);
    return block ? blockSource(content, block) : null;
  }
  if (heading) return headingSection(content.replace(/\r\n?/g, '\n'), heading);
  return withoutFrontmatter(content).trim();
}

/** Href of a rendered link: tags, blocks and pages (by resolved path when known). */
export function renderHref(path: VaultPath | null, target: string, heading?: string, blockId?: string): string {
  if (path === null && target.startsWith('#') && !heading && !blockId) return tagHref(target);
  const page = path ? stripMdExtension(path) : target;
  return pageHref(`${page}${heading ? `#${heading}` : ''}${blockId ? `#^${blockId}` : ''}`);
}

/**
 * Render context for Markdown shown inside the editor (transclusions, table cells, query
 * results): links resolve through the vault from `path`; nested embeds render recursively
 * (core render limits the depth and breaks cycles).
 */
export function renderContextFor(ctx: EditorContext | null, path: VaultPath | null, depth = 0): RenderContext {
  const from = path ?? ctx?.path ?? '';
  const rc: RenderContext = {
    path: from,
    locale: ctx?.locale ?? 'pl',
    depth,
    idPrefix: `lp${depth}-`,
    resolveLink: (target, f) => {
      try {
        return ctx?.workspace.index.resolveLink(target, f) ?? null;
      } catch {
        return null;
      }
    },
    href: renderHref,
    assetUrl: (p) => {
      if (!ctx) return null;
      if (/^(https?:|data:image\/)/i.test(p)) return p;
      for (const c of assetCandidates(p, from, ctx.workspace.config)) {
        const url = ctx.assetUrl(c);
        if (url) return url;
      }
      return null;
    },
    blockRef: (id) => {
      const ref = ctx?.workspace.index.blockById(id);
      return ref ? { path: ref.path, text: ref.block.text } : null;
    },
  };
  if (ctx) {
    rc.now = ctx.now();
    rc.embed = (target, heading, blockId, d) => {
      const markdown = embedMarkdown(ctx, target, heading, blockId);
      return markdown === null ? null : renderMarkdownSafe(markdown, renderContextFor(ctx, target, d));
    };
  }
  return rc;
}

/**
 * The embed box. `inline` (default) replaces/accompanies the `![[…]]` source inside its line
 * as a full-width inline-block.
 */
export class EmbedWidget extends PreviewWidget {
  constructor(
    readonly raw: string,
    readonly spec: EmbedSpec,
  ) {
    super();
  }

  override eq(o: EmbedWidget): boolean {
    return o.raw === this.raw && sameSpec(o.spec, this.spec);
  }

  override get estimatedHeight(): number {
    return this.spec.kind === 'image' ? 200 : this.spec.kind === 'note' ? 120 : 48;
  }

  toDOM(view: EditorView): HTMLElement {
    const s = this.spec;
    const el = h('span', { class: `b-lp-embed b-lp-embed--${s.kind}`, 'data-lp-root': '', 'data-lp': 'embed' });
    if (this.raw) el.appendChild(sourceSpan(this.raw));
    if (s.kind === 'note') {
      const head = h(
        'span',
        { class: 'b-lp-embed__head' },
        h('span', { class: 'b-lp-embed__title', 'data-lp': 'open', 'data-target': s.target, title: s.openLabel }, icon('file', 'b-lp-icon'), h('span', {}, s.title)),
        h('span', { class: 'b-lp-embed__open', 'data-lp': 'open', 'data-target': s.target, title: s.openLabel, 'aria-label': s.openLabel }, icon('arrowRight', 'b-lp-icon')),
      );
      el.appendChild(head);
      const body = h('span', { class: 'b-lp-embed__body b-lp-rendered' });
      if (s.markdown === null) {
        body.classList.add('is-missing');
        body.textContent = s.missingText;
      } else {
        const ctx = view.state.facet(editorContext);
        body.innerHTML = renderMarkdownSafe(s.markdown, renderContextFor(ctx, s.path, 1));
      }
      el.appendChild(body);
      return el;
    }
    if (!s.src) {
      el.classList.add('is-missing');
      el.append(icon(MEDIA_ICON[s.kind] ?? 'file', 'b-lp-icon'), h('span', { class: 'b-lp-embed__name' }, s.missingText));
      return el;
    }
    switch (s.kind) {
      case 'image': {
        const img = h('img', { src: s.src, alt: s.alt || s.name, loading: 'lazy', decoding: 'async', draggable: 'false' });
        if (s.width) img.style.width = `${s.width}px`;
        if (s.height) img.style.height = `${s.height}px`;
        img.addEventListener('load', () => remeasure(view));
        img.addEventListener('error', () => {
          el.classList.add('is-missing');
          img.remove();
          el.append(icon('image', 'b-lp-icon'), h('span', { class: 'b-lp-embed__name' }, s.missingText));
          remeasure(view);
        });
        el.appendChild(img);
        return el;
      }
      case 'audio': {
        const audio = h('audio', { src: s.src, controls: true, preload: 'metadata' });
        el.appendChild(audio);
        return el;
      }
      case 'video': {
        const video = h('video', { src: s.src, controls: true, preload: 'metadata', playsinline: true });
        if (s.width) video.style.width = `${s.width}px`;
        video.addEventListener('loadedmetadata', () => remeasure(view));
        el.appendChild(video);
        return el;
      }
      default:
        el.append(
          h('span', { class: 'b-lp-embed__file' }, icon(MEDIA_ICON[s.kind] ?? 'file', 'b-lp-icon'), h('span', { class: 'b-lp-embed__name' }, s.name)),
          h('span', { class: 'b-lp-embed__open', 'data-lp': 'open-asset', 'data-src': s.src, title: s.openLabel, 'aria-label': s.openLabel }, icon('external', 'b-lp-icon')),
        );
        return el;
    }
  }
}

