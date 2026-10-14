/**
 * Rendering helpers for widgets: Markdown → sanitised HTML through `@/core/render` (transclusions,
 * table cells, query results), the fragment hrefs rendered links carry (parsed back by the click
 * handler in actions.ts) and plain-text excerpts for block references and titles.
 */
import { escapeHtml, renderMarkdown, renderMarkdownInline, type RenderContext } from '@/core/render';
import { pageTitle } from '@/core/paths';

export { escapeHtml };
export type { RenderContext };

// ---------------------------------------------------------------------------------------------
// Link hrefs inside rendered HTML
// ---------------------------------------------------------------------------------------------

/** `#/page/<target>` — fragment hrefs survive sanitising and never navigate the webview. */
export function pageHref(target: string): string {
  return `#/page/${encodeURIComponent(target)}`;
}

export function tagHref(tag: string): string {
  return `#/tag/${encodeURIComponent(tag.replace(/^#/, ''))}`;
}

export function blockHref(id: string): string {
  return `#/block/${encodeURIComponent(id)}`;
}

export type RenderedLink = { kind: 'page'; target: string } | { kind: 'block'; id: string } | { kind: 'tag'; tag: string } | { kind: 'url'; url: string };

/** Interpret an `href` from rendered HTML (null: nothing to open, e.g. `javascript:` or `#anchor`). */
export function parseRenderedHref(href: string): RenderedLink | null {
  const m = /^#\/(page|block|tag)\/(.*)$/.exec(href);
  if (m) {
    let v: string;
    try {
      v = decodeURIComponent(m[2]!);
    } catch {
      v = m[2]!;
    }
    if (m[1] === 'page') return { kind: 'page', target: v };
    if (m[1] === 'block') return { kind: 'block', id: v };
    return { kind: 'tag', tag: v };
  }
  if (/^(https?|mailto|tel):/i.test(href)) return { kind: 'url', url: href };
  if (href.startsWith('#') || /^[a-z][a-z0-9+.-]*:/i.test(href)) return null;
  if (href) {
    let target = href;
    try {
      target = decodeURI(href);
    } catch {
      // keep as written
    }
    return { kind: 'page', target };
  }
  return null;
}

/**
 * What a click on rendered HTML (core render's markup) opens: tags (`a.tag[data-tag]`), block
 * references (`.blockref[data-id]`) and links (by href). Null when the element opens nothing.
 */
export function renderedLinkOf(el: Element): RenderedLink | null {
  const tag = el.closest('[data-tag]');
  if (tag && tag.classList.contains('tag')) return { kind: 'tag', tag: tag.getAttribute('data-tag') ?? '' };
  const ref = el.closest('.blockref[data-id]');
  if (ref) return { kind: 'block', id: ref.getAttribute('data-id') ?? '' };
  const a = el.closest('a[href]');
  return a ? parseRenderedHref(a.getAttribute('href') ?? '') : null;
}

// ---------------------------------------------------------------------------------------------
// Markdown
// ---------------------------------------------------------------------------------------------

/** Render Markdown to sanitised HTML (never throws: escaped text on failure). */
export function renderMarkdownSafe(content: string, ctx: RenderContext): string {
  try {
    return renderMarkdown(content, ctx);
  } catch {
    return `<p>${escapeHtml(content)}</p>`;
  }
}

/** Render inline Markdown (a table cell, a task's text) to sanitised HTML. */
export function renderInlineSafe(text: string, ctx: RenderContext): string {
  try {
    return renderMarkdownInline(text, ctx);
  } catch {
    return escapeHtml(text);
  }
}

// ---------------------------------------------------------------------------------------------
// Inline text
// ---------------------------------------------------------------------------------------------

/** Inline Markdown as plain text (for block-ref widgets, titles): syntax removed, truncated. */
export function plainInline(text: string, max = 140): string {
  let s = text
    .replace(/%%[\s\S]*?%%/g, '')
    .replace(/!?\[\[([^\]|]*)\|([^\]]*)\]\]/g, '$2')
    .replace(/!?\[\[([^\]]*)\]\]/g, (_m, t: string) => {
      const [page, heading] = t.split('#');
      return (page ? pageTitle(page) : '') + (heading ? ` › ${heading.replace(/^\^/, '')}` : '');
    })
    .replace(/!?\[([^\]]*)\]\([^)]*\)/g, '$1')
    .replace(/(\*\*|__|~~|==)(.+?)\1/g, '$2')
    .replace(/(^|[^\w*])[*_]([^*_\n]+)[*_](?=$|[^\w*])/g, '$1$2')
    .replace(/`([^`]*)`/g, '$1')
    .replace(/[ \t]\^[A-Za-z0-9-]+[ \t]*$/, '')
    .replace(/\s+/g, ' ')
    .trim();
  if (s.length > max) s = `${s.slice(0, max - 1).trimEnd()}…`;
  return s;
}
