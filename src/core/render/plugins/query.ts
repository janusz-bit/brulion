/**
 * ```` ```query ```` blocks (SYNTAX §10): the host renders live results through `ctx.query(src)`
 * (HTML, sanitised with the rest of the page). Without a host (or when it returns null) a
 * placeholder `<div class="query-placeholder" data-query>` shows the query source.
 */
import { escapeHtml, renderAttrs } from '../escape';
import { safeCall, tr } from '../env';
import type { RenderEnv } from '../types';

/** HTML of a query block with source `src` (`attrs`: extra serialised wrapper attributes). */
export function renderQueryBlock(env: RenderEnv, src: string, attrs = ''): string {
  const query = src.replace(/\r?\n$/, '');
  const fn = env.ctx.query;
  const html = fn ? safeCall(() => fn.call(env.ctx, query), null) : null;
  if (typeof html === 'string') {
    return `<div${renderAttrs({ class: 'query', 'data-query': query })}${attrs}>${html}</div>\n`;
  }
  const title = tr(env, 'render.query.title');
  return (
    `<div${renderAttrs({ class: 'query-placeholder', 'data-query': query })}${attrs}>` +
    `<div class="query-title">🔎 ${escapeHtml(title)}</div><pre><code class="language-query">${escapeHtml(query)}</code></pre></div>\n`
  );
}
