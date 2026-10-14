/**
 * Wiki links `[[Page]]`, `[[Page|Alias]]`, `[[Page#Heading]]`, `[[Page#^block]]`, same-page
 * `[[#Heading]]` / `[[#^block]]` (SYNTAX §2) and the parsing half of embeds `![[…]]` (rendered
 * by ./embeds.ts).
 *
 * Links render as `<a class="wikilink" data-target href>` with the href from `ctx.href`;
 * unresolved targets get the extra class `is-unresolved`.
 */
import type { MarkdownIt, RendererRule, StateInline } from 'markdown-it';
import { escapeHtml, renderAttrs, classNames } from '../escape';
import { getEnv, linkHref, resolveLink } from '../env';
import { parseWikiTarget, wikiDisplay, type WikiTarget } from '../targets';
import type { RenderEnv } from '../types';

/** Longest `[[…]]` we try to match (bounds the work per `[`). */
const MAX_WIKI_LENGTH = 1000;

/** Token meta of `wikilink` and `embed` tokens. */
export type WikiMeta = WikiTarget & { raw: string };

/** The `wikilink` inline rule (also `![[embeds]]`). */
export function wikilinkRule(state: StateInline, silent: boolean): boolean {
  const src = state.src;
  const start = state.pos;
  let pos = start;
  const embed = src.charCodeAt(pos) === 0x21; /* ! */
  if (embed) pos++;
  if (src.charCodeAt(pos) !== 0x5b || src.charCodeAt(pos + 1) !== 0x5b) return false;
  // A wiki link inside a Markdown link text would nest anchors.
  if (!embed && state.linkLevel > 0) return false;
  const close = src.indexOf(']]', pos + 2);
  if (close === -1 || close + 2 > state.posMax || close - pos > MAX_WIKI_LENGTH) return false;
  const inner = src.slice(pos + 2, close);
  if (/[\r\n]/.test(inner) || inner.includes('[[')) return false;
  const parts = parseWikiTarget(inner);
  if (!parts) return false;
  if (!silent) {
    const token = state.push(embed ? 'embed' : 'wikilink', '', 0);
    token.content = inner;
    token.meta = { ...parts, raw: src.slice(start, close + 2) } satisfies WikiMeta;
  }
  state.pos = close + 2;
  return true;
}

/** HTML of a wiki link to `t` (resolved through the render context). */
export function renderWikiLink(env: RenderEnv, t: WikiTarget, extraClass?: string): string {
  const resolved = t.target ? resolveLink(env, t.target) : env.ctx.path;
  const href = linkHref(env, resolved, t.target, t.heading, t.blockId);
  const attrs = renderAttrs({
    class: classNames('wikilink', extraClass, !resolved && 'is-unresolved'),
    'data-target': t.target,
    'data-heading': t.heading,
    'data-block-id': t.blockId,
    href,
  });
  return `<a${attrs}>${escapeHtml(wikiDisplay(t))}</a>`;
}

const renderWikilinkToken: RendererRule = (tokens, idx, _options, mdEnv) => {
  const token = tokens[idx]!;
  const meta = token.meta as WikiMeta | null;
  const env = getEnv(mdEnv);
  if (!meta) return escapeHtml(token.content);
  if (!env) return `<span class="wikilink">${escapeHtml(wikiDisplay(meta))}</span>`;
  return renderWikiLink(env, meta);
};

/** Install wiki link parsing and rendering. */
export function wikilinksPlugin(md: MarkdownIt): void {
  md.inline.ruler.before('link', 'wikilink', wikilinkRule);
  md.renderer.rules.wikilink = renderWikilinkToken;
}
