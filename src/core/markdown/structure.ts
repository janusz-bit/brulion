/**
 * Block structure parser (docs/SYNTAX.md §4): a fast, line-based parser that splits a note
 * into blocks — list items (outline tree by indentation), paragraphs, headings (ATX and
 * setext), fenced code, tables, blockquotes, callouts (containers with child blocks),
 * thematic breaks, HTML blocks and the frontmatter / leading page-property block.
 *
 * It only finds structure and line ranges; inline content (links, tags, tokens, properties)
 * is interpreted later by ./parse.ts. Every line is visited a constant number of times
 * (callouts re-parse their own lines once per nesting level), so parsing is linear.
 *
 * Blockquotes that contain list items, fenced code, HTML or callouts are containers too (their
 * content becomes child blocks, so tasks in quotes are tasks); other quotes stay leaf blocks.
 *
 * Deliberate differences from CommonMark, chosen for notes written by Obsidian / Logseq:
 * indented code blocks are not recognised (indented lines are list items or text); a list
 * item is a child of the previous item when its marker is indented past that item's marker
 * (tab = 4 columns); a GFM table ends at the first line without `|`.
 */
import type { BlockKind } from '../types';
import { findFrontmatter, type FrontmatterBlock } from './frontmatter';
import { TAB_WIDTH, charsForColumns, indentOf, isBlank, splitLines, type Indent, type Lines } from './lines';
import { docCommentRanges, fenceOpen, isFenceClose, maskRanges, type FenceOpen } from './mask';
import { isStandaloneBlockId } from './tokens';

/** One line as seen by a container (container prefixes such as `> ` removed). */
export interface LineView {
  /** Original text. */
  text: string;
  /** Same text with `%%comments%%` blanked (same length). */
  masked: string;
  /** Document offset of `text[0]`. */
  offset: number;
  /** 0-based document line. */
  line: number;
}

/**
 * How a block uses a line: `text` (inline Markdown, scanned for links/tags/tokens), `code`
 * (code / raw HTML content, never scanned), `fence` (code fence delimiter), `blank` (blank
 * line between own lines), `meta` (syntax only: setext underline, `---`, standalone `^id`).
 */
export type LineRole = 'text' | 'code' | 'fence' | 'blank' | 'meta';

/** A line owned by a block. */
export interface OwnLine {
  view: LineView;
  /** Index in `view.text` where the block's content starts (after markers / indentation). */
  start: number;
  role: LineRole;
}

/** Structural block node. */
export interface Node {
  kind: BlockKind;
  line: number;
  endLine: number;
  ownEndLine: number;
  depth: number;
  own: OwnLine[];
  children: Node[];
  /** List items: visual column of the marker and of the content after it (else -1). */
  markerCol: number;
  contentCol: number;
  headingLevel?: number;
  lang?: string;
  calloutType?: string;
  calloutFold?: string;
  /** Id from a standalone `^id` line following the block (Obsidian style for tables/code). */
  trailingId?: string;
  trailingIdLine?: number;
  /** A blockquote whose content is parsed into child blocks (no own lines). */
  container: boolean;
}

/** Parsed document structure. */
export interface Structure {
  lines: Lines;
  /** Document with `%%comments%%` masked (=== content when there are none). */
  masked: string;
  /** Document-level `%%comment%%` ranges `[from, to)`, in order. */
  comments: [number, number][];
  views: LineView[];
  frontmatter: FrontmatterBlock | null;
  /** Roots in document order. */
  roots: Node[];
}

/** List item marker of a line. */
export interface ListMarker {
  indent: Indent;
  /** The marker itself: "-", "*", "+", "1.", "2)". */
  marker: string;
  /** Index after the marker. */
  markerEnd: number;
  /** Index where content starts (after the spaces following the marker). */
  contentStart: number;
  /** Visual column of the content. */
  contentCol: number;
}

const LIST_RE = /^([ \t]*)([-*+]|\d{1,9}[.)])(?:([ \t]+)|$)/;
export const HR_RE = /^[ \t]*(?:(?:\*[ \t]*){3,}|(?:-[ \t]*){3,}|(?:_[ \t]*){3,})$/;
export const HEADING_RE = /^[ \t]{0,3}(#{1,6})(?:[ \t]+|$)/;
const SETEXT_RE = /^[ \t]{0,3}(=+|-+)[ \t]*$/;
const QUOTE_RE = /^[ \t]{0,3}>[ \t]?/;
const CALLOUT_RE = /^\[!([^\]\s]+)\]([+-])?[ \t]*(.*)$/;
const DELIM_RE = /^[ \t]*\|?[ \t]*:?-+:?[ \t]*(?:\|[ \t]*:?-+:?[ \t]*)*\|?[ \t]*$/;
/** Logseq / Dataview property line `key:: value`. */
export const PROPERTY_RE = /^([A-Za-z0-9_-]+)::(?:[ \t]+(.*?))?[ \t]*$/;
const HTML1_RE = /^[ \t]{0,3}<(script|pre|style|textarea)(?=[\s>]|$)/i;
const HTML2_RE = /^[ \t]{0,3}<!--/;
const HTML6_RE =
  /^[ \t]{0,3}<\/?(?:address|article|aside|base|basefont|blockquote|body|caption|center|col|colgroup|dd|details|dialog|dir|div|dl|dt|fieldset|figcaption|figure|footer|form|frame|frameset|h[1-6]|head|header|hr|html|iframe|legend|li|link|main|menu|menuitem|nav|noframes|ol|optgroup|option|p|param|search|section|summary|table|tbody|td|tfoot|th|thead|title|tr|track|ul)(?=[\s/>]|$)/i;
const HTML7_RE =
  /^[ \t]{0,3}(?:<[A-Za-z][A-Za-z0-9-]*(?:\s+[A-Za-z_:][\w.:-]*(?:\s*=\s*(?:"[^"]*"|'[^']*'|[^\s"'=<>`]+))?)*\s*\/?>|<\/[A-Za-z][A-Za-z0-9-]*\s*>)[ \t]*$/;

/** Maximum callout nesting parsed as containers (deeper quotes are plain quote blocks). */
const MAX_NEST = 16;

/** Match a list item marker (not a thematic break). */
export function matchListMarker(t: string): ListMarker | null {
  const m = LIST_RE.exec(t);
  if (!m || HR_RE.test(t)) return null;
  const indent = indentOf(t);
  const marker = m[2]!;
  const markerEnd = m[1]!.length + marker.length;
  const spaces = m[3] ?? '';
  let col = indent.col + marker.length;
  if (spaces === '') col += 1;
  for (const ch of spaces) col = ch === '\t' ? col + TAB_WIDTH - (col % TAB_WIDTH) : col + 1;
  return { indent, marker, markerEnd, contentStart: markerEnd + spaces.length, contentCol: col };
}

/** Number of table cells in a row (pipes escaped with `\` don't count). */
function cellCount(row: string): number {
  let t = row.trim();
  if (t.startsWith('|')) t = t.slice(1);
  if (t.endsWith('|') && !t.endsWith('\\|')) t = t.slice(0, -1);
  let n = 1;
  for (let i = 0; i < t.length; i++) {
    if (t[i] === '\\') i++;
    else if (t[i] === '|') n++;
  }
  return n;
}

/** GFM table start: a header row with `|` followed by a matching delimiter row. */
export function isTableStart(t: string, next: string | undefined): boolean {
  if (next === undefined || t.indexOf('|') === -1 || next.indexOf('|') === -1 || next.indexOf('-') === -1) return false;
  if (!DELIM_RE.test(next)) return false;
  return cellCount(t) === cellCount(next);
}

/** HTML block start type (CommonMark types 1, 2, 6, 7), or 0. Type 7 cannot interrupt a paragraph. */
function htmlStart(t: string, inParagraph: boolean): number {
  if (t.indexOf('<') === -1) return 0;
  if (HTML1_RE.test(t)) return 1;
  if (HTML2_RE.test(t)) return 2;
  if (HTML6_RE.test(t)) return 6;
  if (!inParagraph && HTML7_RE.test(t)) return 7;
  return 0;
}

/** True when line `t` starts a block that ends a paragraph (or lazy continuation). */
function interruptsParagraph(t: string, next: string | undefined): boolean {
  if (HEADING_RE.test(t) || HR_RE.test(t) || QUOTE_RE.test(t) || fenceOpen(t)) return true;
  const lm = matchListMarker(t);
  if (lm) {
    const ordered = /\d/.test(lm.marker[0]!);
    const empty = isBlank(t.slice(lm.contentStart));
    if (!empty && (!ordered || /^0*1[.)]$/.test(lm.marker))) return true;
  }
  if (htmlStart(t, true)) return true;
  return isTableStart(t, next);
}

function newNode(kind: BlockKind, line: number, depth: number): Node {
  // All fields are initialised so every node has the same shape (keeps V8 property access monomorphic).
  return {
    kind,
    line,
    endLine: line,
    ownEndLine: line,
    depth,
    own: [],
    children: [],
    markerCol: -1,
    contentCol: -1,
    headingLevel: undefined,
    lang: undefined,
    calloutType: undefined,
    calloutFold: undefined,
    trailingId: undefined,
    trailingIdLine: undefined,
    container: false,
  };
}

/** A view of `v` without its first `n` characters (container prefix). */
function subView(v: LineView, n: number): LineView {
  return { text: v.text.slice(n), masked: v.masked.slice(n), offset: v.offset + n, line: v.line };
}

/** Parses the lines of one container (the document body or a callout's body). */
class ContainerParser {
  private i = 0;
  private readonly roots: Node[] = [];
  /** Open list items, outermost first. */
  private readonly stack: Node[] = [];
  private pendingBlanks: LineView[] = [];
  /** The previous line was paragraph text of the innermost open list item. */
  private lazyOk = false;

  constructor(
    private readonly views: LineView[],
    private readonly depth: number,
    private readonly nest: number,
  ) {}

  run(): Node[] {
    const views = this.views;
    while (this.i < views.length) {
      const v = views[this.i]!;
      const t = v.masked;
      if (isBlank(t)) {
        this.pendingBlanks.push(v);
        this.lazyOk = false;
        this.i++;
        continue;
      }
      const lm = matchListMarker(t);
      if (lm) {
        this.openListItem(v, lm);
        continue;
      }
      if (this.stack.length) {
        if (this.continueItem(v)) continue;
        this.stack.length = 0;
      }
      this.pendingBlanks = [];
      this.lazyOk = false;
      this.parseLeaf();
    }
    return this.roots;
  }

  private nextMasked(): string | undefined {
    return this.views[this.i + 1]?.masked;
  }

  private openListItem(v: LineView, lm: ListMarker): void {
    const stack = this.stack;
    while (stack.length && lm.indent.col <= stack[stack.length - 1]!.markerCol) stack.pop();
    const parent = stack[stack.length - 1];
    const node = newNode('listItem', v.line, this.depth + stack.length);
    node.markerCol = lm.indent.col;
    node.contentCol = lm.contentCol;
    node.own.push({ view: v, start: lm.contentStart, role: 'text' });
    (parent ? parent.children : this.roots).push(node);
    this.pendingBlanks = [];
    stack.push(node);
    this.i++;
    const fo = fenceOpen(v.masked.slice(lm.contentStart));
    if (fo) {
      node.own[0]!.role = 'fence';
      this.consumeItemFence(node, fo);
      this.lazyOk = false;
    } else this.lazyOk = true;
  }

  /** Attach a non-item line to an open list item (by indentation or lazily). */
  private continueItem(v: LineView): boolean {
    const t = v.masked;
    const ind = indentOf(t);
    const stack = this.stack;
    let k = stack.length - 1;
    // Marker columns grow along the stack: a line at or before the outermost marker matches
    // no open item (checked first so lazy lines under a deep outline cost O(1)).
    if (stack.length && ind.col <= stack[0]!.markerCol) k = -1;
    else while (k >= 0 && ind.col <= stack[k]!.markerCol) k--;
    if (k >= 0) {
      stack.length = k + 1;
      const item = stack[k]!;
      for (const b of this.pendingBlanks) item.own.push({ view: b, start: 0, role: 'blank' });
      this.pendingBlanks = [];
      const start = charsForColumns(t, item.contentCol);
      this.i++;
      const fo = fenceOpen(t);
      if (fo) {
        item.own.push({ view: v, start, role: 'fence' });
        this.consumeItemFence(item, fo);
        this.lazyOk = false;
        return true;
      }
      item.own.push({ view: v, start, role: 'text' });
      this.lazyOk = !HR_RE.test(t) && !HEADING_RE.test(t.slice(start));
      return true;
    }
    if (this.lazyOk && this.pendingBlanks.length === 0 && !interruptsParagraph(t, this.nextMasked())) {
      stack[stack.length - 1]!.own.push({ view: v, start: ind.chars, role: 'text' });
      this.i++;
      return true;
    }
    return false;
  }

  /** Lines of a fenced code block inside a list item (closed by the fence or by dedent). */
  private consumeItemFence(item: Node, fo: FenceOpen): void {
    const views = this.views;
    let blanks: LineView[] = [];
    const flush = (): void => {
      for (const b of blanks) item.own.push({ view: b, start: charsForColumns(b.masked, item.contentCol), role: 'code' });
      blanks = [];
    };
    while (this.i < views.length) {
      const v = views[this.i]!;
      const t = v.masked;
      if (isBlank(t)) {
        blanks.push(v);
        this.i++;
        continue;
      }
      if (isFenceClose(t, fo)) {
        flush();
        item.own.push({ view: v, start: charsForColumns(t, item.contentCol), role: 'fence' });
        this.i++;
        return;
      }
      if (indentOf(t).col <= item.markerCol) {
        this.pendingBlanks = blanks;
        return;
      }
      flush();
      item.own.push({ view: v, start: charsForColumns(t, item.contentCol), role: 'code' });
      this.i++;
    }
  }

  private push(node: Node): void {
    this.roots.push(node);
  }

  private parseLeaf(): void {
    const v = this.views[this.i]!;
    const t = v.masked;
    const fo = fenceOpen(t);
    if (fo) return this.parseFenced(fo);
    const hm = HEADING_RE.exec(t);
    if (hm) {
      const node = newNode('heading', v.line, this.depth);
      node.headingLevel = hm[1]!.length;
      node.own.push({ view: v, start: hm[0].length, role: 'text' });
      this.push(node);
      this.i++;
      return;
    }
    if (HR_RE.test(t)) {
      const node = newNode('hr', v.line, this.depth);
      node.own.push({ view: v, start: 0, role: 'meta' });
      this.push(node);
      this.i++;
      return;
    }
    if (QUOTE_RE.test(t)) return this.parseQuote();
    const ht = htmlStart(t, false);
    if (ht) return this.parseHtml(ht);
    if (isTableStart(t, this.nextMasked())) return this.parseTable();
    this.parseParagraph();
  }

  private parseFenced(fo: FenceOpen): void {
    const views = this.views;
    const v = views[this.i]!;
    const node = newNode('code', v.line, this.depth);
    const lang = fo.info.split(/[\s{}]+/).find((s) => s !== '');
    if (lang) node.lang = lang.toLowerCase();
    node.own.push({ view: v, start: 0, role: 'fence' });
    this.i++;
    let blanks: LineView[] = [];
    while (this.i < views.length) {
      const w = views[this.i]!;
      if (isFenceClose(w.masked, fo)) {
        for (const b of blanks) node.own.push({ view: b, start: 0, role: 'code' });
        node.own.push({ view: w, start: 0, role: 'fence' });
        this.i++;
        this.push(node);
        return;
      }
      if (isBlank(w.masked)) blanks.push(w);
      else {
        for (const b of blanks) node.own.push({ view: b, start: 0, role: 'code' });
        blanks = [];
        node.own.push({ view: w, start: 0, role: 'code' });
      }
      this.i++;
    }
    // Unclosed fence: runs to the end of the container (trailing blank lines excluded).
    this.push(node);
  }

  private parseQuote(): void {
    const views = this.views;
    const lines: { view: LineView; prefix: number }[] = [];
    let lastHadContent = false;
    while (this.i < views.length) {
      const w = views[this.i]!;
      const u = w.masked;
      const qm = QUOTE_RE.exec(u);
      if (qm) {
        lines.push({ view: w, prefix: qm[0].length });
        lastHadContent = !isBlank(u.slice(qm[0].length));
        this.i++;
        continue;
      }
      if (isBlank(u) || !lastHadContent || interruptsParagraph(u, this.nextMasked())) break;
      lines.push({ view: w, prefix: indentOf(u).chars }); // lazy continuation
      this.i++;
    }
    const first = lines[0]!;
    const cm = this.nest < MAX_NEST ? CALLOUT_RE.exec(first.view.masked.slice(first.prefix)) : null;
    if (cm) {
      const node = newNode('callout', first.view.line, this.depth);
      node.calloutType = cm[1]!.toLowerCase();
      if (cm[2]) node.calloutFold = cm[2];
      const titleStart = first.prefix + cm[0].length - cm[3]!.length;
      node.own.push({ view: first.view, start: titleStart, role: 'text' });
      node.endLine = lines[lines.length - 1]!.view.line;
      const sub = lines.slice(1).map((l) => subView(l.view, l.prefix));
      node.children = new ContainerParser(sub, this.depth + 1, this.nest + 1).run();
      this.push(node);
      return;
    }
    if (this.nest < MAX_NEST) {
      const sub = lines.map((l) => subView(l.view, l.prefix));
      const children = new ContainerParser(sub, this.depth + 1, this.nest + 1).run();
      if (children.some(isStructural)) {
        const node = newNode('quote', first.view.line, this.depth);
        node.container = true;
        node.endLine = lines[lines.length - 1]!.view.line;
        node.children = children;
        this.push(node);
        return;
      }
    }
    const node = newNode('quote', first.view.line, this.depth);
    for (const l of lines) node.own.push({ view: l.view, start: l.prefix, role: 'text' });
    this.push(node);
  }

  private parseHtml(type: number): void {
    const views = this.views;
    const v = views[this.i]!;
    const node = newNode('html', v.line, this.depth);
    const endMarker = type === 1 ? /<\/(?:script|pre|style|textarea)>/i : type === 2 ? /-->/ : null;
    let first = true;
    while (this.i < views.length) {
      const w = views[this.i]!;
      if (endMarker === null && isBlank(w.masked)) break;
      node.own.push({ view: w, start: 0, role: 'code' });
      this.i++;
      // For type 2 the `-->` must come after the opening `<!--`.
      const probe = first && type === 2 ? w.text.slice(w.text.indexOf('<!--') + 4) : w.text;
      first = false;
      if (endMarker && endMarker.test(probe)) break;
    }
    this.push(node);
  }

  private parseTable(): void {
    const views = this.views;
    const v = views[this.i]!;
    const node = newNode('table', v.line, this.depth);
    node.own.push({ view: v, start: 0, role: 'text' });
    node.own.push({ view: views[this.i + 1]!, start: 0, role: 'meta' });
    this.i += 2;
    while (this.i < views.length) {
      const w = views[this.i]!;
      if (isBlank(w.masked) || w.masked.indexOf('|') === -1) break;
      node.own.push({ view: w, start: 0, role: 'text' });
      this.i++;
    }
    this.push(node);
  }

  private parseParagraph(): void {
    const views = this.views;
    const v = views[this.i]!;
    // A standalone `^id` line right after a table / code block / quote names that block.
    const id = isStandaloneBlockId(v.masked);
    const prev = this.roots[this.roots.length - 1];
    if (id && prev && prev.trailingId === undefined && TRAILING_ID_KINDS.has(prev.kind)) {
      prev.trailingId = id;
      prev.trailingIdLine = v.line;
      if (prev.kind !== 'callout' && !prev.container) prev.own.push({ view: v, start: 0, role: 'meta' });
      this.i++;
      return;
    }
    const own: OwnLine[] = [{ view: v, start: indentOf(v.masked).chars, role: 'text' }];
    this.i++;
    let headingLevel = 0;
    while (this.i < views.length) {
      const w = views[this.i]!;
      const u = w.masked;
      if (isBlank(u)) break;
      const sm = SETEXT_RE.exec(u);
      if (sm) {
        headingLevel = sm[1]![0] === '=' ? 1 : 2;
        own.push({ view: w, start: 0, role: 'meta' });
        this.i++;
        break;
      }
      if (interruptsParagraph(u, this.nextMasked())) break;
      own.push({ view: w, start: indentOf(u).chars, role: 'text' });
      this.i++;
    }
    const node = newNode(headingLevel ? 'heading' : 'paragraph', v.line, this.depth);
    if (headingLevel) node.headingLevel = headingLevel;
    node.own = own;
    this.push(node);
  }
}

/** Blocks inside a quote that make it a container (tasks must be found, code must not be scanned). */
function isStructural(n: Node): boolean {
  return n.kind === 'listItem' || n.kind === 'code' || n.kind === 'html' || n.kind === 'callout' || n.container;
}

/** Blocks that take their id from a standalone `^id` line after them (Obsidian). */
const TRAILING_ID_KINDS: ReadonlySet<BlockKind> = new Set<BlockKind>(['code', 'table', 'quote', 'callout', 'html', 'hr']);

/** Compute `ownEndLine` / `endLine` for a forest (iterative post-order; deep outlines are safe). */
function finalize(roots: Node[]): void {
  const order: Node[] = [];
  const todo: Node[] = [...roots];
  while (todo.length) {
    const n = todo.pop()!;
    order.push(n);
    for (const c of n.children) todo.push(c);
  }
  for (let k = order.length - 1; k >= 0; k--) {
    const n = order[k]!;
    let own = n.line;
    for (const l of n.own) if (l.role !== 'blank' && l.view.line > own) own = l.view.line;
    if (n.kind === 'callout') own = n.line;
    n.ownEndLine = own;
    let end = Math.max(n.endLine, own, n.trailingIdLine ?? -1);
    for (const c of n.children) if (c.endLine > end) end = c.endLine;
    n.endLine = end;
  }
}

/** Visit nodes in document pre-order (iterative). */
export function preOrder(roots: readonly Node[], visit: (n: Node, parent: Node | null) => void): void {
  const todo: [Node, Node | null][] = [];
  for (let k = roots.length - 1; k >= 0; k--) todo.push([roots[k]!, null]);
  while (todo.length) {
    const [n, parent] = todo.pop()!;
    visit(n, parent);
    for (let k = n.children.length - 1; k >= 0; k--) todo.push([n.children[k]!, n]);
  }
}

/** Build line views for a document (masked text taken from the comment-masked copy). */
function buildViews(content: string, masked: string, lines: Lines): LineView[] {
  const views: LineView[] = [];
  for (let k = 0; k < lines.texts.length; k++) {
    const text = lines.texts[k]!;
    const offset = lines.starts[k]!;
    let m = masked === content ? text : masked.slice(offset, offset + text.length);
    if (k === 0 && m.charCodeAt(0) === 0xfeff) m = ' ' + m.slice(1);
    views.push({ text, masked: m, offset, line: k });
  }
  return views;
}

/** Parse the block structure of a whole document. */
export function parseStructure(content: string): Structure {
  const lines = splitLines(content);
  const comments = docCommentRanges(content, lines);
  const masked = maskRanges(content, comments);
  const views = buildViews(content, masked, lines);
  const roots: Node[] = [];
  const fm = findFrontmatter(content);
  let first = 0;
  if (fm) {
    const node = newNode('frontmatter', 0, 0);
    for (let k = 0; k <= fm.endLine && k < views.length; k++) node.own.push({ view: views[k]!, start: 0, role: 'code' });
    roots.push(node);
    first = fm.bodyStartLine;
  }
  // Logseq-style page properties: `key:: value` lines before any other block.
  let j = first;
  while (j < views.length && isBlank(views[j]!.masked)) j++;
  const propStart = j;
  while (j < views.length && PROPERTY_RE.test(views[j]!.masked)) j++;
  if (j > propStart) {
    const node = newNode('frontmatter', propStart, 0);
    for (let k = propStart; k < j; k++) node.own.push({ view: views[k]!, start: 0, role: 'text' });
    roots.push(node);
    first = j;
  }
  const body = new ContainerParser(first === 0 ? views : views.slice(first), 0, 0).run();
  for (const n of body) roots.push(n);
  finalize(roots);
  return { lines, masked, comments, views, frontmatter: fm, roots };
}

/** Innermost node whose own lines include `line` (or whose range does, as a fallback). */
export function nodeAtLine(roots: readonly Node[], line: number): Node | null {
  let best: Node | null = null;
  let level: readonly Node[] = roots;
  for (;;) {
    const hit = level.find((n) => n.line <= line && line <= n.endLine);
    if (!hit) return best;
    best = hit;
    if (hit.own.some((l) => l.view.line === line) && !hit.children.some((c) => c.line <= line && line <= c.endLine)) return hit;
    level = hit.children;
  }
}
