/**
 * Markdown → sanitised HTML for the reading view, hover previews, embeds and HTML export,
 * implementing the Brulion dialect (docs/SYNTAX.md) with markdown-it and the plugins in
 * ./plugins (one feature per file). The output always passes through `sanitizeHtml`.
 */
import MarkdownIt from 'markdown-it';
import type { MarkdownIt as MarkdownItInstance } from 'markdown-it';
import { escapeHtml } from './escape';
import { createEnv, toMdEnv, withRender } from './env';
import { blockIdsPlugin } from './plugins/blockids';
import { blockRefsPlugin } from './plugins/blockrefs';
import { calcPlugin } from './plugins/calc';
import { calloutsPlugin } from './plugins/callouts';
import { chipsPlugin } from './plugins/chips';
import { commentsPlugin } from './plugins/comments';
import { embedsPlugin } from './plugins/embeds';
import { fencePlugin } from './plugins/fence';
import { footnotesPlugin } from './plugins/footnotes';
import { frontmatterPlugin } from './plugins/frontmatter';
import { headingsPlugin } from './plugins/headings';
import { highlightPlugin } from './plugins/highlight';
import { linksPlugin } from './plugins/links';
import { propertiesPlugin } from './plugins/properties';
import { sourcemapPlugin } from './plugins/sourcemap';
import { tablesPlugin } from './plugins/tables';
import { tagsPlugin } from './plugins/tags';
import { tasksPlugin } from './plugins/tasks';
import { textPlugin } from './plugins/text';
import { wikilinksPlugin } from './plugins/wikilinks';
import { sanitizeHtml } from './sanitize';
import type { RenderContext } from './types';

/**
 * Create a markdown-it instance with the whole Brulion dialect. The registration order is the
 * execution order of the core rules:
 *
 * * before inline parsing: callouts → tasks → block properties;
 * * after inline parsing: footnotes → embeds (paragraph splitting) → block ids → heading ids →
 *   table formulas → inline calc lines; then markdown-it's linkify / text_join;
 * * last: task checkboxes → chips (metadata tokens) → source lines (`data-line`).
 */
export function createMarkdownIt(opts: { breaks?: boolean } = {}): MarkdownItInstance {
  const md = new MarkdownIt('default', {
    html: true,
    linkify: true,
    typographer: false,
    breaks: opts.breaks ?? true,
    langPrefix: 'language-',
  });
  // Bare domains ("plik.md", "skrypt.py") are not links; scheme URLs and e-mails are.
  md.linkify.set({ fuzzyLink: false, fuzzyIP: false });
  md.use(textPlugin)
    .use(frontmatterPlugin)
    .use(commentsPlugin)
    .use(calloutsPlugin)
    .use(tasksPlugin)
    .use(propertiesPlugin)
    .use(wikilinksPlugin)
    .use(blockRefsPlugin)
    .use(footnotesPlugin)
    .use(tagsPlugin)
    .use(highlightPlugin)
    .use(embedsPlugin)
    .use(blockIdsPlugin)
    .use(headingsPlugin)
    .use(tablesPlugin)
    .use(calcPlugin)
    .use(fencePlugin)
    .use(linksPlugin)
    .use(chipsPlugin)
    .use(sourcemapPlugin);
  return md;
}

const instances = new Map<boolean, MarkdownItInstance>();

function markdownIt(breaks: boolean): MarkdownItInstance {
  let md = instances.get(breaks);
  if (!md) {
    md = createMarkdownIt({ breaks });
    instances.set(breaks, md);
  }
  return md;
}

/** Plain fallback when rendering itself fails (should not happen): the escaped source. */
function failSafe(content: string): string {
  return `<pre class="render-error">${escapeHtml(content)}</pre>`;
}

/**
 * Render a note (or an excerpt of one) to sanitised HTML. `ctx` connects the renderer to the
 * vault (link resolution, hrefs, attachments, embeds, block references, queries). Never throws.
 */
export function renderMarkdown(content: string, ctx: RenderContext): string {
  const text = typeof content === 'string' ? content : '';
  const env = createEnv(text, ctx);
  return withRender(env, () => {
    let html: string;
    try {
      html = markdownIt(ctx.breaks !== false).render(text, toMdEnv(env));
    } catch {
      html = failSafe(text);
    }
    return sanitizeHtml(html, { allowedUrls: env.assetUrls, interactive: ctx.interactive === true });
  });
}

/**
 * Render a single line of inline Markdown (block reference texts, table cells, titles) to
 * sanitised HTML — no block structure, no frontmatter. Never throws.
 */
export function renderMarkdownInline(text: string, ctx: RenderContext): string {
  const src = typeof text === 'string' ? text : '';
  const env = createEnv(src, ctx, { frontmatter: false });
  return withRender(env, () => {
    let html: string;
    try {
      html = markdownIt(ctx.breaks !== false).renderInline(src, toMdEnv(env));
    } catch {
      html = escapeHtml(src);
    }
    return sanitizeHtml(html, { allowedUrls: env.assetUrls, interactive: ctx.interactive === true });
  });
}
