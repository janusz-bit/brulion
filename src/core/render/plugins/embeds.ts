/**
 * Embeds (SYNTAX §2): `![[photo.png]]`, `![[audio.mp3]]`, `![[film.mp4]]`, `![[doc.pdf]]`,
 * `![[Note]]`, `![[Note#Heading]]`, `![[Note#^block]]` and Logseq block embeds `!((id))`.
 *
 * * Attachments (by extension) render inline: `<img>`, `<audio controls>`, `<video controls>`,
 *   `<a class="embed-pdf">`, `<a class="embed-file">`; the URL comes from `ctx.assetUrl`.
 * * Notes / sections / blocks render as `<div class="embed" data-target>` boxes containing the
 *   HTML from `ctx.embed`. A paragraph containing such an embed is split around it so the box
 *   is never nested in `<p>`. Embeds deeper than {@link MAX_EMBED_DEPTH} or embedding a note
 *   that is already being rendered (a cycle) show a placeholder instead.
 * * Missing targets render a placeholder with the class `is-unresolved`.
 */
import type { Env, MarkdownIt, RendererRule, StateCore, Token } from 'markdown-it';
import { basename, isMarkdown, normalizePath } from '@/core/paths';
import { classNames, escapeHtml, renderAttrPairs, renderAttrs, type AttrValue } from '../escape';
import {
  MAX_EMBED_DEPTH,
  assetUrl,
  blockEmbedKey,
  getEnv,
  isEmbedInProgress,
  linkHref,
  noteEmbedKey,
  resolveLink,
  safeCall,
  tr,
  withEmbedKey,
} from '../env';
import { attachmentKind, parseDimensions, wikiDisplay, type AttachmentKind } from '../targets';
import type { RenderEnv } from '../types';
import { lookupBlock, renderBlockText, type BlockRefMeta } from './blockrefs';
import type { WikiMeta } from './wikilinks';

/** What an `embed` token shows. */
export type EmbedKind = AttachmentKind | 'note';

/** Meta of `embed` tokens after resolution. */
export type ResolvedEmbedMeta = WikiMeta & { kind: EmbedKind; resolved: string | null };

/** Meta of `embed_block` tokens (block-level note / block embeds). */
export type EmbedBlockMeta = { embed: 'note'; wiki: ResolvedEmbedMeta } | { embed: 'block'; id: string };

/** Classify and resolve a `![[…]]` embed. */
export function resolveEmbed(env: RenderEnv, meta: WikiMeta): ResolvedEmbedMeta {
  if (!meta.target) return { ...meta, kind: 'note', resolved: env.ctx.path };
  const resolved = resolveLink(env, meta.target);
  const byExt = attachmentKind(meta.target);
  let kind: EmbedKind;
  if (resolved && isMarkdown(resolved)) kind = 'note';
  else if (byExt) kind = byExt;
  else if (resolved) kind = attachmentKind(resolved) ?? 'note';
  else kind = 'note';
  return { ...meta, kind, resolved };
}

// ---------------------------------------------------------------------------------------------
// Core rule: resolve embeds, lift note/block embeds out of paragraphs
// ---------------------------------------------------------------------------------------------

function isBlockLevel(child: Token): boolean {
  if (child.type === 'block_embed') return true;
  return child.type === 'embed' && (child.meta as ResolvedEmbedMeta | null)?.kind === 'note';
}

function isBlankSegment(children: readonly Token[]): boolean {
  return children.every(
    (c) => c.type === 'softbreak' || c.type === 'hardbreak' || (c.type === 'text' && c.content.trim() === ''),
  );
}

/** Drop leading/trailing line breaks of a paragraph segment. */
function trimBreaks(children: Token[]): Token[] {
  let a = 0;
  let b = children.length;
  while (a < b && (children[a]!.type === 'softbreak' || children[a]!.type === 'hardbreak')) a++;
  while (b > a && (children[b - 1]!.type === 'softbreak' || children[b - 1]!.type === 'hardbreak')) b--;
  return children.slice(a, b);
}

function embedBlockToken(state: StateCore, child: Token, para: Token): Token {
  const token = new state.Token('embed_block', 'div', 0);
  token.block = true;
  token.level = para.level;
  token.map = para.map ? [para.map[0], para.map[1]] : null;
  token.meta =
    child.type === 'block_embed'
      ? ({ embed: 'block', id: (child.meta as BlockRefMeta | null)?.id ?? child.content } satisfies EmbedBlockMeta)
      : ({ embed: 'note', wiki: child.meta as ResolvedEmbedMeta } satisfies EmbedBlockMeta);
  return token;
}

/** Split paragraph `tokens[i-1..i+1]` around its block-level embeds. */
function splitParagraph(state: StateCore, open: Token, inline: Token, close: Token): Token[] {
  const out: Token[] = [];
  const children = inline.children ?? [];
  let segment: Token[] = [];
  let first = true;
  const flush = (): void => {
    const seg = trimBreaks(segment);
    segment = [];
    const keep = !isBlankSegment(seg) || (first && inline.meta?.task !== undefined);
    if (keep) {
      if (first) {
        inline.children = seg;
        out.push(open, inline, close);
      } else {
        const p = new state.Token('paragraph_open', 'p', 1);
        p.block = true;
        p.level = open.level;
        p.hidden = open.hidden;
        p.map = open.map;
        const inl = new state.Token('inline', '', 0);
        inl.level = inline.level;
        inl.map = inline.map;
        inl.children = seg;
        const pc = new state.Token('paragraph_close', 'p', -1);
        pc.block = true;
        pc.level = close.level;
        pc.hidden = close.hidden;
        out.push(p, inl, pc);
      }
    }
    first = false;
  };
  for (const child of children) {
    if (isBlockLevel(child)) {
      flush();
      out.push(embedBlockToken(state, child, open));
    } else {
      segment.push(child);
    }
  }
  flush();
  return out;
}

/** The `embed_blocks` core rule. */
export function embedBlocksRule(state: StateCore): void {
  const env = getEnv(state.env);
  if (!env) return;
  const tokens = state.tokens;
  let out: Token[] | null = null;
  for (let i = 0; i < tokens.length; i++) {
    const tok = tokens[i]!;
    let hasBlock = false;
    if (tok.type === 'inline' && tok.children) {
      for (const child of tok.children) {
        if (child.type === 'embed' && child.meta) child.meta = resolveEmbed(env, child.meta as WikiMeta);
        if (isBlockLevel(child)) hasBlock = true;
      }
    }
    const open = tokens[i - 1];
    const close = tokens[i + 1];
    if (hasBlock && open?.type === 'paragraph_open' && close?.type === 'paragraph_close') {
      // `open` is re-added by splitParagraph.
      if (out === null) out = tokens.slice(0, i - 1);
      else out.pop();
      for (const t of splitParagraph(state, open, tok, close)) out.push(t);
      i++;
      continue;
    }
    out?.push(tok);
  }
  if (out) state.tokens = out;
}

// ---------------------------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------------------------

/** Merge base attributes with markdown-it attribute pairs (classes are joined). */
function mergeAttrs(base: Record<string, AttrValue>, extra: Token['attrs']): string {
  const attrs: Record<string, AttrValue> = { ...base };
  const rest: [string, string | number][] = [];
  for (const [name, value] of extra ?? []) {
    if (name === 'class') attrs.class = classNames(String(attrs.class ?? ''), String(value));
    else if (!(name in attrs)) rest.push([name, value]);
  }
  return renderAttrs(attrs) + renderAttrPairs(rest);
}

function placeholder(env: RenderEnv, meta: WikiMeta, kind: 'missing' | 'cycle' | 'too-deep', extra: Token['attrs'], blockId?: string): string {
  const key = kind === 'missing' ? 'render.embed.missing' : kind === 'cycle' ? 'render.embed.cycle' : 'render.embed.tooDeep';
  const label = wikiDisplay(meta) || (blockId ? `((${blockId}))` : '');
  const resolved = kind === 'missing' ? null : (meta.target ? resolveLink(env, meta.target) : env.ctx.path);
  const href = linkHref(env, resolved, meta.target, meta.heading, meta.blockId ?? blockId);
  const attrs = mergeAttrs(
    {
      class: classNames('embed', `embed-${kind}`, kind === 'missing' && 'is-unresolved'),
      'data-target': meta.target,
      'data-heading': meta.heading,
      'data-block-id': meta.blockId ?? blockId,
    },
    extra,
  );
  const link = `<a${renderAttrs({ class: classNames('wikilink', kind === 'missing' && 'is-unresolved'), 'data-target': meta.target, href })}>${escapeHtml(label)}</a>`;
  return `<div${attrs}><span class="embed-message">${escapeHtml(tr(env, key, { target: label }))}</span> ${link}</div>`;
}

/** HTML of an attachment embed (image, audio, video, pdf, other file). */
export function renderAttachment(env: RenderEnv, meta: ResolvedEmbedMeta, kind: AttachmentKind): string {
  const path = meta.resolved ?? normalizePath(meta.target);
  const url = path ? assetUrl(env, path) : null;
  const name = basename(meta.target) || meta.target;
  if (!url) {
    const label = meta.display && !parseDimensions(meta.display) ? meta.display : name;
    return `<span${renderAttrs({ class: 'embed embed-missing is-unresolved', 'data-target': meta.target })}>${escapeHtml(tr(env, 'render.embed.missing', { target: label }))}</span>`;
  }
  switch (kind) {
    case 'image': {
      const dims = parseDimensions(meta.display);
      const alt = dims ? name : (meta.display ?? name);
      return `<img${renderAttrs({ class: 'embed-image', src: url, alt, width: dims?.width, height: dims?.height, 'data-target': meta.target, loading: 'lazy' })}>`;
    }
    case 'audio':
      return `<audio${renderAttrs({ class: 'embed-audio', controls: true, preload: 'metadata', src: url, 'data-target': meta.target })}></audio>`;
    case 'video': {
      const dims = parseDimensions(meta.display);
      return `<video${renderAttrs({ class: 'embed-video', controls: true, preload: 'metadata', src: url, width: dims?.width, height: dims?.height, 'data-target': meta.target })}></video>`;
    }
    case 'pdf': {
      const href = meta.heading ? `${url}#${meta.heading}` : url;
      const label = meta.display ?? name;
      return `<a${renderAttrs({ class: 'embed-pdf', href, 'data-target': meta.target, title: meta.target })}>📄 ${escapeHtml(label)}</a>`;
    }
    case 'file':
      return `<a${renderAttrs({ class: 'embed-file', href: url, 'data-target': meta.target, title: meta.target })}>📎 ${escapeHtml(meta.display ?? name)}</a>`;
  }
}

/** HTML of a note / section embed box. */
export function renderNoteEmbed(env: RenderEnv, meta: ResolvedEmbedMeta, extra: Token['attrs'] = null): string {
  const resolved = meta.resolved;
  if (!resolved) return placeholder(env, meta, 'missing', extra);
  const key = meta.blockId ? blockEmbedKey(meta.blockId) : noteEmbedKey(resolved, meta.heading);
  if (isEmbedInProgress(key)) return placeholder(env, meta, 'cycle', extra);
  const depth = env.depth + 1;
  if (depth > MAX_EMBED_DEPTH) return placeholder(env, meta, 'too-deep', extra);
  const embed = env.ctx.embed;
  let content = '';
  if (embed) {
    const html = withEmbedKey(key, () => safeCall(() => embed.call(env.ctx, resolved, meta.heading, meta.blockId, depth), null));
    if (typeof html !== 'string') return placeholder(env, meta, 'missing', extra);
    content = `<div class="embed-content">${html}</div>`;
  }
  const base = {
    // Without `ctx.embed` the box only links to the embedded note.
    class: classNames('embed', 'embed-note', !embed && 'embed-link-only'),
    'data-target': meta.target,
    'data-path': resolved,
    'data-heading': meta.heading,
    'data-block-id': meta.blockId,
  };
  return `<div${mergeAttrs(base, extra)}><div class="embed-title">${linkTo(env, meta, resolved)}</div>${content}</div>`;
}

function linkTo(env: RenderEnv, meta: WikiMeta, resolved: string): string {
  const href = linkHref(env, resolved, meta.target, meta.heading, meta.blockId);
  const attrs = renderAttrs({ class: 'wikilink embed-link', 'data-target': meta.target, href });
  return `<a${attrs}>${escapeHtml(wikiDisplay(meta) || basename(resolved))}</a>`;
}

/** HTML of a Logseq block embed `!((id))`. */
export function renderBlockEmbed(md: MarkdownIt, mdEnv: Env, env: RenderEnv, id: string, extra: Token['attrs'] = null): string {
  const meta: WikiMeta = { target: '', blockId: id, raw: `!((${id}))` };
  const ref = lookupBlock(env.ctx, id);
  if (!ref) return placeholder(env, meta, 'missing', extra, id);
  const key = blockEmbedKey(id);
  if (isEmbedInProgress(key)) return placeholder(env, { ...meta, target: ref.path }, 'cycle', extra, id);
  const depth = env.depth + 1;
  if (depth > MAX_EMBED_DEPTH) return placeholder(env, { ...meta, target: ref.path }, 'too-deep', extra, id);
  const embed = env.ctx.embed;
  let html = embed ? withEmbedKey(key, () => safeCall(() => embed.call(env.ctx, ref.path, undefined, id, depth), null)) : null;
  if (typeof html !== 'string') html = `<p>${withEmbedKey(key, () => renderBlockText(md, mdEnv, env, id, ref.text))}</p>`;
  const base = { class: 'embed embed-block', 'data-id': id, 'data-path': ref.path };
  return `<div${mergeAttrs(base, extra)}><div class="embed-content">${html}</div></div>`;
}

/** Install embed resolution and rendering. */
export function embedsPlugin(md: MarkdownIt): void {
  md.core.ruler.before('linkify', 'embed_blocks', embedBlocksRule);
  const renderInlineEmbed: RendererRule = (tokens, idx, _options, mdEnv) => {
    const token = tokens[idx]!;
    const env = getEnv(mdEnv);
    if (!env || !token.meta) return escapeHtml(`![[${token.content}]]`);
    const meta = 'kind' in token.meta ? (token.meta as ResolvedEmbedMeta) : resolveEmbed(env, token.meta as WikiMeta);
    return meta.kind === 'note' ? renderNoteEmbed(env, meta) : renderAttachment(env, meta, meta.kind);
  };
  const renderBlockEmbedToken: RendererRule = (tokens, idx, _options, mdEnv) => {
    const token = tokens[idx]!;
    const env = getEnv(mdEnv);
    const id = (token.meta as BlockRefMeta | null)?.id ?? token.content;
    if (!env || !mdEnv) return escapeHtml(`!((${id}))`);
    return renderBlockEmbed(md, mdEnv, env, id);
  };
  const renderEmbedBlock: RendererRule = (tokens, idx, _options, mdEnv) => {
    const token = tokens[idx]!;
    const env = getEnv(mdEnv);
    const meta = token.meta as EmbedBlockMeta | null;
    if (!env || !meta || !mdEnv) return '';
    const html = meta.embed === 'block' ? renderBlockEmbed(md, mdEnv, env, meta.id, token.attrs) : renderNoteEmbed(env, meta.wiki, token.attrs);
    return `${html}\n`;
  };
  md.renderer.rules.embed = renderInlineEmbed;
  md.renderer.rules.block_embed = renderBlockEmbedToken;
  md.renderer.rules.embed_block = renderEmbedBlock;
}
