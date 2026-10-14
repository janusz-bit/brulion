/**
 * Strict allowlist HTML sanitizer working on strings — the fallback used where no DOM is
 * available (plain Node, workers). It is deliberately conservative: it re-serialises every
 * allowed tag from parsed parts (so no attribute or tag text is copied verbatim), escapes
 * anything that does not parse as a well-formed tag, drops comments / doctypes / processing
 * instructions, removes dangerous elements with their content, and validates URLs after decoding
 * entities (URL attributes with unknown named entities are dropped).
 */
import {
  ALLOWED_ATTRS,
  ALLOWED_TAGS,
  ARIA_ATTR_RE,
  BOOLEAN_ATTRS,
  DATA_ATTR_RE,
  DROP_WITH_CONTENT,
  INPUT_ATTRS,
  URL_ATTRS,
  VOID_TAGS,
  isExternalWebUrl,
  isSafeUrl,
  type SanitizeOptions,
} from './sanitize-policy';

const ALLOWED_TAG_SET = new Set(ALLOWED_TAGS);
const ALLOWED_ATTR_SET = new Set(ALLOWED_ATTRS);
const DROP_SET = new Set(DROP_WITH_CONTENT);

const START_TAG_RE = /<([A-Za-z][A-Za-z0-9-]*)((?:\s+[^\s"'>/=<]+(?:\s*=\s*(?:"[^"]*"|'[^']*'|[^\s"'=<>`]+))?)*)\s*(\/?)>/y;
const END_TAG_RE = /<\/([A-Za-z][A-Za-z0-9-]*)[^>]*>/y;
const ATTR_RE = /([^\s"'>/=<]+)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s"'=<>`]+)))?/g;

const NAMED_ENTITIES: Readonly<Record<string, string>> = {
  amp: '&', lt: '<', gt: '>', quot: '"', apos: "'", nbsp: ' ', colon: ':', tab: '\t', newline: '\n',
  sol: '/', lpar: '(', rpar: ')', period: '.', comma: ',', semi: ';', excl: '!', quest: '?', num: '#',
  percnt: '%', equals: '=', plus: '+', lowbar: '_', hyphen: '-', dash: '-', commat: '@', lsqb: '[',
  rsqb: ']', lbrack: '[', rbrack: ']', lcub: '{', rcub: '}', grave: '`', bsol: '\\', ast: '*', dollar: '$',
};

/** Decode character references; returns the text and whether unknown named entities remain. */
export function decodeEntities(s: string): { text: string; unknown: boolean } {
  let unknown = false;
  const text = s.replace(/&(#[0-9]{1,8}|#[xX][0-9A-Fa-f]{1,7}|[A-Za-z][A-Za-z0-9]{0,31});?/g, (m, body: string) => {
    if (body[0] === '#') {
      const code = body[1] === 'x' || body[1] === 'X' ? parseInt(body.slice(2), 16) : parseInt(body.slice(1), 10);
      if (!Number.isFinite(code) || code <= 0 || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return '\uFFFD';
      return String.fromCodePoint(code);
    }
    const named = NAMED_ENTITIES[body.toLowerCase()];
    if (named !== undefined && m.endsWith(';')) return named;
    if (m.endsWith(';')) unknown = true;
    return m;
  });
  return { text, unknown };
}

/** Escape text content (keeps valid character references, escapes stray `&`, `<`, `>`). */
function escapeText(s: string): string {
  return s
    .replace(/&(?!(?:#[0-9]{1,8}|#[xX][0-9A-Fa-f]{1,7}|[A-Za-z][A-Za-z0-9]{0,31});)/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .split('\u0000')
    .join('');
}

function escapeAttr(s: string): string {
  return s.replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

function attrAllowed(tag: string, name: string): boolean {
  if (name.startsWith('on')) return false;
  if (tag === 'input') return INPUT_ATTRS.has(name) || DATA_ATTR_RE.test(name) || ARIA_ATTR_RE.test(name);
  return ALLOWED_ATTR_SET.has(name) || DATA_ATTR_RE.test(name) || ARIA_ATTR_RE.test(name);
}

/** Rebuild an allowed start tag from its raw attribute source. */
function rebuildStartTag(tag: string, rawAttrs: string, allowed: ReadonlySet<string>, interactive: boolean): string {
  const attrs = new Map<string, string | true>();
  ATTR_RE.lastIndex = 0;
  let m: RegExpExecArray | null;
  while ((m = ATTR_RE.exec(rawAttrs)) !== null) {
    const name = m[1]!.toLowerCase();
    if (attrs.has(name) || !attrAllowed(tag, name)) continue;
    const raw = m[2] ?? m[3] ?? m[4];
    if (raw === undefined) {
      attrs.set(name, true);
      continue;
    }
    const { text, unknown } = decodeEntities(raw);
    if (URL_ATTRS.has(name) && (unknown || !isSafeUrl(text, tag, name, allowed))) continue;
    if (name === 'target' && text.toLowerCase() !== '_blank') continue;
    if (name === 'id' && !/^[^\s]{1,200}$/.test(text)) continue;
    attrs.set(name, text);
  }
  if (tag === 'a') {
    const href = attrs.get('href');
    if (typeof href === 'string' && isExternalWebUrl(href)) attrs.set('target', '_blank');
    if (attrs.has('target')) attrs.set('rel', 'noopener noreferrer');
  }
  if (tag === 'input') {
    attrs.set('type', 'checkbox');
    if (!interactive) attrs.set('disabled', true);
  }
  if (tag === 'audio' || tag === 'video') attrs.set('controls', true);
  let out = `<${tag}`;
  for (const [name, value] of attrs) {
    if (value === true || (BOOLEAN_ATTRS.has(name) && value === '')) out += ` ${name}`;
    else out += ` ${name}="${escapeAttr(value)}"`;
  }
  return `${out}>`;
}

/** Index just after the closing tag of a dropped element (end of input when unclosed). */
function skipElement(html: string, tag: string, from: number): number {
  const re = new RegExp(`</${tag}(?=[\\s/>])[^>]*>`, 'ig');
  re.lastIndex = from;
  const m = re.exec(html);
  return m ? m.index + m[0].length : html.length;
}

/**
 * Sanitise `html` without a DOM: allowed tags are rebuilt from their parsed attributes, other
 * tags are removed (their text kept), dangerous elements are removed with their content, and
 * everything else is escaped.
 */
export function sanitizeHtmlString(html: string, opts: SanitizeOptions = {}): string {
  const allowed = new Set(opts.allowedUrls ?? []);
  const interactive = opts.interactive === true;
  const out: string[] = [];
  const n = html.length;
  let i = 0;
  while (i < n) {
    const lt = html.indexOf('<', i);
    if (lt === -1) {
      out.push(escapeText(html.slice(i)));
      break;
    }
    if (lt > i) out.push(escapeText(html.slice(i, lt)));
    i = lt;
    if (html.startsWith('<!--', i)) {
      const end = html.indexOf('-->', i + 4);
      i = end === -1 ? n : end + 3;
      continue;
    }
    const next = html[i + 1];
    if (next === '!' || next === '?') {
      const end = html.indexOf('>', i);
      i = end === -1 ? n : end + 1;
      continue;
    }
    END_TAG_RE.lastIndex = i;
    const end = END_TAG_RE.exec(html);
    if (end) {
      const tag = end[1]!.toLowerCase();
      if (ALLOWED_TAG_SET.has(tag) && !VOID_TAGS.has(tag)) out.push(`</${tag}>`);
      i += end[0].length;
      continue;
    }
    START_TAG_RE.lastIndex = i;
    const start = START_TAG_RE.exec(html);
    if (start) {
      const tag = start[1]!.toLowerCase();
      i += start[0].length;
      if (DROP_SET.has(tag)) {
        if (!VOID_TAGS.has(tag) && start[3] !== '/' && tag !== 'link' && tag !== 'meta' && tag !== 'base') i = skipElement(html, tag, i);
        continue;
      }
      if (ALLOWED_TAG_SET.has(tag)) out.push(rebuildStartTag(tag, start[2] ?? '', allowed, interactive));
      continue;
    }
    out.push('&lt;');
    i++;
  }
  return out.join('');
}
