/**
 * Tags (SYNTAX §3): `#tag`, `#nested/tag`, `#[[tag with spaces]]` → `<a class="tag">`.
 * A tag must be preceded by the start of the inline content, whitespace or `(`; its body is
 * Unicode letters/digits, `_`, `-`, `/` and must not be all digits (`#1` is not a tag). Tags are
 * never recognised in code (separate tokens), link texts (`linkLevel`), URLs (consumed by
 * linkify / autolinks first) or link targets (consumed by the link rules).
 */
import type { MarkdownIt, RendererRule, StateInline } from 'markdown-it';
import { escapeHtml, renderAttrs } from '../escape';
import { getEnv, linkHref } from '../env';
import type { RenderEnv } from '../types';

const TAG_BODY_RE = /[\p{L}\p{N}\p{M}_\-/]+/uy;
const HAS_LETTER_OR_DIGIT_RE = /[\p{L}\p{N}]/u;
const ALL_DIGITS_RE = /^\d+$/;
const MAX_BRACKET_TAG = 200;

/** Token meta of a `tag` token. */
export type TagMeta = {
  /** Tag name without `#` (NFC). */
  name: string;
};

/** May a tag start at `pos` (start, whitespace or `(` before it)? */
function tagBoundary(src: string, pos: number): boolean {
  if (pos === 0) return true;
  const prev = src[pos - 1]!;
  return prev === '(' || /\s/u.test(prev);
}

/** Validate a `#tag` body; returns the cleaned name (trailing `/` removed) or null. */
export function cleanTagName(body: string): string | null {
  const name = body.replace(/\/+$/, '');
  if (!name || ALL_DIGITS_RE.test(name) || !HAS_LETTER_OR_DIGIT_RE.test(name)) return null;
  return name;
}

/** The `tag` inline rule. */
export function tagRule(state: StateInline, silent: boolean): boolean {
  const src = state.src;
  const pos = state.pos;
  if (src.charCodeAt(pos) !== 0x23 /* # */ || state.linkLevel > 0 || !tagBoundary(src, pos)) return false;
  let name: string;
  let end: number;
  if (src.charCodeAt(pos + 1) === 0x5b && src.charCodeAt(pos + 2) === 0x5b) {
    const close = src.indexOf(']]', pos + 3);
    if (close === -1 || close + 2 > state.posMax || close - pos > MAX_BRACKET_TAG) return false;
    name = src.slice(pos + 3, close).trim();
    if (!name || /[[\]|\r\n]/.test(name)) return false;
    end = close + 2;
  } else {
    TAG_BODY_RE.lastIndex = pos + 1;
    const m = TAG_BODY_RE.exec(src);
    if (!m) return false;
    const body = m[0].slice(0, Math.max(0, state.posMax - pos - 1));
    const clean = cleanTagName(body);
    if (clean === null) return false;
    name = clean;
    end = pos + 1 + clean.length;
  }
  if (!silent) {
    const token = state.push('tag', '', 0);
    token.content = name;
    token.meta = { name: name.normalize('NFC') } satisfies TagMeta;
  }
  state.pos = end;
  return true;
}

/** HTML of a tag link. */
export function renderTagHtml(env: RenderEnv, name: string): string {
  const clean = name.normalize('NFC');
  const href = linkHref(env, null, `#${clean}`);
  return `<a${renderAttrs({ class: 'tag', 'data-tag': clean, href })}>#${escapeHtml(clean)}</a>`;
}

const renderTagToken: RendererRule = (tokens, idx, _options, mdEnv) => {
  const token = tokens[idx]!;
  const meta = token.meta as TagMeta | null;
  const name = meta?.name ?? token.content;
  const env = getEnv(mdEnv);
  if (!env) return `<span class="tag">#${escapeHtml(name)}</span>`;
  return renderTagHtml(env, name);
};

/** Install tag parsing and rendering. */
export function tagsPlugin(md: MarkdownIt): void {
  md.inline.ruler.before('link', 'brulion_tag', tagRule);
  md.renderer.rules.tag = renderTagToken;
}
