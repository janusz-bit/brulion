/**
 * Callouts (Obsidian): a blockquote whose first line is `[!type] Title` becomes
 * `<div class="callout callout-type" data-callout="type">` with a title row (icon + title) and
 * a content box. `[!type]+` / `[!type]-` make it foldable (`<details>`, open / closed). Nested
 * callouts work (each blockquote is handled on its own). Unknown types render like notes but keep
 * their `data-callout` value (so themes can style them).
 */
import type { MarkdownIt, RendererRule, StateCore, Token } from 'markdown-it';
import { classNames, escapeHtml, renderAttrPairs, renderAttrs } from '../escape';
import { dataLine, getEnv, tr } from '../env';
import type { RenderMessageKey } from '../messages';
import type { RenderEnv } from '../types';

/** Canonical callout types with their icons (emoji, no external assets). */
export const CALLOUT_ICONS: Readonly<Record<string, string>> = {
  note: '✏️',
  abstract: '📋',
  info: 'ℹ️',
  todo: '☑️',
  tip: '💡',
  success: '✅',
  question: '❓',
  warning: '⚠️',
  failure: '❌',
  danger: '⚡',
  bug: '🐞',
  example: '📑',
  quote: '❝',
};

/** Obsidian aliases → canonical type. */
const CALLOUT_ALIASES: Readonly<Record<string, string>> = {
  summary: 'abstract',
  tldr: 'abstract',
  hint: 'tip',
  important: 'tip',
  check: 'success',
  done: 'success',
  help: 'question',
  faq: 'question',
  caution: 'warning',
  attention: 'warning',
  fail: 'failure',
  missing: 'failure',
  error: 'danger',
  cite: 'quote',
};

/** Canonical type for a written callout type (lower-cased; unknown types map to themselves). */
export function canonicalCalloutType(type: string): string {
  const t = type.toLowerCase();
  return CALLOUT_ALIASES[t] ?? t;
}

/** Parsed first line of a callout. */
export type CalloutHead = {
  /** Type as written, lower-cased (`warning`, `faq`, `my-type`). */
  type: string;
  /** Canonical type used for the icon and the default title (`faq` → `question`). */
  canonical: string;
  /** `+` (foldable, open), `-` (foldable, closed) or '' (not foldable). */
  fold: '' | '+' | '-';
  /** Title as written (may be empty). */
  title: string;
};

const HEAD_RE = /^\[!([A-Za-z0-9_-]{1,64})\]([+-]?)(?:[ \t]+(.*))?$/;

/** Parse `[!type]± Title` (the first line of a blockquote), or null. */
export function parseCalloutHead(line: string): CalloutHead | null {
  const m = HEAD_RE.exec(line.trimEnd());
  if (!m) return null;
  const type = m[1]!.toLowerCase();
  return { type, canonical: canonicalCalloutType(type), fold: (m[2] ?? '') as CalloutHead['fold'], title: (m[3] ?? '').trim() };
}

/** Localised default title of a callout type (capitalised type for unknown types). */
export function defaultCalloutTitle(env: RenderEnv, head: CalloutHead): string {
  if (head.canonical in CALLOUT_ICONS) return tr(env, `render.callout.${head.canonical}` as RenderMessageKey);
  return head.type.charAt(0).toUpperCase() + head.type.slice(1).replace(/[-_]+/g, ' ');
}

function newToken(state: StateCore, type: string, tag: string, nesting: 1 | 0 | -1, level: number): Token {
  const t = new state.Token(type, tag, nesting);
  t.block = true;
  t.level = level;
  return t;
}

/** The `callouts` core rule (runs before inline parsing). */
export function calloutsRule(state: StateCore): void {
  const env = getEnv(state.env);
  if (!env || state.inlineMode) return;
  const tokens = state.tokens;
  const out: Token[] = [];
  const stack: (Token | null)[] = [];
  let changed = false;
  for (let i = 0; i < tokens.length; i++) {
    const tok = tokens[i]!;
    if (tok.type === 'blockquote_open') {
      const p = tokens[i + 1];
      const inline = tokens[i + 2];
      const pc = tokens[i + 3];
      const lines = p?.type === 'paragraph_open' && inline?.type === 'inline' ? inline.content.split('\n') : null;
      const head = lines ? parseCalloutHead(lines[0]!) : null;
      if (!head || !p || !inline || !pc) {
        stack.push(null);
        out.push(tok);
        continue;
      }
      changed = true;
      const foldable = head.fold !== '';
      tok.type = 'callout_open';
      tok.tag = foldable ? 'details' : 'div';
      tok.meta = { head };
      stack.push(tok);
      const line = tok.map?.[0] ?? 0;
      const titleOpen = newToken(state, 'callout_title_open', foldable ? 'summary' : 'div', 1, tok.level + 1);
      titleOpen.meta = { head };
      const title = newToken(state, 'inline', '', 0, tok.level + 2);
      title.block = false;
      title.content = head.title || defaultCalloutTitle(env, head);
      title.map = [line, line + 1];
      title.children = [];
      const titleClose = newToken(state, 'callout_title_close', titleOpen.tag, -1, tok.level + 1);
      const contentOpen = newToken(state, 'callout_content_open', 'div', 1, tok.level + 1);
      out.push(tok, titleOpen, title, titleClose, contentOpen);
      const rest = lines!.slice(1).join('\n');
      if (rest.trim()) {
        inline.content = rest;
        if (p.map) p.map = [p.map[0] + 1, p.map[1]];
        if (inline.map) inline.map = [inline.map[0] + 1, inline.map[1]];
        out.push(p, inline, pc);
      }
      i += 3;
      continue;
    }
    if (tok.type === 'blockquote_close') {
      const open = stack.pop();
      if (open) {
        out.push(newToken(state, 'callout_content_close', 'div', -1, open.level + 1));
        tok.type = 'callout_close';
        tok.tag = open.tag;
        tok.meta = open.meta;
      }
    }
    out.push(tok);
  }
  if (changed) state.tokens = out;
}

function headOf(token: Token): CalloutHead | null {
  return (token.meta?.head as CalloutHead | undefined) ?? null;
}

/** Install callouts. */
export function calloutsPlugin(md: MarkdownIt): void {
  md.core.ruler.before('inline', 'callouts', calloutsRule);
  const open: RendererRule = (tokens, idx, _options, mdEnv) => {
    const token = tokens[idx]!;
    const head = headOf(token);
    const env = getEnv(mdEnv);
    if (!head) return '<div class="callout">\n';
    const attrs = renderAttrs({
      class: classNames('callout', `callout-${head.canonical}`),
      'data-callout': head.type,
      'data-callout-fold': head.fold || undefined,
      open: head.fold === '+',
      'data-line': env && token.map ? dataLine(env, token.map[0]) : undefined,
    });
    const rest = (token.attrs ?? []).filter(([name]) => name !== 'data-line' && name !== 'class');
    return `<${token.tag}${attrs}${renderAttrPairs(rest)}>\n`;
  };
  const titleOpen: RendererRule = (tokens, idx) => {
    const token = tokens[idx]!;
    const head = headOf(token);
    const icon = head ? (CALLOUT_ICONS[head.canonical] ?? CALLOUT_ICONS.note!) : '';
    return `<${token.tag} class="callout-title"><span class="callout-icon" aria-hidden="true">${escapeHtml(icon)}</span><span class="callout-title-inner">`;
  };
  const titleClose: RendererRule = (tokens, idx) => `</span></${tokens[idx]!.tag}>\n`;
  md.renderer.rules.callout_open = open;
  md.renderer.rules.callout_close = (tokens, idx) => `</${tokens[idx]!.tag}>\n`;
  md.renderer.rules.callout_title_open = titleOpen;
  md.renderer.rules.callout_title_close = titleClose;
  md.renderer.rules.callout_content_open = () => '<div class="callout-content">\n';
  md.renderer.rules.callout_content_close = () => '</div>\n';
}
