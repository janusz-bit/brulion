/**
 * The HTML allowlist shared by both sanitizers (DOMPurify in a DOM, the strict string sanitizer
 * elsewhere). Everything not listed is removed: `<script>`, `<style>`, `<iframe>`, `<object>`,
 * forms, SVG / MathML, inline `style` and every `on*` handler.
 */

/** Elements that may appear in rendered notes. */
export const ALLOWED_TAGS: readonly string[] = [
  'a', 'abbr', 'address', 'article', 'aside', 'audio', 'b', 'bdi', 'bdo', 'big', 'blockquote', 'br',
  'caption', 'center', 'cite', 'code', 'col', 'colgroup', 'dd', 'del', 'details', 'dfn', 'div', 'dl',
  'dt', 'em', 'figcaption', 'figure', 'footer', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'header', 'hr',
  'i', 'img', 'input', 'ins', 'kbd', 'li', 'mark', 'nav', 'ol', 'p', 'picture', 'pre', 'q', 'rp', 'rt',
  'ruby', 's', 'samp', 'section', 'small', 'source', 'span', 'strike', 'strong', 'sub', 'summary',
  'sup', 'table', 'tbody', 'td', 'tfoot', 'th', 'thead', 'time', 'tr', 'track', 'tt', 'u', 'ul', 'var',
  'video', 'wbr',
];

/** Attributes allowed on any allowed element (plus `data-*` and `aria-*`). */
export const ALLOWED_ATTRS: readonly string[] = [
  'href', 'src', 'alt', 'title', 'class', 'id', 'width', 'height', 'align', 'valign', 'colspan',
  'rowspan', 'scope', 'start', 'reversed', 'type', 'value', 'checked', 'disabled', 'open', 'controls',
  'loop', 'muted', 'playsinline', 'preload', 'poster', 'target', 'rel', 'lang', 'dir', 'datetime',
  'cite', 'loading', 'decoding', 'span', 'role', 'kind', 'srclang', 'label',
];

/** Elements removed together with their content. */
export const DROP_WITH_CONTENT: readonly string[] = [
  'script', 'style', 'iframe', 'object', 'embed', 'frame', 'frameset', 'noscript', 'noembed',
  'noframes', 'template', 'textarea', 'title', 'xmp', 'plaintext', 'svg', 'math', 'select',
  'option', 'button', 'form', 'applet', 'head', 'link', 'meta', 'base',
];

/** Attributes holding URLs (validated by {@link isSafeUrl}). */
export const URL_ATTRS: ReadonlySet<string> = new Set(['href', 'src', 'poster', 'cite']);

/** Boolean attributes (serialised without a value by the string sanitizer). */
export const BOOLEAN_ATTRS: ReadonlySet<string> = new Set(['checked', 'disabled', 'open', 'controls', 'loop', 'muted', 'playsinline', 'reversed']);

/** Void elements (no closing tag). */
export const VOID_TAGS: ReadonlySet<string> = new Set(['br', 'hr', 'img', 'input', 'col', 'source', 'track', 'wbr']);

/** Attributes kept on `<input>` (which is always forced to a checkbox). */
export const INPUT_ATTRS: ReadonlySet<string> = new Set(['type', 'checked', 'disabled', 'class', 'title', 'id']);

/** URL schemes allowed everywhere (`asset:` = Tauri asset protocol, `blob:` = object URLs). */
const SAFE_SCHEMES: ReadonlySet<string> = new Set(['http', 'https', 'mailto', 'tel', 'asset', 'blob']);
/** Schemes never allowed, even when the host provided the URL. */
const DANGEROUS_SCHEMES: ReadonlySet<string> = new Set(['javascript', 'vbscript', 'livescript', 'mocha', 'file']);

/** Whitespace / control characters browsers ignore inside URLs (stripped before checking). */
// eslint-disable-next-line no-control-regex
const URL_WHITESPACE_RE = /[\u0000-\u0020\u007f-\u00a0\u1680\u180e\u2000-\u2029\u205f\u3000\ufeff]/g;

const DATA_IMAGE_RE = /^data:image\/(?:png|jpe?g|gif|webp|avif|bmp|x-icon|vnd\.microsoft\.icon|svg\+xml)[;,]/i;
const DATA_MEDIA_RE = /^data:(?:audio|video)\/[a-z0-9.+-]+[;,]/i;

/** Data attribute names we keep (`data-` + lower-case name characters). */
export const DATA_ATTR_RE = /^data-[a-z0-9_.:-]+$/;
/** ARIA attribute names we keep. */
export const ARIA_ATTR_RE = /^aria-[a-z]+$/;

/**
 * Is `value` (an attribute value, entities already decoded) a safe URL for `attr` on `tag`?
 *
 * * relative references, fragments and protocol-relative URLs: yes;
 * * `http(s):`, `mailto:`, `tel:`, `asset:`, `blob:`: yes;
 * * `data:`: only images (`<img>`, `<source>`, `poster`) and audio/video media;
 * * `javascript:`, `vbscript:`, `file:`: never;
 * * other schemes: only URLs listed in `allowed` (URLs the host itself produced).
 */
export function isSafeUrl(value: string, tag: string, attr: string, allowed?: ReadonlySet<string>): boolean {
  const v = value.replace(URL_WHITESPACE_RE, '');
  const m = /^([a-z][a-z0-9+.-]*):/i.exec(v);
  if (!m) return true; // relative reference, fragment or protocol-relative URL
  const scheme = m[1]!.toLowerCase();
  if (DANGEROUS_SCHEMES.has(scheme)) return false;
  if (scheme === 'data') {
    if (attr !== 'src' && attr !== 'poster') return false;
    if (tag === 'img' || tag === 'picture' || attr === 'poster') return DATA_IMAGE_RE.test(v);
    if (tag === 'source') return DATA_IMAGE_RE.test(v) || DATA_MEDIA_RE.test(v);
    if (tag === 'audio' || tag === 'video' || tag === 'track') return DATA_MEDIA_RE.test(v);
    return false;
  }
  if (SAFE_SCHEMES.has(scheme)) return true;
  return allowed?.has(value) === true || allowed?.has(value.trim()) === true;
}

/** Absolute web URL (gets `target="_blank" rel="noopener noreferrer"`). */
export function isExternalWebUrl(value: string): boolean {
  return /^https?:\/\//i.test(value.replace(URL_WHITESPACE_RE, '')) || /^\/\//.test(value.trim());
}

/** Options of `sanitizeHtml`. */
export interface SanitizeOptions {
  /**
   * URLs produced by the host (`ctx.assetUrl`) that are kept even with an unusual scheme
   * (never `javascript:` / `vbscript:` / non-media `data:`).
   */
  allowedUrls?: Iterable<string>;
  /** Keep checkboxes enabled (interactive task lists); otherwise all are disabled. */
  interactive?: boolean;
}
