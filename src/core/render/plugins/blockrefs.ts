/**
 * Logseq block references `((id))` → `<span class="blockref" data-id>` with the referenced
 * block's text (from `ctx.blockRef`, rendered as inline Markdown), and the parsing half of block
 * embeds `!((id))` (rendered by ./embeds.ts). Unresolved references keep `((id))` visible and get
 * the class `is-unresolved`. Nested references are expanded at most two levels deep and never
 * in a cycle.
 */
import type { Env, MarkdownIt, RendererRule, StateInline } from 'markdown-it';
import { escapeHtml, renderAttrs } from '../escape';
import { getEnv, safeCall, tr } from '../env';
import type { RenderEnv, RenderContext } from '../types';

const BLOCKREF_RE = /\(\(([A-Za-z0-9_-]{1,128})\)\)/y;
/** Maximum nesting of block references rendered inside block reference texts. */
const MAX_REF_NESTING = 2;

/** Token meta of `blockref` / `block_embed` tokens. */
export type BlockRefMeta = { id: string };

/** The `blockref` inline rule (also `!((id))` block embeds). */
export function blockRefRule(state: StateInline, silent: boolean): boolean {
  const src = state.src;
  let pos = state.pos;
  const embed = src.charCodeAt(pos) === 0x21; /* ! */
  if (embed) pos++;
  if (src.charCodeAt(pos) !== 0x28 || src.charCodeAt(pos + 1) !== 0x28) return false;
  BLOCKREF_RE.lastIndex = pos;
  const m = BLOCKREF_RE.exec(src);
  if (!m || pos + m[0].length > state.posMax) return false;
  if (!silent) {
    const token = state.push(embed ? 'block_embed' : 'blockref', '', 0);
    token.content = m[1]!;
    token.meta = { id: m[1]! } satisfies BlockRefMeta;
  }
  state.pos = pos + m[0].length;
  return true;
}

/** Look up a block through the context (null when missing or the host throws). */
export function lookupBlock(ctx: RenderContext, id: string): { path: string; text: string } | null {
  const ref = safeCall(() => ctx.blockRef?.(id) ?? null, null);
  if (!ref || typeof ref.text !== 'string' || typeof ref.path !== 'string') return null;
  return ref;
}

/**
 * Render the text of block `id` as inline Markdown, guarding against nesting and cycles
 * (plain escaped text then).
 */
export function renderBlockText(md: MarkdownIt, mdEnv: Env, env: RenderEnv, id: string, text: string): string {
  if (env.blockRefStack.length >= MAX_REF_NESTING || env.blockRefStack.includes(id)) return escapeHtml(text);
  env.blockRefStack.push(id);
  try {
    return md.renderInline(text, mdEnv);
  } finally {
    env.blockRefStack.pop();
  }
}

/** Install block reference parsing and rendering. */
export function blockRefsPlugin(md: MarkdownIt): void {
  md.inline.ruler.before('link', 'blockref', blockRefRule);
  const render: RendererRule = (tokens, idx, _options, mdEnv) => {
    const token = tokens[idx]!;
    const id = (token.meta as BlockRefMeta | null)?.id ?? token.content;
    const env = getEnv(mdEnv);
    const ref = env ? lookupBlock(env.ctx, id) : null;
    if (!env || !ref || !mdEnv) {
      const title = env ? tr(env, 'render.blockRef.missing', { id }) : undefined;
      return `<span${renderAttrs({ class: 'blockref is-unresolved', 'data-id': id, title })}>((${escapeHtml(id)}))</span>`;
    }
    const html = renderBlockText(md, mdEnv, env, id, ref.text);
    return `<span${renderAttrs({ class: 'blockref', 'data-id': id, 'data-path': ref.path })}>${html}</span>`;
  };
  md.renderer.rules.blockref = render;
}
