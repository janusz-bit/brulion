/**
 * WebDAV XML: the PROPFIND request body and a tolerant `multistatus` parser (any namespace
 * prefix or none, several propstat blocks, missing properties, entity-encoded ETags).
 */
import { XMLParser } from 'fast-xml-parser';
import { WebDavError } from './errors';
import { normalizeEtag } from './url';

/** PROPFIND body asking for the properties the sync engine needs. */
export const PROPFIND_BODY =
  '<?xml version="1.0" encoding="utf-8"?>\n' +
  '<d:propfind xmlns:d="DAV:">\n' +
  '  <d:prop>\n' +
  '    <d:getetag/>\n' +
  '    <d:getlastmodified/>\n' +
  '    <d:getcontentlength/>\n' +
  '    <d:resourcetype/>\n' +
  '  </d:prop>\n' +
  '</d:propfind>\n';

/** One `<response>` of a multistatus document. */
export interface DavResource {
  /** The href exactly as sent by the server (still percent-encoded). */
  href: string;
  isCollection: boolean;
  /** Normalised ETag ('' if missing). */
  etag: string;
  /** ms since epoch, 0 if missing or unparsable. */
  mtime: number;
  /** Bytes, 0 if missing. */
  size: number;
}

const ARRAY_TAGS = new Set(['response', 'propstat']);

const parser = new XMLParser({
  removeNSPrefix: true,
  ignoreAttributes: true,
  parseTagValue: false,
  trimValues: true,
  ignoreDeclaration: true,
  isArray: (name) => ARRAY_TAGS.has(name),
});

type XmlNode = Record<string, unknown>;

function isNode(v: unknown): v is XmlNode {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

/** Text content of a parsed element (string, number or `{ '#text': ... }`). */
function textOf(v: unknown): string {
  if (typeof v === 'string') return v;
  if (typeof v === 'number' || typeof v === 'boolean') return String(v);
  if (isNode(v) && '#text' in v) return textOf(v['#text']);
  if (Array.isArray(v)) return textOf(v[0]);
  return '';
}

function asArray(v: unknown): unknown[] {
  if (v === undefined || v === null) return [];
  return Array.isArray(v) ? v : [v];
}

/** HTTP status code from "HTTP/1.1 200 OK"; undefined when absent/unparsable. */
export function parseStatusLine(line: string): number | undefined {
  const m = /\b(\d{3})\b/.exec(line);
  return m ? Number(m[1]) : undefined;
}

function isSuccess(status: unknown): boolean {
  const s = textOf(status);
  if (s === '') return true;
  const code = parseStatusLine(s);
  return code === undefined || (code >= 200 && code < 300);
}

/** Merge the `<prop>` contents of all successful propstat blocks. */
function successfulProps(response: XmlNode): XmlNode {
  const props: XmlNode = {};
  for (const ps of asArray(response.propstat)) {
    if (!isNode(ps) || !isSuccess(ps.status)) continue;
    for (const prop of asArray(ps.prop)) if (isNode(prop)) Object.assign(props, prop);
  }
  return props;
}

function isCollectionType(resourcetype: unknown): boolean {
  for (const rt of asArray(resourcetype)) {
    if (isNode(rt) && 'collection' in rt) return true;
  }
  return false;
}

function parseMtime(v: unknown): number {
  const s = textOf(v).trim();
  if (!s) return 0;
  const t = Date.parse(s);
  return Number.isFinite(t) ? t : 0;
}

function parseSize(v: unknown): number {
  const n = Number(textOf(v).trim());
  return Number.isFinite(n) && n >= 0 ? Math.floor(n) : 0;
}

/**
 * Parse a `207 Multi-Status` body into resources. Responses with a non-2xx response status
 * are skipped. Throws `WebDavError('parse')` for documents that are not a multistatus.
 */
export function parseMultistatus(xml: string): DavResource[] {
  let doc: unknown;
  try {
    doc = parser.parse(xml);
  } catch (e) {
    throw new WebDavError('parse', 207, `Invalid WebDAV XML: ${e instanceof Error ? e.message : String(e)}`);
  }
  if (!isNode(doc) || !('multistatus' in doc)) {
    throw new WebDavError('parse', 207, 'Invalid WebDAV response: no <multistatus> element');
  }
  const ms = doc.multistatus;
  if (!isNode(ms)) return [];
  const out: DavResource[] = [];
  for (const r of asArray(ms.response)) {
    if (!isNode(r)) continue;
    const href = textOf(r.href).trim();
    if (!href || !isSuccess(r.status)) continue;
    const props = successfulProps(r);
    out.push({
      href,
      isCollection: isCollectionType(props.resourcetype),
      etag: normalizeEtag(textOf(props.getetag)),
      mtime: parseMtime(props.getlastmodified),
      size: parseSize(props.getcontentlength),
    });
  }
  return out;
}
