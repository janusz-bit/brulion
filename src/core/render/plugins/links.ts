/**
 * Markdown links and images:
 *
 * * external links (`https:`, `mailto:`...) get `class="external-link"`; web links open in a
 *   new tab (`target="_blank" rel="noopener noreferrer"`);
 * * relative links to notes (`[x](Folder/Page.md)`, `[x](Page%20Name.md#Heading)`) resolve
 *   like wiki links (`class="wikilink md-link"`, `is-unresolved` when missing, href from
 *   `ctx.href`); relative links to attachments use `ctx.assetUrl`; `[x](#Heading)` links to a
 *   heading of the current note;
 * * relative image sources are resolved through `ctx.assetUrl`; Obsidian's size suffix in the alt
 *   text (`![alt|200](x.png)`, `|200x100`) sets the size.
 */
import MarkdownItCtor from 'markdown-it';
import type { MarkdownIt, RendererRule, Token } from 'markdown-it';
import { getEnv, assetUrl, linkHref, resolveLink } from '../env';
import { attachmentKind, isWebUrl, parseDimensions, safeDecodeUri, splitFragment, urlScheme } from '../targets';
import type { RenderEnv } from '../types';

function addClass(token: Token, cls: string): void {
  token.attrJoin('class', cls);
}

/** Rewrite a `link_open` token according to the link policy. */
export function applyLinkPolicy(token: Token, env: RenderEnv): void {
  const href = String(token.attrGet('href') ?? '');
  if (!href) return;
  if (urlScheme(href) !== null) {
    addClass(token, 'external-link');
    if (isWebUrl(href)) {
      token.attrSet('target', '_blank');
      token.attrSet('rel', 'noopener noreferrer');
    }
    return;
  }
  const decoded = safeDecodeUri(href);
  const { target, heading, blockId } = splitFragment(decoded);
  if (!target) {
    // `#Heading` / `#^block` on the current page.
    if (!heading && !blockId) return;
    token.attrSet('href', linkHref(env, env.ctx.path, '', heading, blockId));
    addClass(token, 'internal-link');
    return;
  }
  const clean = target.replace(/^\.\//, '');
  if (attachmentKind(clean) === null) {
    const resolved = resolveLink(env, clean);
    token.attrSet('href', linkHref(env, resolved, clean, heading, blockId));
    token.attrSet('data-target', clean);
    addClass(token, resolved ? 'wikilink md-link' : 'wikilink md-link is-unresolved');
    return;
  }
  const path = resolveLink(env, clean) ?? clean;
  const url = assetUrl(env, path);
  if (url) token.attrSet('href', heading ? `${url}#${heading}` : url);
  token.attrSet('data-target', clean);
  addClass(token, 'internal-link attachment-link');
}

/** Rewrite an `image` token: resolve relative sources, apply `|WxH` sizes, lazy loading. */
export function applyImagePolicy(token: Token, env: RenderEnv | undefined): void {
  const src = String(token.attrGet('src') ?? '');
  if (env && src && urlScheme(src) === null && !src.startsWith('//')) {
    const target = safeDecodeUri(src).replace(/^\.\//, '').split('#')[0]!;
    const path = resolveLink(env, target) ?? target;
    const url = assetUrl(env, path);
    if (url) token.attrSet('src', url);
  }
  const alt = token.content;
  const m = /^(.*?)\|\s*(\d{1,5}(?:\s*x\s*\d{1,5})?)\s*$/i.exec(alt);
  const dims = m ? parseDimensions(m[2]) : null;
  if (m && dims) {
    token.attrSet('width', dims.width);
    if (dims.height !== undefined) token.attrSet('height', dims.height);
    const text = new MarkdownItCtor.Token('text', '', 0);
    text.content = m[1]!.trim();
    token.children = [text];
    token.content = text.content;
  }
  token.attrSet('loading', 'lazy');
}

/** Install the link / image policy. */
export function linksPlugin(md: MarkdownIt): void {
  const linkOpen: RendererRule = (tokens, idx, options, mdEnv, self) => {
    const env = getEnv(mdEnv);
    if (env) applyLinkPolicy(tokens[idx]!, env);
    return self.renderToken(tokens, idx, options);
  };
  const defaultImage = md.renderer.rules.image;
  const image: RendererRule = (tokens, idx, options, mdEnv, self) => {
    applyImagePolicy(tokens[idx]!, getEnv(mdEnv));
    return defaultImage ? defaultImage(tokens, idx, options, mdEnv, self) : self.renderToken(tokens, idx, options);
  };
  md.renderer.rules.link_open = linkOpen;
  md.renderer.rules.image = image;
}
