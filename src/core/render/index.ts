/**
 * `@/core/render` — Markdown (Brulion dialect, docs/SYNTAX.md) → sanitised HTML for the reading
 * view, hover previews, embeds and HTML export. See docs/ARCHITECTURE.md for the contract.
 *
 * DOM use is limited to `sanitizeHtml`, which binds DOMPurify to `globalThis.window` lazily and
 * falls back to a strict string sanitizer when no DOM exists (plain Node, workers).
 */
export { renderMarkdown, renderMarkdownInline, createMarkdownIt } from './render';
export { sanitizeHtml } from './sanitize';
export type { SanitizeOptions } from './sanitize';
export { sanitizeHtmlString } from './sanitize-string';
export { RENDER_CSS, RENDER_ROOT_CLASS } from './styles';
export type { RenderContext, RenderTranslator } from './types';
export { MAX_EMBED_DEPTH } from './env';
export { headingAnchor, blockAnchor, slugify, footnoteId, footnoteRefId, HEADING_ID_PREFIX } from './anchors';
export { attachmentKind, parseWikiTarget, wikiDisplay } from './targets';
export type { AttachmentKind, WikiTarget } from './targets';
export { CALLOUT_ICONS, canonicalCalloutType, parseCalloutHead } from './plugins/callouts';
export { escapeHtml } from './escape';
export { messages as renderMessages } from './messages';
