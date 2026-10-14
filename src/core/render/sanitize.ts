/**
 * `sanitizeHtml` — the last step of every render. Notes may come from anywhere (imports, sync),
 * so their raw HTML is never trusted.
 *
 * In a DOM environment (browser, Tauri webview, jsdom) DOMPurify does the work, bound lazily to
 * `globalThis.window` (never touched at module load) and configured with the allowlist from
 * ./sanitize-policy.ts plus hooks that validate URLs, force checkboxes and harden links.
 * Without a DOM (plain Node, workers) the strict string sanitizer from ./sanitize-string.ts is
 * used instead: same allowlist, conservative re-serialisation.
 */
import DOMPurify, { type Config, type DOMPurify as DOMPurifyInstance, type WindowLike } from 'dompurify';
import {
  ALLOWED_ATTRS,
  ALLOWED_TAGS,
  DROP_WITH_CONTENT,
  INPUT_ATTRS,
  URL_ATTRS,
  isExternalWebUrl,
  isSafeUrl,
  type SanitizeOptions,
} from './sanitize-policy';
import { sanitizeHtmlString } from './sanitize-string';

export type { SanitizeOptions } from './sanitize-policy';

const CONFIG: Config = {
  ALLOWED_TAGS: [...ALLOWED_TAGS],
  ALLOWED_ATTR: [...ALLOWED_ATTRS],
  ALLOW_DATA_ATTR: true,
  ALLOW_ARIA_ATTR: true,
  ALLOW_UNKNOWN_PROTOCOLS: false,
  FORBID_TAGS: [...DROP_WITH_CONTENT],
  FORBID_ATTR: ['style', 'srcset', 'formaction', 'action', 'xlink:href'],
  ADD_FORBID_CONTENTS: [...DROP_WITH_CONTENT],
  KEEP_CONTENT: true,
  // Our own URL hook decides; this regexp only matters if the hook is bypassed.
  ALLOWED_URI_REGEXP: /^(?:(?:https?|mailto|tel|asset|blob):|[^a-z]|[a-z+.-]+(?:[^a-z+.\-:]|$))/i,
};

/** Options of the sanitize call in progress (DOMPurify hooks are per instance, calls are sync). */
let current: { allowed: ReadonlySet<string>; interactive: boolean } = { allowed: new Set(), interactive: false };

const instances = new WeakMap<object, DOMPurifyInstance>();

/** A DOM window when one exists (browser, webview, jsdom), else null. */
function domWindow(): (WindowLike & object) | null {
  const w = (globalThis as { window?: unknown }).window;
  if (!w || typeof w !== 'object') return null;
  const doc = (w as { document?: { createElement?: unknown } }).document;
  return doc && typeof doc.createElement === 'function' ? (w as WindowLike & object) : null;
}

function installHooks(purify: DOMPurifyInstance): void {
  purify.addHook('uponSanitizeAttribute', (node, data) => {
    const name = data.attrName;
    const tag = node.nodeName.toLowerCase();
    if (URL_ATTRS.has(name)) {
      const ok = isSafeUrl(data.attrValue, tag, name, current.allowed);
      data.keepAttr = ok;
      data.forceKeepAttr = ok ? true : undefined;
      return;
    }
    if (name === 'target' && data.attrValue.trim().toLowerCase() !== '_blank') data.keepAttr = false;
    if (tag === 'input' && !INPUT_ATTRS.has(name) && !name.startsWith('data-') && !name.startsWith('aria-')) data.keepAttr = false;
  });
  purify.addHook('afterSanitizeAttributes', (node) => {
    const tag = node.nodeName.toLowerCase();
    if (tag === 'a') {
      const href = node.getAttribute('href');
      if (href !== null && isExternalWebUrl(href)) node.setAttribute('target', '_blank');
      if (node.hasAttribute('target')) node.setAttribute('rel', 'noopener noreferrer');
    } else if (tag === 'input') {
      node.setAttribute('type', 'checkbox');
      if (!current.interactive) node.setAttribute('disabled', '');
    } else if (tag === 'audio' || tag === 'video') {
      node.setAttribute('controls', '');
    }
  });
}

/** DOMPurify bound to `win` (created once per window). */
function purifierFor(win: WindowLike & object): DOMPurifyInstance | null {
  let purify = instances.get(win);
  if (!purify) {
    purify = DOMPurify(win);
    if (!purify.isSupported) return null;
    installHooks(purify);
    instances.set(win, purify);
  }
  return purify;
}

/**
 * Sanitise HTML for display: only the allowlisted elements / attributes survive, URLs are
 * validated (`javascript:` and friends never pass), external links get
 * `target="_blank" rel="noopener noreferrer"`, every `<input>` becomes a checkbox (disabled
 * unless `opts.interactive`). Uses DOMPurify when a DOM window is available, otherwise a strict
 * string sanitizer (documented in ./sanitize-string.ts). Never throws.
 */
export function sanitizeHtml(html: string, opts: SanitizeOptions = {}): string {
  if (typeof html !== 'string' || html === '') return '';
  const win = domWindow();
  const purify = win ? purifierFor(win) : null;
  if (!purify) return sanitizeHtmlString(html, opts);
  const previous = current;
  current = { allowed: new Set(opts.allowedUrls ?? []), interactive: opts.interactive === true };
  try {
    return purify.sanitize(html, CONFIG);
  } catch {
    return sanitizeHtmlString(html, opts);
  } finally {
    current = previous;
  }
}
