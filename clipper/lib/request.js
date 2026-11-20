// A clip as the host takes it (docs/CLIPPER.md "Messages"): built from what the page gave
// (content/extract.js), the form or the settings, and the tab. Pure functions (tests: clipper/test/).
// The host checks everything again: these checks only give an answer before anything is sent.

export const KINDS = ['article', 'selection', 'page', 'screenshot', 'bookmark', 'picture', 'link'];

/** The largest HTML sent (characters); the host takes 24 MiB, a browser sends 64 MiB at most. */
export const MAX_HTML_CHARS = 16 * 1024 * 1024;

/**
 * The kind to clip: `preferred` when the page has it, else the selection, the article, the page.
 * `extraction` may be null (a page the browser lets no extension read): only a bookmark or a
 * screenshot then.
 */
export function chooseKind(extraction, preferred = 'auto') {
  const has = {
    article: Boolean(extraction?.articleHtml),
    selection: Boolean(extraction?.selectionHtml || extraction?.selectionText),
    page: Boolean(extraction?.pageHtml) || Boolean(extraction),
    screenshot: true,
    bookmark: true,
  };
  if (preferred !== 'auto' && has[preferred]) return preferred;
  if (has.selection) return 'selection';
  if (has.article) return 'article';
  if (extraction) return 'page';
  return 'bookmark';
}

/** Tags typed "a, #b c; d" as ["a", "b c", "d"]. */
export function splitTags(text) {
  return String(text ?? '')
    .split(/[,;]/)
    .map((tag) => tag.trim().replace(/^#+/, '').trim())
    .filter((tag, i, all) => tag && all.findIndex((other) => other.toLowerCase() === tag.toLowerCase()) === i)
    .slice(0, 32);
}

/**
 * A folder typed for a new page, as the host takes it: "" (the root) or "a/b". null when the host
 * would refuse it: "..", ".", a hidden part, an absolute path, a backslash, an empty part.
 */
export function checkFolder(text) {
  const folder = String(text ?? '').trim();
  if (folder === '') return '';
  if (folder.includes('\\') || folder.startsWith('/') || /^[A-Za-z]:/.test(folder)) return null;
  const parts = folder.split('/').map((part) => part.trim());
  if (parts.length > 12) return null;
  for (const part of parts) {
    if (!part || part === '.' || part === '..' || part.startsWith('.') || part.length > 120) return null;
    if (/[\u0000-\u001f\u007f]/.test(part)) return null;
  }
  return parts.join('/');
}

/** A template option's value ("builtin:brulion", "page:templates/Artykuł.md", "custom:t1") as the host's choice. */
export function templateChoice(value, customTemplates = []) {
  const text = String(value ?? '');
  const colon = text.indexOf(':');
  const kind = colon < 0 ? 'builtin' : text.slice(0, colon);
  const name = colon < 0 ? text : text.slice(colon + 1);
  if (kind === 'page') return { page: name };
  if (kind === 'custom') {
    const custom = customTemplates.find((c) => c.id === name);
    if (custom) return { text: custom.text };
  }
  return { builtin: kind === 'builtin' && name ? name : 'brulion' };
}

function cut(text, max) {
  const s = String(text ?? '');
  return s.length > max ? s.slice(0, max) : s;
}

/**
 * The host's `clip` object.
 * - `extraction`: content/extract.js's result (or null)
 * - `form`: {kind, title, place, folder, headingPage, heading, journalLink, template, tags, downloadPictures}
 * - `tab`: {url, title}
 * - `extra`: {screenshot (data: URL), linkUrl, linkText, pictureUrl}
 */
export function buildClipRequest({ extraction, form, tab, extra = {}, customTemplates = [] }) {
  const e = extraction ?? {};
  const kind = KINDS.includes(form.kind) ? form.kind : 'article';
  const clip = {
    kind,
    url: e.url || tab?.url || '',
    baseUrl: e.baseUrl || '',
    title: String(form.title ?? '').trim() || e.title || tab?.title || '',
    author: e.author || '',
    site: e.site || '',
    published: e.published || '',
    description: e.description || '',
    language: e.language || '',
    image: e.image || '',
    tags: splitTags(form.tags),
    downloadPictures: form.downloadPictures !== false,
    template: templateChoice(form.template, customTemplates),
  };
  if (kind === 'article') clip.html = cut(e.articleHtml, MAX_HTML_CHARS);
  if (kind === 'page') clip.html = cut(e.pageHtml, MAX_HTML_CHARS);
  // The selection goes with every kind ({{selection}} in a template).
  if (e.selectionHtml) clip.selectionHtml = cut(e.selectionHtml, MAX_HTML_CHARS / 2);
  if (e.selectionText) clip.selectionText = cut(e.selectionText, MAX_HTML_CHARS / 2);
  if (kind === 'screenshot') clip.screenshot = extra.screenshot || '';
  if (kind === 'picture') clip.pictureUrl = extra.pictureUrl || '';
  if (kind === 'link') {
    clip.linkUrl = extra.linkUrl || '';
    clip.linkText = extra.linkText || '';
  }
  const place = { kind: ['page', 'journal', 'heading'].includes(form.place) ? form.place : 'page' };
  if (place.kind === 'page') {
    const folder = checkFolder(form.folder);
    if (folder !== null) place.folder = folder;
    place.journalLink = form.journalLink === true;
  }
  if (place.kind === 'heading') {
    place.page = form.headingPage || '';
    place.heading = form.heading || '';
  }
  clip.place = place;
  return clip;
}

/** What is wrong with a form before it is sent: a message key of _locales, or "". */
export function formProblem(form) {
  if (form.place === 'page' && checkFolder(form.folder) === null) return 'errorFolder';
  if (form.place === 'heading' && !form.headingPage) return 'errorNoPage';
  if (form.place === 'heading' && !form.heading) return 'errorNoHeading';
  return '';
}
