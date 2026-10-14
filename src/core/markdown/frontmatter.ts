/**
 * YAML frontmatter (docs/SYNTAX.md §4 "Page properties") — a small, dependency-free YAML
 * subset parser and a matching serializer.
 *
 * Supported: block mappings (any nesting), block sequences (`- a`, `- key: v`), flow
 * sequences / mappings (`[a, "b c"]`, `{a: 1}`, also spanning lines), plain / single- /
 * double-quoted scalars, `|` and `>` block scalars (with `-` / `+` chomping), comments,
 * `null`/`~`, booleans and numbers. Dates stay strings. Anchors, aliases, tags and
 * multi-document streams are not supported (read as plain text).
 *
 * Malformed input never throws: unparseable lines are skipped and whatever parsed is returned.
 */
import { toLocalDate } from '@/core/dates';

/** Result of {@link parseFrontmatter}. */
export interface FrontmatterResult {
  data: Record<string, unknown>;
  /** Offset of the first character after the frontmatter block (0 when there is none). */
  bodyStart: number;
  /** 0-based line where the body starts (0 when there is no frontmatter). */
  bodyStartLine: number;
}

/** Location of a frontmatter block in a document. */
export interface FrontmatterBlock {
  /** Line of the closing `---` / `...`. */
  endLine: number;
  /** YAML source between the delimiters. */
  yaml: string;
  /** Offset where the YAML source starts. */
  yamlStart: number;
  bodyStart: number;
  bodyStartLine: number;
}

const MAX_DEPTH = 64;
const MAX_FLOW_LINES = 500;

/**
 * Locate a frontmatter block: the document (after an optional BOM) must start with a `---`
 * line and contain a closing `---` or `...` line. Returns null otherwise.
 */
export function findFrontmatter(content: string): FrontmatterBlock | null {
  const start = content.charCodeAt(0) === 0xfeff ? 1 : 0;
  let end = lineEnd(content, start);
  if (content.slice(start, end).trimEnd() !== '---' || end >= content.length) return null;
  let pos = nextLineStart(content, end);
  const yamlStart = pos;
  for (let line = 1; ; line++) {
    end = lineEnd(content, pos);
    const text = content.slice(pos, end).trimEnd();
    if (text === '---' || text === '...') {
      return {
        endLine: line,
        yaml: content.slice(yamlStart, pos),
        yamlStart,
        bodyStart: nextLineStart(content, end),
        bodyStartLine: line + 1,
      };
    }
    if (end >= content.length) return null;
    pos = nextLineStart(content, end);
  }
}

function isBreak(c: number): boolean {
  return c === 10 || c === 13;
}

function lineEnd(s: string, from: number): number {
  let i = from;
  while (i < s.length && !isBreak(s.charCodeAt(i))) i++;
  return i;
}

function nextLineStart(s: string, end: number): number {
  if (end >= s.length) return s.length;
  return s.charCodeAt(end) === 13 && s.charCodeAt(end + 1) === 10 ? end + 2 : end + 1;
}

/**
 * Parse the YAML frontmatter at the start of `content`. Without frontmatter returns
 * `{ data: {}, bodyStart: 0, bodyStartLine: 0 }`. Never throws.
 */
export function parseFrontmatter(content: string): FrontmatterResult {
  const fm = findFrontmatter(content);
  if (!fm) return { data: {}, bodyStart: 0, bodyStartLine: 0 };
  return { data: parseYaml(fm.yaml), bodyStart: fm.bodyStart, bodyStartLine: fm.bodyStartLine };
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

interface YLine {
  raw: string;
  indent: number;
  /** Content after indentation, trailing whitespace removed. */
  content: string;
}

/** Define an own enumerable property (safe for keys like `__proto__`). */
export function setOwn(obj: Record<string, unknown>, key: string, value: unknown): void {
  if (key === '__proto__') Object.defineProperty(obj, key, { value, enumerable: true, writable: true, configurable: true });
  else obj[key] = value;
}

function toYLine(raw: string): YLine {
  let indent = 0;
  let i = 0;
  for (; i < raw.length; i++) {
    const c = raw[i];
    if (c === ' ') indent++;
    else if (c === '\t') indent += 2;
    else break;
  }
  return { raw, indent, content: raw.slice(i).trimEnd() };
}

function isSkippable(l: YLine): boolean {
  return l.content === '' || l.content.startsWith('#');
}

function isSeqItem(content: string): boolean {
  return content === '-' || content.startsWith('- ') || content.startsWith('-\t');
}

/** Single-character escapes of double-quoted YAML scalars. */
const DQ_ESCAPES: Readonly<Record<string, string>> = {
  n: '\n',
  t: '\t',
  r: '\r',
  '0': '\0',
  b: '\b',
  f: '\f',
  e: '\x1b',
  a: '\x07',
  v: '\v',
  '"': '"',
  '\\': '\\',
  '/': '/',
  ' ': ' ',
  '\t': '\t',
  N: '\u0085',
  _: '\u00a0',
  L: '\u2028',
  P: '\u2029',
};

/** Read a quoted scalar starting at `pos` (which holds the quote). */
function readQuoted(s: string, pos: number): { value: string; end: number } | null {
  const q = s[pos];
  if (q === "'") {
    let out = '';
    let i = pos + 1;
    while (i < s.length) {
      const c = s[i]!;
      if (c === "'") {
        if (s[i + 1] === "'") {
          out += "'";
          i += 2;
          continue;
        }
        return { value: out, end: i + 1 };
      }
      out += c;
      i++;
    }
    return null;
  }
  if (q !== '"') return null;
  let out = '';
  let i = pos + 1;
  while (i < s.length) {
    const c = s[i]!;
    if (c === '"') return { value: out, end: i + 1 };
    if (c === '\\') {
      const e = s[i + 1];
      if (e !== undefined && Object.prototype.hasOwnProperty.call(DQ_ESCAPES, e)) {
        out += DQ_ESCAPES[e];
        i += 2;
        continue;
      }
      const hexLen = e === 'x' ? 2 : e === 'u' ? 4 : e === 'U' ? 8 : 0;
      if (hexLen) {
        const hex = s.slice(i + 2, i + 2 + hexLen);
        const code = /^[0-9A-Fa-f]+$/.test(hex) && hex.length === hexLen ? parseInt(hex, 16) : NaN;
        if (!Number.isNaN(code) && code <= 0x10ffff) {
          out += String.fromCodePoint(code);
          i += 2 + hexLen;
          continue;
        }
      }
      out += e ?? '';
      i += 2;
      continue;
    }
    out += c;
    i++;
  }
  return null;
}

const NUMBER_RE = /^[-+]?(?:(?:0|[1-9]\d*)(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?$/;

/** Resolve a plain scalar: null, booleans, numbers; everything else (dates too) is a string. */
export function resolvePlain(s: string): unknown {
  const t = s.trim();
  if (t === '' || t === '~' || t === 'null' || t === 'Null' || t === 'NULL') return null;
  if (t === 'true' || t === 'True' || t === 'TRUE') return true;
  if (t === 'false' || t === 'False' || t === 'FALSE') return false;
  if (NUMBER_RE.test(t)) {
    const n = Number(t);
    if (Number.isFinite(n) && (!Number.isInteger(n) || Number.isSafeInteger(n) || /[.eE]/.test(t))) return n;
  }
  return t;
}

/** Remove a ` #comment` from a plain scalar. */
function stripComment(s: string): string {
  if (s.startsWith('#')) return '';
  const i = s.search(/[ \t]#/);
  return (i === -1 ? s : s.slice(0, i)).trim();
}

class YamlParser {
  private i = 0;
  private readonly lines: YLine[];

  constructor(src: string) {
    const raw = src.split(/\r\n|\r|\n/);
    // The break ending the last line does not start another (blank) line (matters for `|+`).
    if (raw.length > 1 && raw[raw.length - 1] === '') raw.pop();
    this.lines = raw.map(toYLine);
  }

  private peek(): YLine | undefined {
    while (this.i < this.lines.length && isSkippable(this.lines[this.i]!)) this.i++;
    return this.lines[this.i];
  }

  /** Parse the whole document as a mapping, skipping malformed lines. */
  parseRoot(): Record<string, unknown> {
    const root: Record<string, unknown> = {};
    while (this.peek()) {
      const before = this.i;
      const line = this.peek()!;
      if (isSeqItem(line.content) || !splitKey(line.content)) {
        this.i++; // a top-level sequence/scalar is not frontmatter: skip
        continue;
      }
      this.parseMapInto(root, line.indent, 0);
      if (this.i === before) this.i++;
    }
    return root;
  }

  /** Parse `key: value` lines at exactly `indent` into `obj`. */
  private parseMapInto(obj: Record<string, unknown>, indent: number, depth: number): void {
    for (;;) {
      const line = this.peek();
      if (!line || line.indent < indent) return;
      if (line.indent > indent) {
        this.i++; // unexpected deeper line (malformed): skip
        continue;
      }
      const kv = splitKey(line.content);
      if (!kv) {
        this.i++;
        continue;
      }
      this.i++;
      setOwn(obj, kv.key, this.parseValue(kv.rest, indent, depth + 1, true));
    }
  }

  /** Parse the value after `key:` / `- ` whose owner line has indentation `indent`. */
  private parseValue(rest: string, indent: number, depth: number, allowSameIndentSeq: boolean): unknown {
    if (depth > MAX_DEPTH) {
      this.skipDeeper(indent);
      return null;
    }
    if (rest === '' || rest.startsWith('#')) return this.parseNested(indent, depth, allowSameIndentSeq);
    const c = rest[0];
    if (c === '|' || c === '>') {
      const header = /^([|>])([+-]?)(\d?)([+-]?)[ \t]*(?:#.*)?$/.exec(rest);
      if (header) return this.parseBlockScalar(header[1] === '>', header[2] || header[4] || '', indent);
    }
    if (c === '[' || c === '{') return this.parseFlowValue(rest, indent);
    if (c === '"' || c === "'") return this.parseQuotedValue(rest, indent);
    return this.parsePlainValue(rest, indent);
  }

  private skipDeeper(indent: number): void {
    for (;;) {
      const l = this.peek();
      if (!l || l.indent <= indent) return;
      this.i++;
    }
  }

  /** Value on the following lines: a deeper block, or a same-indent sequence under a key. */
  private parseNested(indent: number, depth: number, allowSameIndentSeq: boolean): unknown {
    const next = this.peek();
    if (!next) return null;
    if (next.indent > indent) return this.parseBlock(next.indent, depth);
    if (allowSameIndentSeq && next.indent === indent && isSeqItem(next.content)) return this.parseSeq(indent, depth);
    return null;
  }

  /** A block node at `indent`: sequence, mapping or multi-line plain scalar. */
  private parseBlock(indent: number, depth: number): unknown {
    const line = this.peek()!;
    if (isSeqItem(line.content)) return this.parseSeq(indent, depth);
    if (splitKey(line.content)) {
      const obj: Record<string, unknown> = {};
      this.parseMapInto(obj, indent, depth);
      return obj;
    }
    const first = this.lines[this.i]!;
    this.i++;
    return this.parsePlainValue(first.content, indent - 1);
  }

  private parseSeq(indent: number, depth: number): unknown[] {
    const arr: unknown[] = [];
    for (;;) {
      const line = this.peek();
      if (!line || line.indent !== indent || !isSeqItem(line.content)) return arr;
      this.i++;
      const afterDash = line.content.slice(1);
      const rest = afterDash.trim();
      const kv = rest && rest[0] !== '"' && rest[0] !== "'" && rest[0] !== '[' && rest[0] !== '{' ? splitKey(rest) : null;
      if (kv) {
        // "- key: value" starts a mapping whose keys are aligned with "key".
        const itemIndent = indent + 1 + (afterDash.length - afterDash.trimStart().length);
        const obj: Record<string, unknown> = {};
        setOwn(obj, kv.key, this.parseValue(kv.rest, itemIndent, depth + 1, true));
        this.parseMapInto(obj, itemIndent, depth + 1);
        arr.push(obj);
      } else if (isSeqItem(rest)) {
        // "- - a": nested sequence on the same line (rare) — read as a one-element list.
        arr.push([resolvePlain(stripComment(rest.slice(1)))]);
      } else {
        arr.push(this.parseValue(rest, indent, depth + 1, false));
      }
    }
  }

  private parseBlockScalar(folded: boolean, chomp: string, indent: number): string {
    const raw: string[] = [];
    let contentIndent = -1;
    while (this.i < this.lines.length) {
      const l = this.lines[this.i]!;
      if (l.content === '') {
        raw.push('');
        this.i++;
        continue;
      }
      if (contentIndent === -1) {
        if (l.indent <= indent) break;
        contentIndent = l.indent;
      }
      if (l.indent < contentIndent) break;
      raw.push(sliceIndent(l.raw, contentIndent));
      this.i++;
    }
    // Trailing blank lines are only kept with '+' chomping.
    let trailing = 0;
    while (raw.length && raw[raw.length - 1] === '') {
      raw.pop();
      trailing++;
    }
    const body = folded ? foldLines(raw) : raw.join('\n');
    if (raw.length === 0) return '';
    if (chomp === '-') return body;
    if (chomp === '+') return body + '\n'.repeat(trailing + 1);
    return body + '\n';
  }

  /** Flow collection, possibly continued on following deeper lines until brackets balance. */
  private parseFlowValue(rest: string, indent: number): unknown {
    let text = rest;
    let guard = 0;
    while (!flowBalanced(text) && guard++ < MAX_FLOW_LINES) {
      const next = this.lines[this.i];
      if (!next) break;
      // Deeper lines continue the collection; a closing bracket may sit at the key's indent.
      const closing = next.content.startsWith(']') || next.content.startsWith('}');
      if (next.indent <= indent && next.content !== '' && !closing) break;
      text += ' ' + next.content;
      this.i++;
    }
    const p = new FlowParser(text);
    const v = p.parse();
    return v === FLOW_FAIL ? stripComment(text) : v;
  }

  private parseQuotedValue(rest: string, indent: number): unknown {
    let text = rest;
    let q = readQuoted(text, 0);
    let guard = 0;
    while (!q && guard++ < MAX_FLOW_LINES) {
      const next = this.lines[this.i];
      if (!next || (next.indent <= indent && next.content !== '')) break;
      if (next.content === '') text += '\n';
      else text += (text.endsWith('\n') ? '' : ' ') + next.content;
      this.i++;
      q = readQuoted(text, 0);
    }
    if (!q) return stripComment(text);
    return q.value;
  }

  private parsePlainValue(rest: string, indent: number): unknown {
    let text = stripComment(rest);
    // Plain multi-line scalars: deeper lines continue the value.
    for (;;) {
      const next = this.lines[this.i];
      if (!next || next.content === '' || next.indent <= indent || next.content.startsWith('#')) break;
      text += ' ' + stripComment(next.content);
      this.i++;
    }
    return resolvePlain(text);
  }
}

/** YAML line folding for `>` scalars: single breaks become spaces, blank lines newlines. */
function foldLines(raw: string[]): string {
  let body = '';
  for (let k = 0; k < raw.length; k++) {
    const cur = raw[k]!;
    const prev = raw[k - 1];
    if (k === 0) body = cur;
    else if (cur === '') body += '\n';
    else if (prev === '') body += cur;
    else if (/^[ \t]/.test(cur) || /^[ \t]/.test(prev ?? '')) body += '\n' + cur;
    else body += ' ' + cur;
  }
  return body;
}

function sliceIndent(raw: string, n: number): string {
  let col = 0;
  let i = 0;
  while (i < raw.length && col < n && (raw[i] === ' ' || raw[i] === '\t')) {
    col += raw[i] === '\t' ? 2 : 1;
    i++;
  }
  return raw.slice(i);
}

/** Split `key: rest` (plain or quoted key). Null when the line is not a mapping entry. */
function splitKey(content: string): { key: string; rest: string } | null {
  const c0 = content[0];
  if (c0 === undefined || c0 === '#') return null;
  if (c0 === '"' || c0 === "'") {
    const q = readQuoted(content, 0);
    if (!q) return null;
    let j = q.end;
    while (content[j] === ' ' || content[j] === '\t') j++;
    if (content[j] !== ':') return null;
    const after = content[j + 1];
    if (after !== undefined && after !== ' ' && after !== '\t') return null;
    return { key: q.value, rest: content.slice(j + 1).trim() };
  }
  if (c0 === '[' || c0 === '{' || c0 === '|' || c0 === '>' || c0 === '%' || c0 === '@' || c0 === '`') return null;
  if (isSeqItem(content)) return null;
  for (let k = 0; k < content.length; k++) {
    const c = content[k];
    if (c === ':') {
      const after = content[k + 1];
      if (after === undefined || after === ' ' || after === '\t') {
        const key = content.slice(0, k).trim();
        return key ? { key, rest: content.slice(k + 1).trim() } : null;
      }
    } else if ((c === ' ' || c === '\t') && content[k + 1] === '#') return null;
  }
  return null;
}

/** True when `[`/`{` brackets outside quotes are balanced. */
function flowBalanced(s: string): boolean {
  let depth = 0;
  let quote: string | null = null;
  for (let i = 0; i < s.length; i++) {
    const c = s[i];
    if (quote) {
      if (quote === '"' && c === '\\') i++;
      else if (c === quote) quote = null;
      continue;
    }
    if (c === '"' || c === "'") quote = c;
    else if (c === '[' || c === '{') depth++;
    else if (c === ']' || c === '}') depth--;
    else if (c === '#' && (i === 0 || s[i - 1] === ' ') && depth <= 0) break;
  }
  return depth <= 0;
}

const FLOW_FAIL = Symbol('flow-fail');

/** Recursive-descent parser for flow collections. */
class FlowParser {
  private i = 0;
  private depth = 0;
  constructor(private readonly s: string) {}

  parse(): unknown {
    const v = this.value();
    if (v === FLOW_FAIL) return FLOW_FAIL;
    this.ws();
    if (this.i < this.s.length && this.s[this.i] !== '#') return FLOW_FAIL;
    return v;
  }

  private ws(): void {
    while (this.i < this.s.length && /\s/.test(this.s[this.i]!)) this.i++;
  }

  private value(): unknown {
    this.ws();
    const c = this.s[this.i];
    if (c === '[' || c === '{') {
      if (++this.depth > MAX_DEPTH) return FLOW_FAIL;
      const v = c === '[' ? this.seq() : this.map();
      this.depth--;
      return v;
    }
    if (c === '"' || c === "'") {
      const q = readQuoted(this.s, this.i);
      if (!q) return FLOW_FAIL;
      this.i = q.end;
      return q.value;
    }
    return resolvePlain(this.plain(false));
  }

  private plain(isKey: boolean): string {
    const start = this.i;
    while (this.i < this.s.length) {
      const c = this.s[this.i]!;
      if (c === ',' || c === ']' || c === '}' || c === '[' || c === '{') break;
      if (c === ':' && (isKey || /[\s,\]}]/.test(this.s[this.i + 1] ?? ' '))) break;
      this.i++;
    }
    return this.s.slice(start, this.i).trim();
  }

  private seq(): unknown[] | typeof FLOW_FAIL {
    this.i++; // [
    const out: unknown[] = [];
    for (;;) {
      this.ws();
      if (this.s[this.i] === ']') {
        this.i++;
        return out;
      }
      if (this.i >= this.s.length) return FLOW_FAIL;
      const start = this.i;
      const v = this.value();
      if (v === FLOW_FAIL) return FLOW_FAIL;
      this.ws();
      if (this.s[this.i] === ':' ) {
        // single-pair mapping inside a sequence: [a: 1]
        this.i++;
        const pv = this.value();
        if (pv === FLOW_FAIL) return FLOW_FAIL;
        const obj: Record<string, unknown> = {};
        setOwn(obj, String(v ?? ''), pv);
        out.push(obj);
      } else if (this.i > start || v !== null) out.push(v);
      this.ws();
      if (this.s[this.i] === ',') this.i++;
      else if (this.s[this.i] !== ']') return FLOW_FAIL;
    }
  }

  private map(): Record<string, unknown> | typeof FLOW_FAIL {
    this.i++; // {
    const out: Record<string, unknown> = {};
    for (;;) {
      this.ws();
      if (this.s[this.i] === '}') {
        this.i++;
        return out;
      }
      if (this.i >= this.s.length) return FLOW_FAIL;
      let key: string;
      const c = this.s[this.i];
      if (c === '"' || c === "'") {
        const q = readQuoted(this.s, this.i);
        if (!q) return FLOW_FAIL;
        this.i = q.end;
        key = q.value;
      } else key = this.plain(true);
      this.ws();
      let v: unknown = null;
      if (this.s[this.i] === ':') {
        this.i++;
        v = this.value();
        if (v === FLOW_FAIL) return FLOW_FAIL;
      }
      setOwn(out, key, v);
      this.ws();
      if (this.s[this.i] === ',') this.i++;
      else if (this.s[this.i] !== '}') return FLOW_FAIL;
    }
  }
}

/**
 * Parse a YAML subset document into a mapping (see the module comment). Non-mapping
 * documents yield `{}`. Never throws.
 */
export function parseYaml(src: string): Record<string, unknown> {
  const p = new YamlParser(src);
  try {
    return p.parseRoot();
  } catch {
    // Defensive: the parser is written not to throw, but malformed input must never break indexing.
    return {};
  }
}

// ---------------------------------------------------------------------------
// Serializer
// ---------------------------------------------------------------------------

const INDICATOR_START_RE = /^[-?:,[\]{}#&*!|>'"%@`]/;

/**
 * Plain scalars that this parser keeps as strings but other YAML readers (YAML 1.1 such as
 * js-yaml / Obsidian, or full YAML 1.2) would turn into booleans or numbers: `yes`/`no`/`on`/
 * `off`/`y`/`n`, leading-zero / octal / hex / binary / underscored / sexagesimal numbers,
 * `.inf` and `.nan`.
 */
const FOREIGN_SCALAR_RE =
  /^(?:y|Y|yes|Yes|YES|n|N|no|No|NO|on|On|ON|off|Off|OFF|[-+]?\.(?:inf|Inf|INF)|\.(?:nan|NaN|NAN)|[-+]?0[0-7_]+|[-+]?0[xX][0-9a-fA-F_]+|[-+]?0[oO][0-7_]+|[-+]?0[bB][01_]+|[-+]?[0-9][0-9_]*(?:\.[0-9_]*)?(?:[eE][-+]?[0-9]+)?|[-+]?[0-9][0-9_]*(?::[0-5]?[0-9])+(?:\.[0-9_]*)?)$/;

/** True when a string must be quoted to survive a YAML round trip as the same string. */
function needsQuotes(s: string): boolean {
  if (s === '' || s !== s.trim()) return true;
  if (INDICATOR_START_RE.test(s)) return true;
  if (/: |:$| #|\t/.test(s)) return true;
  // eslint-disable-next-line no-control-regex
  if (/[\u0000-\u001f\u007f\u2028\u2029\ufeff]/.test(s)) return true;
  if (FOREIGN_SCALAR_RE.test(s)) return true;
  return resolvePlain(s) !== s;
}

function formatString(s: string): string {
  return needsQuotes(s) ? JSON.stringify(s) : s;
}

function isPlainObject(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null && !Array.isArray(v) && !(v instanceof Date);
}

function formatDate(d: Date): string {
  if (Number.isNaN(d.getTime())) return '';
  const date = toLocalDate(d);
  if (d.getHours() === 0 && d.getMinutes() === 0) return date;
  return `${date} ${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
}

/** Scalar in flow/inline position (no block scalars). Undefined for unsupported values. */
function formatScalar(v: unknown): string | undefined {
  if (v === null) return 'null';
  switch (typeof v) {
    case 'boolean':
      return v ? 'true' : 'false';
    case 'number':
      return Number.isFinite(v) ? String(v) : JSON.stringify(String(v));
    case 'bigint':
      return String(v);
    case 'string':
      return formatString(v);
    default:
      if (v instanceof Date) return formatDate(v);
      return undefined;
  }
}

function formatFlow(v: unknown): string {
  if (Array.isArray(v)) return `[${v.map(formatFlow).join(', ')}]`;
  if (isPlainObject(v)) {
    return `{${Object.entries(v)
      .filter(([, x]) => x !== undefined)
      .map(([k, x]) => `${formatString(k)}: ${formatFlow(x)}`)
      .join(', ')}}`;
  }
  return formatScalar(v) ?? 'null';
}

/** Block scalar lines for a multi-line string, or null when quoting is safer. */
function blockScalar(s: string, indent: string): string[] | null {
  if (s.includes('\r') || /^[ \t]/.test(s)) return null;
  // eslint-disable-next-line no-control-regex
  if (/[\u0000-\u0008\u000b-\u001f\u007f]/.test(s)) return null;
  let body = s;
  let trailing = 0;
  while (body.endsWith('\n')) {
    body = body.slice(0, -1);
    trailing++;
  }
  if (body === '') return null;
  const header = trailing === 0 ? '|-' : trailing === 1 ? '|' : '|+';
  const lines = body.split('\n').map((l) => (l === '' ? '' : indent + l));
  for (let k = 1; k < trailing; k++) lines.push('');
  return [header, ...lines];
}

function emitMap(obj: Record<string, unknown>, indent: number, out: string[]): void {
  const pad = ' '.repeat(indent);
  for (const [key, value] of Object.entries(obj)) {
    if (value === undefined || typeof value === 'function' || typeof value === 'symbol') continue;
    const k = formatString(key);
    if (Array.isArray(value)) {
      if (value.length === 0) out.push(`${pad}${k}: []`);
      else {
        out.push(`${pad}${k}:`);
        emitSeq(value, indent + 2, out);
      }
    } else if (isPlainObject(value)) {
      if (Object.keys(value).length === 0) out.push(`${pad}${k}: {}`);
      else {
        out.push(`${pad}${k}:`);
        emitMap(value, indent + 2, out);
      }
    } else if (value === null) {
      out.push(`${pad}${k}:`);
    } else if (typeof value === 'string' && value.includes('\n')) {
      const block = blockScalar(value, ' '.repeat(indent + 2));
      if (block) {
        out.push(`${pad}${k}: ${block[0]}`);
        out.push(...block.slice(1));
      } else out.push(`${pad}${k}: ${JSON.stringify(value)}`);
    } else {
      const s = formatScalar(value);
      if (s !== undefined) out.push(`${pad}${k}: ${s}`);
    }
  }
}

function emitSeq(arr: unknown[], indent: number, out: string[]): void {
  const pad = ' '.repeat(indent);
  for (const item of arr) {
    if (item === undefined || typeof item === 'function' || typeof item === 'symbol') {
      out.push(`${pad}- null`);
    } else if (Array.isArray(item)) {
      out.push(`${pad}- ${formatFlow(item)}`);
    } else if (isPlainObject(item)) {
      const inner: string[] = [];
      emitMap(item, indent + 2, inner);
      if (inner.length === 0) out.push(`${pad}- {}`);
      else {
        out.push(`${pad}- ${inner[0]!.slice(indent + 2)}`);
        out.push(...inner.slice(1));
      }
    } else if (typeof item === 'string' && item.includes('\n')) {
      out.push(`${pad}- ${JSON.stringify(item)}`);
    } else {
      out.push(`${pad}- ${formatScalar(item) ?? 'null'}`);
    }
  }
}

/** Serialize a mapping as YAML (block style, 2-space indent, Obsidian-like lists). */
export function stringifyYaml(data: Record<string, unknown>): string {
  const out: string[] = [];
  emitMap(data, 0, out);
  return out.length ? out.join('\n') + '\n' : '';
}

/**
 * Serialize `data` as a complete frontmatter block: `---\n<yaml>---\n`. Returns `''` when
 * there is nothing to write (empty object / only undefined values). Readable back with
 * {@link parseFrontmatter}.
 */
export function stringifyFrontmatter(data: Record<string, unknown>): string {
  const yaml = stringifyYaml(data);
  return yaml ? `---\n${yaml}---\n` : '';
}
