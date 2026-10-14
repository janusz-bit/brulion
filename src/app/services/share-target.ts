/**
 * PWA share target (vite.config.ts manifest: `./?share-target&title=…&text=…&url=…`): text and
 * links shared from other apps become a quick capture in today's journal.
 */

export interface SharedContent {
  title: string;
  text: string;
  url: string;
}

const PARAMS = ['share-target', 'title', 'text', 'url'] as const;

/** The shared content in a launch URL, or null when the URL is not a share-target launch. */
export function parseShareTarget(href: string): SharedContent | null {
  let url: URL;
  try {
    url = new URL(href);
  } catch {
    return null;
  }
  if (!url.searchParams.has('share-target')) return null;
  const get = (k: string) => (url.searchParams.get(k) ?? '').trim();
  const shared = { title: get('title'), text: get('text'), url: get('url') };
  return shared.title || shared.text || shared.url ? shared : null;
}

/** `href` without the share-target parameters (for history.replaceState). */
export function stripShareParams(href: string): string {
  try {
    const url = new URL(href);
    for (const p of PARAMS) url.searchParams.delete(p);
    return url.href;
  } catch {
    return href;
  }
}

/**
 * One capture text: "Title — text url". Android puts the link into `text` as well, so parts
 * already contained in another part are dropped; a URL becomes a Markdown link with the title.
 */
export function composeSharedText(s: SharedContent): string {
  const title = s.title.replace(/\s+/g, ' ').trim();
  let text = s.text.trim();
  let url = s.url.trim();
  // A URL inside the text (common on Android) is used as the link.
  if (!url) {
    const m = /https?:\/\/\S+/.exec(text);
    if (m) {
      url = m[0];
      text = (text.slice(0, m.index) + text.slice(m.index + m[0].length)).trim();
    }
  } else if (text.includes(url)) {
    text = text.replace(url, '').trim();
  }
  if (title && text === title) text = '';
  const head = url ? (title ? `[${title.replace(/[[\]]/g, '')}](${url})` : `<${url}>`) : title;
  return [head, text].filter(Boolean).join(' — ');
}
