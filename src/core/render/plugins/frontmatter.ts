/**
 * YAML frontmatter: never rendered as text. A block rule consumes the `---` … `---` lines at
 * the top of the document (detected once per render with `findFrontmatter`) and renders a
 * compact properties table (`<table class="properties">`) unless `ctx.showProperties` is false.
 */
import type { MarkdownIt, RendererRule, StateBlock } from 'markdown-it';
import { escapeHtml, renderAttrs } from '../escape';
import { dataLine, getEnv, tr } from '../env';
import type { RenderEnv } from '../types';
import { renderTagHtml } from './tags';

/** Keys whose values are tags (rendered as tag links). */
const TAG_KEYS = new Set(['tags', 'tag']);
/** Keys whose values are shown as plain text (never parsed as Markdown). */
const PLAIN_KEYS = new Set(['aliases', 'alias', 'cssclasses', 'cssclass']);

/** The block rule: consume the frontmatter lines at the very top of the document. */
export function frontmatterRule(state: StateBlock, startLine: number, _endLine: number, silent: boolean): boolean {
  if (startLine !== 0 || state.level !== 0 || state.parentType !== 'root') return false;
  const fm = getEnv(state.env)?.frontmatter;
  if (!fm || fm.endLine + 1 > state.lineMax) return false;
  if (silent) return true;
  const token = state.push('front_matter', '', 0);
  token.block = true;
  token.map = [0, fm.endLine + 1];
  token.meta = { data: fm.data };
  state.line = fm.endLine + 1;
  return true;
}

/** Renders a string as inline Markdown (with the current render's environment). */
export type InlineRenderer = (src: string) => string;

function renderScalar(value: unknown, key: string, env: RenderEnv, inline: InlineRenderer): string {
  if (value === null || value === undefined) return '';
  if (typeof value === 'boolean') {
    const label = tr(env, value ? 'render.properties.yes' : 'render.properties.no');
    return `<input${renderAttrs({ type: 'checkbox', class: 'prop-checkbox', checked: value, disabled: true, 'aria-label': label })}>`;
  }
  if (typeof value === 'number' || typeof value === 'bigint') return escapeHtml(String(value));
  if (typeof value === 'string') {
    if (TAG_KEYS.has(key)) {
      const name = value.trim().replace(/^#/, '');
      return name ? renderTagHtml(env, name) : '';
    }
    if (PLAIN_KEYS.has(key)) return escapeHtml(value);
    return inline(value);
  }
  let json: string;
  try {
    json = JSON.stringify(value) ?? String(value);
  } catch {
    json = String(value);
  }
  return `<code>${escapeHtml(json)}</code>`;
}

/** HTML of one property value (lists become separate items). */
export function renderPropertyValue(value: unknown, key: string, env: RenderEnv, inline: InlineRenderer): string {
  const k = key.toLowerCase();
  if (Array.isArray(value)) {
    return value
      .map((item) => `<span class="prop-item">${renderScalar(item, k, env, inline)}</span>`)
      .join('<span class="prop-sep">, </span>');
  }
  // `tags: a, b` written as a plain string.
  if (TAG_KEYS.has(k) && typeof value === 'string' && /[,\s]/.test(value.trim())) {
    return renderPropertyValue(value.split(/[,\s]+/).filter(Boolean), key, env, inline);
  }
  return renderScalar(value, k, env, inline);
}

/** HTML of the properties table for frontmatter `data` (empty string when there is nothing). */
export function renderPropertiesTable(data: Record<string, unknown>, line: number, env: RenderEnv, inline: InlineRenderer): string {
  const keys = Object.keys(data);
  if (keys.length === 0) return '';
  let rows = '';
  for (const key of keys) {
    rows += `<tr${renderAttrs({ 'data-key': key })}><th>${escapeHtml(key)}</th><td>${renderPropertyValue(data[key], key, env, inline)}</td></tr>`;
  }
  return `<table${renderAttrs({ class: 'properties', 'data-line': dataLine(env, line) })}><tbody>${rows}</tbody></table>\n`;
}

/** Install frontmatter handling. */
export function frontmatterPlugin(md: MarkdownIt): void {
  md.block.ruler.before('table', 'front_matter', frontmatterRule);
  const render: RendererRule = (tokens, idx, _options, mdEnv) => {
    const env = getEnv(mdEnv);
    if (!env || env.ctx.showProperties === false) return '';
    const data = (tokens[idx]!.meta?.data ?? {}) as Record<string, unknown>;
    const inline: InlineRenderer = (src) => md.renderInline(src, mdEnv);
    return renderPropertiesTable(data, tokens[idx]!.map?.[0] ?? 0, env, inline);
  };
  md.renderer.rules.front_matter = render;
}
