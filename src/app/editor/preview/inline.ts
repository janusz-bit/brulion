/**
 * Inline live preview (ViewPlugin): hides Markdown syntax, styles headings / quotes / callouts /
 * code, draws outline bullets with indentation guides and checkboxes, turns task tokens into
 * chips, links into links, refs and embeds into widgets.
 *
 * Only `view.visibleRanges` are decorated. Document, viewport, syntax-tree, fold, focus and
 * vault changes rebuild the visible ranges; a selection-only change rebuilds just the lines the
 * old and new selections touch (widened to their paragraph / code block), so moving the cursor
 * costs a few lines, not a screen. Expensive results (calc blocks, inline calculations) are
 * memoised per document version.
 */
import { foldable, foldedRanges, syntaxTree } from '@codemirror/language';
import type { EditorState, Range, Text } from '@codemirror/state';
import { Decoration, ViewPlugin, type DecorationSet, type EditorView, type ViewUpdate } from '@codemirror/view';
import type { SyntaxNode, SyntaxNodeRef } from '@lezer/common';
import { evaluateCalc, evaluateInline, pageScope, type CalcLineResult } from '@/core/calc';
import type { Translator } from '@/core/i18n';
import { pageTitle } from '@/core/paths';
import type { Locale, TaskStatus } from '@/core/types';
import { editorContext, type EditorContext } from '../context';
import { ancestorItems } from '../outliner';
import { zoomField } from '../zoom';
import { blockCandidates, calloutCollapsed, calloutToggles } from './blocks';
import { parseCalloutHead } from './callouts';
import { attachmentSpec, blockEmbedSpec, EmbedWidget, mediaKind, noteEmbedSpec, parseEmbedTarget, type EmbedSpec } from './embeds';
import { outlineLines, type OutlineLine } from './lists';
import { plainInline } from './render';
import { clipSpans, revealDelta, revealSelection, sameSelection, selectedLines, touches, type Span } from './reveal';
import { hasVaultChange, localeOf, nowOf, previewFocus, translatorOf } from './state';
import { chipFor, priorityOf, recurrencePrefix, tokenKind } from './tokens';
import {
  BlockRefWidget,
  BulletWidget,
  CalcHintWidget,
  CalcResultWidget,
  CalloutFoldWidget,
  CalloutHeadWidget,
  CheckboxWidget,
  ChipWidget,
  FenceLabelWidget,
  HeadingFoldWidget,
  HiddenWidget,
  HrWidget,
  InlineCalcWidget,
  LinkWidget,
  OrderedWidget,
  PriorityWidget,
  PropertyKeyWidget,
} from './widgets';

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------

/** Display text of a wiki link target without alias: page title (› heading / ^id). */
export function wikiDisplay(target: string): string {
  const hash = target.indexOf('#');
  const page = (hash >= 0 ? target.slice(0, hash) : target).trim();
  const frag = hash >= 0 ? target.slice(hash + 1).trim() : '';
  const title = page ? pageTitle(page) : '';
  if (!frag) return title || target;
  return title ? `${title} › ${frag}` : frag;
}

/** Metadata that ends a task's text (tokens, block id): the strike-through stops before it. */
const TASK_META_RE = /[ \t]*(?:(?:📅|📆|🗓|⏳|⌛|🛫|➕|✅|❌|🔁|⏰|🔺|⏫|🔼|🔽|⏬)|\^[A-Za-z0-9-]+[ \t]*$)/u;

/** End offset (within the line) of a task's text: before its first metadata token. */
export function strikeEnd(lineText: string, contentFrom: number): number {
  const m = TASK_META_RE.exec(lineText.slice(contentFrom));
  return m ? contentFrom + m.index : lineText.trimEnd().length;
}

const hidden = (raw: string) => Decoration.replace({ widget: new HiddenWidget(raw) });
const mark = (cls: string, attributes?: Record<string, string>) => Decoration.mark(attributes ? { class: cls, attributes } : { class: cls });
const line = (cls: string, attributes?: Record<string, string>) => Decoration.line(attributes ? { class: cls, attributes } : { class: cls });

const LANG_ICON: Record<string, 'calculator' | 'search'> = { calc: 'calculator', query: 'search' };

interface Memo {
  doc: Text;
  text: string | null;
  calc: Map<number, CalcLineResult[]>;
  inline: Map<number, { value?: string; error?: string }>;
}

function memoFor(prev: Memo | null, doc: Text): Memo {
  return prev && prev.doc === doc ? prev : { doc, text: null, calc: new Map(), inline: new Map() };
}

function docText(m: Memo): string {
  return (m.text ??= m.doc.toString());
}

/** Code-like block nodes: their lines are never outline items. */
const CODE_NODES = new Set(['FencedCode', 'CodeBlock', 'CommentBlock', 'Frontmatter', 'HTMLBlock']);

// ---------------------------------------------------------------------------------------------
// Builder
// ---------------------------------------------------------------------------------------------

class Builder {
  readonly out: Range<Decoration>[] = [];
  readonly doc: Text;
  readonly sel: readonly Span[];
  readonly active: Set<number>;
  readonly ctx: EditorContext | null;
  readonly t: Translator;
  readonly locale: Locale;
  readonly now: Date;
  readonly editable: boolean;
  /** Depth offset while zoomed (the zoom root renders at depth 0). */
  readonly depthShift: number;
  private readonly folded = new Set<number>();
  private outline = new Map<number, OutlineLine>();
  private from = 0;
  private to = 0;

  constructor(
    readonly state: EditorState,
    readonly memo: Memo,
  ) {
    this.doc = state.doc;
    this.sel = revealSelection(state);
    this.active = selectedLines(this.doc, this.sel);
    this.ctx = state.facet(editorContext);
    this.t = translatorOf(state);
    this.locale = localeOf(state);
    this.now = nowOf(state);
    this.editable = !state.readOnly;
    foldedRanges(state).between(0, this.doc.length, (from) => {
      this.folded.add(from);
    });
    const zoom = state.field(zoomField, false);
    this.depthShift = zoom ? ancestorItems(this.doc, this.doc.lineAt(zoom.from).number).length : 0;
  }

  /** Add a decoration if it starts inside the range being built (no duplicates across ranges). */
  private add(r: Range<Decoration>): void {
    if (r.from >= this.from && r.from <= this.to) this.out.push(r);
  }

  private hide(from: number, to: number): void {
    if (to > from) this.add(hidden(this.doc.sliceString(from, to)).range(from, to));
  }

  private touched(from: number, to: number): boolean {
    return touches(this.sel, from, to);
  }

  private lineActive(pos: number): boolean {
    return this.active.has(this.doc.lineAt(pos).number);
  }

  /** Decorate the line-aligned range `[from, to]`. */
  build(from: number, to: number): void {
    this.from = from;
    this.to = to;
    const doc = this.doc;
    const tree = syntaxTree(this.state);
    const firstLine = doc.lineAt(from).number;
    const lastLine = doc.lineAt(to).number;

    // Code-like lines (for the outline pass): scan from the outline root above.
    const codeSpans: Span[] = [];
    const scanFrom = doc.line(Math.max(1, firstLine - 400)).from;
    tree.iterate({
      from: scanFrom,
      to,
      enter: (n) => {
        if (CODE_NODES.has(n.name)) {
          codeSpans.push({ from: doc.lineAt(n.from).number, to: doc.lineAt(n.to).number });
          return false;
        }
        return n.name === 'Document' || n.name === 'BulletList' || n.name === 'OrderedList' || n.name === 'ListItem' || n.name === 'Blockquote';
      },
    });
    const isCode = (n: number) => codeSpans.some((s) => n >= s.from && n <= s.to);
    this.outline = outlineLines(doc, firstLine, lastLine, isCode);
    for (const info of this.outline.values()) this.outlineLine(info);

    tree.iterate({ from, to, enter: (n) => this.node(n) });
  }

  // ---------------------------------------------------------------------------------------------
  // Outline
  // ---------------------------------------------------------------------------------------------

  private taskStatusAt(lineNo: number): TaskStatus | null {
    const info = this.outline.get(lineNo);
    if (info?.kind === 'item') return info.task?.status ?? null;
    if (info?.kind === 'cont') {
      const owner = this.outline.get(info.owner);
      return owner?.kind === 'item' ? (owner.task?.status ?? null) : null;
    }
    return null;
  }

  private outlineLine(info: OutlineLine): void {
    const l = this.doc.line(info.line);
    const depth = Math.max(0, info.depth - this.depthShift);
    if (info.kind === 'cont') {
      const cls = `b-lp-li-cont${info.ownerContinues ? ' has-guide' : ''}`;
      this.add(line(cls, { style: `--lp-depth:${depth}` }).range(l.from));
      this.hide(l.from, l.from + info.wsLength);
      return;
    }
    const { item, task } = info;
    const folded = info.hasChildren && this.folded.has(l.to);
    const classes = ['b-lp-li'];
    if (task) classes.push('b-lp-li--task', `is-${task.status}`);
    if (item.number !== null) classes.push('b-lp-li--ordered');
    if (info.hasChildren && !folded) classes.push('has-children');
    this.add(line(classes.join(' '), { style: `--lp-depth:${depth}` }).range(l.from));
    this.hide(l.from, l.from + item.markerFrom);

    const markerFrom = l.from + item.markerFrom;
    const markerEnd = l.from + (task?.kind === 'checkbox' ? task.from : item.contentFrom);
    if (this.touched(markerFrom, l.from + item.markerTo)) {
      this.add(mark('b-lp-marker-raw').range(markerFrom, Math.max(markerEnd, l.from + item.markerTo)));
    } else {
      const raw = this.doc.sliceString(markerFrom, markerEnd);
      const foldLabel = this.t(folded ? 'preview.fold.unfold' : 'preview.fold.fold');
      const widget =
        item.number !== null
          ? new OrderedWidget(raw, item.marker, info.hasChildren, folded, foldLabel)
          : new BulletWidget(raw, depth, info.hasChildren, folded, { zoom: this.t('preview.bullet.zoom'), fold: foldLabel });
      if (markerEnd > markerFrom) this.add(Decoration.replace({ widget }).range(markerFrom, markerEnd));
    }

    if (task) {
      const tFrom = l.from + task.from;
      const tTo = l.from + task.to;
      if (this.touched(tFrom, tTo)) {
        this.add(mark('b-lp-task-raw').range(tFrom, tTo));
      } else {
        const keyword = task.kind === 'keyword' && task.marker !== 'TODO' && task.marker !== 'DONE' ? task.marker : '';
        const label = this.t('preview.task.label', { status: this.t(`preview.task.status.${task.status}`) });
        this.add(Decoration.replace({ widget: new CheckboxWidget(this.doc.sliceString(tFrom, l.from + task.end), task.status, keyword, label) }).range(tFrom, l.from + task.end));
      }
      if (task.status === 'done' || task.status === 'cancelled') {
        const end = l.from + strikeEnd(l.text, task.end);
        if (l.from + task.end < end) this.add(mark('b-lp-task-text').range(l.from + task.end, end));
      }
    }
  }

  // ---------------------------------------------------------------------------------------------
  // Syntax nodes
  // ---------------------------------------------------------------------------------------------

  private node(n: SyntaxNodeRef): boolean | void {
    const doc = this.doc;
    switch (n.name) {
      case 'FencedCode':
        this.fencedCode(n.node);
        return false;
      case 'CodeBlock':
        return false;
      case 'CommentBlock':
        this.lines(n.from, n.to, () => 'b-lp-comment-block');
        return false;
      case 'Frontmatter': {
        // Rendered by the block field unless revealed; then style the source.
        this.lines(n.from, n.to, (_l, i, last) => `b-lp-fm${i === 0 ? ' is-first' : ''}${last ? ' is-last' : ''}`);
        return false;
      }
      case 'Table': {
        const c = blockCandidates(this.state).find((b) => b.kind === 'table' && b.contentFrom === n.from);
        if (c && !this.touched(c.from, c.to)) return false;
        this.lines(n.from, n.to, () => 'b-lp-table-src');
        return false;
      }
      case 'ATXHeading1':
      case 'ATXHeading2':
      case 'ATXHeading3':
      case 'ATXHeading4':
      case 'ATXHeading5':
      case 'ATXHeading6':
        this.heading(n.node, Number(n.name.slice(-1)));
        return true;
      case 'SetextHeading1':
      case 'SetextHeading2': {
        const l = doc.lineAt(n.from);
        this.add(line(`b-lp-h b-lp-h${n.name.slice(-1)}`).range(l.from));
        const m = n.node.getChild('HeaderMark');
        if (m && !this.lineActive(m.from)) {
          const ml = doc.lineAt(m.from);
          this.add(line('b-lp-setext-mark').range(ml.from));
          this.hide(m.from, m.to);
        }
        return true;
      }
      case 'Blockquote':
        this.blockquote(n.node);
        return true;
      case 'QuoteMark':
        return false;
      case 'HorizontalRule': {
        const l = doc.lineAt(n.from);
        this.add(line('b-lp-hr-line').range(l.from));
        if (!this.lineActive(n.from)) this.add(Decoration.replace({ widget: new HrWidget(doc.sliceString(n.from, n.to)) }).range(n.from, n.to));
        return false;
      }
      case 'Emphasis':
      case 'StrongEmphasis':
      case 'Strikethrough':
      case 'Highlight':
        if (!this.touched(n.from, n.to)) {
          for (let c = n.node.firstChild; c; c = c.nextSibling) {
            if (c.name === 'EmphasisMark' || c.name === 'StrikethroughMark' || c.name === 'HighlightMark') this.hide(c.from, c.to);
          }
        }
        return true;
      case 'InlineCode':
        this.inlineCode(n.node);
        return false;
      case 'Link':
        this.link(n.node);
        return true;
      case 'Autolink': {
        const url = n.node.getChild('URL');
        if (!url) return false;
        const dest = doc.sliceString(url.from, url.to);
        if (!this.touched(n.from, n.to)) {
          this.hide(n.from, url.from);
          this.hide(url.to, n.to);
        }
        this.add(mark('b-lp-link b-lp-url', { 'data-lp': 'url', 'data-href': dest, title: dest }).range(url.from, url.to));
        return false;
      }
      case 'URL': {
        const parent = n.node.parent?.name;
        if (parent === 'Link' || parent === 'Image' || parent === 'Autolink') return false;
        const dest = doc.sliceString(n.from, n.to);
        const href = /^www\./i.test(dest) ? `https://${dest}` : dest;
        this.add(mark('b-lp-link b-lp-url', { 'data-lp': 'url', 'data-href': href, title: href }).range(n.from, n.to));
        return false;
      }
      case 'Image':
        this.image(n.node);
        return false;
      case 'WikiLink':
        this.wikiLink(n.node);
        return false;
      case 'Embed':
        this.embed(n.node);
        return false;
      case 'BlockRef':
        this.blockRef(n.node);
        return false;
      case 'BlockEmbed': {
        const idNode = n.node.getChild('BlockRefId');
        if (!idNode) return false;
        const id = doc.sliceString(idNode.from, idNode.to);
        this.embedWidget(n.from, n.to, blockEmbedSpec(this.ctx, id, this.embedLabels(`((${id}))`)));
        return false;
      }
      case 'Tag': {
        const name = n.node.getChild('TagName');
        const tag = name ? doc.sliceString(name.from, name.to) : '';
        const attrs: Record<string, string> = { title: `#${tag}` };
        if (!this.touched(n.from, n.to)) {
          attrs['data-lp'] = 'tag';
          attrs['data-tag'] = tag;
        }
        this.add(mark('b-lp-tag', attrs).range(n.from, n.to));
        return false;
      }
      case 'Comment':
        this.add(mark('b-lp-comment').range(n.from, n.to));
        if (!this.touched(n.from, n.to)) for (const m of n.node.getChildren('CommentMark')) this.hide(m.from, m.to);
        return false;
      case 'BlockId': {
        if (this.lineActive(n.from)) this.add(mark('b-lp-blockid').range(n.from, n.to));
        else {
          const before = n.from > 0 && /[ \t]/.test(doc.sliceString(n.from - 1, n.from)) ? n.from - 1 : n.from;
          this.hide(before, n.to);
        }
        return false;
      }
      case 'TaskToken':
        this.taskToken(n.node);
        return false;
      case 'PriorityToken': {
        if (this.touched(n.from, n.to)) return false;
        const p = priorityOf(doc.sliceString(n.from, n.to));
        if (p) this.add(Decoration.replace({ widget: new PriorityWidget(doc.sliceString(n.from, n.to), p, this.t(`preview.priority.${p}`)) }).range(n.from, n.to));
        return false;
      }
      case 'PropertyLine':
        this.property(n.node);
        return true;
      default:
        return true;
    }
  }

  /** Line decorations for every line of `[from, to]`. */
  private lines(from: number, to: number, cls: (lineNo: number, index: number, last: boolean) => string | null): void {
    const doc = this.doc;
    const a = doc.lineAt(from).number;
    const b = doc.lineAt(Math.min(to, doc.length)).number;
    for (let n = Math.max(a, doc.lineAt(this.from).number); n <= b && n <= doc.lineAt(this.to).number; n++) {
      const c = cls(n, n - a, n === b);
      if (c) this.add(line(c).range(doc.line(n).from));
    }
  }

  private heading(node: SyntaxNode, level: number): void {
    const doc = this.doc;
    const l = doc.lineAt(node.from);
    const n = l.number;
    const folded = this.folded.has(l.to);
    this.add(line(`b-lp-h b-lp-h${level}${folded ? ' is-folded' : ''}`).range(l.from));
    if (!this.outline.has(n) && (folded || foldable(this.state, l.from, l.to))) {
      this.add(Decoration.widget({ widget: new HeadingFoldWidget(folded, this.t(folded ? 'preview.fold.unfold' : 'preview.fold.fold')), side: -1 }).range(l.from));
    }
    if (this.lineActive(node.from)) return;
    for (let c = node.firstChild; c; c = c.nextSibling) {
      if (c.name !== 'HeaderMark') continue;
      if (c.from === node.from || doc.sliceString(node.from, c.from).trim() === '') {
        // Opening marks and the space after them.
        let end = c.to;
        while (end < l.to && /[ \t]/.test(doc.sliceString(end, end + 1))) end++;
        this.hide(c.from, end);
      } else {
        // Closing sequence and the blanks before it.
        let start = c.from;
        while (start > l.from && /[ \t]/.test(doc.sliceString(start - 1, start))) start--;
        this.hide(start, c.to);
      }
    }
  }

  private blockquote(node: SyntaxNode): void {
    const doc = this.doc;
    const first = doc.lineAt(node.from);
    const head = parseCalloutHead(first.text);
    const lastNo = doc.lineAt(node.to).number;
    let collapsed = false;
    if (head?.fold) collapsed = calloutCollapsed({ from: first.from, fold: head.fold }, calloutToggles(this.state)) && !touches(this.sel, first.to + 1, node.to);
    const startNo = Math.max(first.number, doc.lineAt(this.from).number);
    const endNo = Math.min(lastNo, doc.lineAt(this.to).number);
    for (let n = startNo; n <= endNo; n++) {
      const l = doc.line(n);
      const isFirst = n === first.number;
      const isLast = n === lastNo || (isFirst && collapsed);
      const base = head ? `b-lp-callout b-lp-callout--${head.type}` : 'b-lp-quote';
      this.add(line(`${base}${isFirst ? ' is-first' : ''}${isLast ? ' is-last' : ''}${isFirst && head ? ' is-head' : ''}`).range(l.from));
      const gt = l.text.indexOf('>');
      if (gt < 0 || l.text.slice(0, gt).trim() !== '') continue;
      const active = this.active.has(n);
      if (isFirst && head) {
        if (!active) {
          const raw = doc.sliceString(l.from + head.quoteFrom, l.from + head.titleFrom);
          const foldLabel = this.t(collapsed ? 'preview.callout.expand' : 'preview.callout.collapse');
          const defaultTitle = head.title ? '' : this.t(`preview.callout.${head.type}`);
          this.add(Decoration.replace({ widget: new CalloutHeadWidget(raw, head.type, defaultTitle, !!head.fold && !head.title, collapsed, foldLabel) }).range(l.from + head.quoteFrom, l.from + head.titleFrom));
          if (head.title) {
            this.add(mark('b-lp-callout-title').range(l.from + head.titleFrom, l.to));
            if (head.fold) this.add(Decoration.widget({ widget: new CalloutFoldWidget(collapsed, foldLabel), side: 1 }).range(l.to));
          }
        } else if (head.title) this.add(mark('b-lp-callout-title').range(l.from + head.titleFrom, l.to));
        continue;
      }
      if (active) continue;
      const m = /^(?:>[ \t]?)+/.exec(l.text.slice(gt));
      if (m) this.hide(l.from + gt, l.from + gt + m[0].length);
    }
  }

  private fencedCode(node: SyntaxNode): void {
    const doc = this.doc;
    const info = node.getChild('CodeInfo');
    const lang = info ? doc.sliceString(info.from, info.to).trim().toLowerCase() : '';
    const revealed = this.touched(node.from, node.to);
    if (lang === 'query' && !revealed) {
      const c = blockCandidates(this.state).find((b) => b.kind === 'query' && b.contentFrom === node.from);
      if (c) return;
    }
    const openLine = doc.lineAt(node.from);
    const endLine = doc.lineAt(node.to);
    const marks = node.getChildren('CodeMark');
    const closing = marks.length > 1 ? marks[marks.length - 1]! : null;
    const closed = !!closing && doc.lineAt(closing.from).number === endLine.number && endLine.number > openLine.number;
    const calc = lang === 'calc';
    this.lines(node.from, node.to, (n, i, last) => {
      const cls = [`b-lp-code`];
      if (calc) cls.push('b-lp-code--calc');
      if (i === 0) cls.push('is-first');
      if (last) cls.push('is-last');
      if (!revealed && i === 0) cls.push('is-fence');
      if (!revealed && last && closed && n !== openLine.number) cls.push('is-fence-end');
      return cls.join(' ');
    });
    if (!revealed) {
      this.add(Decoration.replace({ widget: new FenceLabelWidget(doc.sliceString(node.from, openLine.to), lang, LANG_ICON[lang] ?? null) }).range(node.from, openLine.to));
      if (closed && closing) this.hide(closing.from, endLine.to);
    }
    if (calc) this.calcResults(node, openLine.number, closed ? endLine.number - 1 : endLine.number);
  }

  private calcResults(node: SyntaxNode, openNo: number, lastNo: number): void {
    const doc = this.doc;
    if (lastNo <= openNo) return;
    let results = this.memo.calc.get(node.from);
    if (!results) {
      const lines: string[] = [];
      for (let n = openNo + 1; n <= lastNo; n++) lines.push(doc.line(n).text);
      try {
        const scope = pageScope(docText(this.memo), openNo - 1, this.locale);
        results = evaluateCalc(lines.join('\n'), { scope, locale: this.locale }).lines;
      } catch {
        results = [];
      }
      this.memo.calc.set(node.from, results);
    }
    for (const r of results) {
      if (r.value === undefined && r.error === undefined) continue;
      const l = doc.line(openNo + 1 + r.line);
      this.add(line('b-lp-calc-line').range(l.from));
      this.add(Decoration.widget({ widget: new CalcResultWidget(r.value ?? null, r.error ?? null, !!r.assigned), side: 1 }).range(l.to));
    }
  }

  private inlineCode(node: SyntaxNode): void {
    const doc = this.doc;
    const marks = node.getChildren('CodeMark');
    const open = marks[0];
    const close = marks.length > 1 ? marks[marks.length - 1]! : null;
    if (!open || !close) return;
    const inner = doc.sliceString(open.to, close.from);
    const touched = this.touched(node.from, node.to);
    if (/^\s*=/.test(inner)) {
      let res = this.memo.inline.get(node.from);
      if (!res) {
        try {
          const scope = pageScope(docText(this.memo), doc.lineAt(node.from).number - 1, this.locale);
          res = evaluateInline(inner, scope, this.locale);
        } catch (err) {
          res = { error: String((err as Error)?.message ?? err) };
        }
        this.memo.inline.set(node.from, res);
      }
      if (!touched) {
        this.add(Decoration.replace({ widget: new InlineCalcWidget(doc.sliceString(node.from, node.to), res.value ?? null, res.error ?? null) }).range(node.from, node.to));
      } else {
        this.add(mark('b-lp-code').range(node.from, node.to));
        if (res.value !== undefined || res.error !== undefined) this.add(Decoration.widget({ widget: new CalcHintWidget(res.value ?? null, res.error ?? null), side: 1 }).range(node.to));
      }
      return;
    }
    this.add(mark('b-lp-code').range(node.from, node.to));
    if (!touched) {
      this.hide(open.from, open.to);
      this.hide(close.from, close.to);
    }
  }

  private link(node: SyntaxNode): void {
    const doc = this.doc;
    const marks = node.getChildren('LinkMark');
    const url = node.getChild('URL');
    if (marks.length < 2) return;
    const textFrom = marks[0]!.to;
    const textTo = marks[1]!.from;
    if (!url) return;
    const dest = doc.sliceString(url.from, url.to).replace(/^<|>$/g, '');
    const external = /^[a-z][a-z0-9+.-]*:/i.test(dest);
    const attrs: Record<string, string> = { title: dest };
    if (!this.touched(node.from, node.to)) {
      this.hide(node.from, textFrom);
      this.hide(textTo, node.to);
      if (external) {
        attrs['data-lp'] = 'url';
        attrs['data-href'] = dest;
      } else {
        let target = dest;
        try {
          target = decodeURI(dest);
        } catch {
          // keep
        }
        attrs['data-lp'] = 'link';
        attrs['data-target'] = target.replace(/\.md(?=$|#)/i, '');
      }
    }
    if (textTo > textFrom) this.add(mark(`b-lp-link${external ? ' b-lp-url' : ''}`, attrs).range(textFrom, textTo));
  }

  private missing(page: string): boolean {
    if (!this.ctx || !page) return false;
    try {
      return this.ctx.workspace.index.resolveLink(page, this.ctx.path) === null;
    } catch {
      return false;
    }
  }

  private wikiLink(node: SyntaxNode): void {
    const doc = this.doc;
    const targetNode = node.getChild('WikiTarget');
    const aliasNode = node.getChild('WikiAlias');
    const target = targetNode ? doc.sliceString(targetNode.from, targetNode.to).trim() : '';
    const page = target.split('#')[0]!.trim();
    const missing = this.missing(page);
    if (this.touched(node.from, node.to)) {
      if (missing) this.add(mark('b-lp-link-src is-missing').range(node.from, node.to));
      return;
    }
    const cls = `b-lp-link b-lp-wikilink${missing ? ' is-missing' : ''}`;
    const title = missing ? this.t('preview.link.missing', { target: page }) : target || pageTitle(this.ctx?.path ?? '');
    const attrs = { 'data-lp': 'link', 'data-target': target, title };
    if (aliasNode && aliasNode.to > aliasNode.from) {
      this.hide(node.from, aliasNode.from);
      this.hide(aliasNode.to, node.to);
      this.add(mark(cls, attrs).range(aliasNode.from, aliasNode.to));
      return;
    }
    const display = wikiDisplay(target);
    if (targetNode && display === doc.sliceString(targetNode.from, targetNode.to)) {
      this.hide(node.from, targetNode.from);
      this.hide(targetNode.to, node.to);
      this.add(mark(cls, attrs).range(targetNode.from, targetNode.to));
      return;
    }
    this.add(Decoration.replace({ widget: new LinkWidget(doc.sliceString(node.from, node.to), display, target, missing, title) }).range(node.from, node.to));
  }

  private embedLabels(target: string): { missing: string; open: string } {
    return { missing: this.t('preview.embed.missing', { target }), open: this.t('preview.embed.open') };
  }

  /** Replace an embed with its widget, or show the widget after the revealed source. */
  private embedWidget(from: number, to: number, spec: EmbedSpec): void {
    const raw = this.doc.sliceString(from, to);
    if (this.touched(from, to)) this.add(Decoration.widget({ widget: new EmbedWidget('', spec), side: 1 }).range(to));
    else this.add(Decoration.replace({ widget: new EmbedWidget(raw, spec) }).range(from, to));
  }

  private embed(node: SyntaxNode): void {
    const doc = this.doc;
    const inner = doc.sliceString(node.from + 3, node.to - 2);
    const target = parseEmbedTarget(inner);
    const kind = mediaKind(target.page);
    const spec =
      kind === 'note'
        ? noteEmbedSpec(this.ctx, target, this.embedLabels(target.page || inner))
        : attachmentSpec(this.ctx, target.page, target.alias, this.embedLabels(target.page));
    this.embedWidget(node.from, node.to, spec);
  }

  private image(node: SyntaxNode): void {
    const doc = this.doc;
    const marks = node.getChildren('LinkMark');
    const url = node.getChild('URL');
    if (!url || marks.length < 2) return;
    const alt = doc.sliceString(marks[0]!.to, marks[1]!.from);
    const src = doc.sliceString(url.from, url.to).replace(/^<|>$/g, '');
    this.embedWidget(node.from, node.to, attachmentSpec(this.ctx, src, alt || undefined, this.embedLabels(src)));
  }

  private blockRef(node: SyntaxNode): void {
    const doc = this.doc;
    const idNode = node.getChild('BlockRefId');
    if (!idNode || this.touched(node.from, node.to)) return;
    const id = doc.sliceString(idNode.from, idNode.to);
    const ref = this.ctx?.workspace.index.blockById(id) ?? null;
    const text = ref ? plainInline(ref.block.text) || pageTitle(ref.path) : null;
    const title = ref ? `${pageTitle(ref.path)} › ${text}` : this.t('preview.blockref.missing', { id });
    this.add(Decoration.replace({ widget: new BlockRefWidget(doc.sliceString(node.from, node.to), id, text, title) }).range(node.from, node.to));
  }

  private taskToken(node: SyntaxNode): void {
    const doc = this.doc;
    if (this.touched(node.from, node.to)) return;
    const markNode = node.getChild('TaskTokenMark');
    const valueNode = node.getChild('TaskTokenValue');
    if (!markNode) return;
    const kind = tokenKind(doc.sliceString(markNode.from, markNode.to));
    if (!kind) return;
    let value = valueNode ? doc.sliceString(valueNode.from, valueNode.to) : '';
    let to = node.to;
    if (kind === 'recurrence' && valueNode) {
      // Only the part that is a rule becomes the chip; trailing text stays text.
      const len = recurrencePrefix(value);
      if (len !== null) {
        value = value.slice(0, len);
        to = valueNode.from + len;
      }
    }
    const lineNo = doc.lineAt(node.from).number;
    const chip = chipFor(kind, value, { now: this.now, locale: this.locale, t: this.t, taskStatus: this.taskStatusAt(lineNo) });
    this.add(Decoration.replace({ widget: new ChipWidget(doc.sliceString(node.from, to), chip) }).range(node.from, to));
  }

  private property(node: SyntaxNode): void {
    const doc = this.doc;
    const key = node.getChild('PropertyKey');
    const pm = node.getChild('PropertyMark');
    const value = node.getChild('PropertyValue');
    if (!key || !pm) return;
    const l = doc.lineAt(node.from);
    this.add(line('b-lp-prop-line').range(l.from));
    if (!this.lineActive(node.from)) {
      const end = value ? value.from : pm.to;
      this.add(Decoration.replace({ widget: new PropertyKeyWidget(doc.sliceString(key.from, end), doc.sliceString(key.from, key.to)) }).range(key.from, end));
    } else {
      this.add(mark('b-lp-propkey-src').range(key.from, pm.to));
    }
    if (value) this.add(mark('b-lp-propval').range(value.from, value.to));
  }
}

// ---------------------------------------------------------------------------------------------
// Plugin
// ---------------------------------------------------------------------------------------------

/** Nodes whose reveal state depends on the whole node (rebuilt together). */
const EXPAND_NODES = new Set(['FencedCode', 'Frontmatter', 'Table', 'Paragraph', 'CommentBlock', 'Blockquote']);

function expandAt(state: EditorState, pos: number): Span | null {
  let best: Span | null = null;
  for (const side of [-1, 1] as const) {
    for (let n: SyntaxNode | null = syntaxTree(state).resolveInner(pos, side); n; n = n.parent) {
      if (EXPAND_NODES.has(n.name)) {
        if (!best) best = { from: n.from, to: n.to };
        else best = { from: Math.min(best.from, n.from), to: Math.max(best.to, n.to) };
        break;
      }
    }
  }
  return best;
}

/** Decorations for `ranges` (line-aligned) of a state. */
export function buildDecorations(state: EditorState, ranges: readonly Span[], memo: Memo | null = null): { decorations: Range<Decoration>[]; memo: Memo } {
  const m = memoFor(memo, state.doc);
  const b = new Builder(state, m);
  for (const r of ranges) {
    const from = state.doc.lineAt(r.from).from;
    const to = state.doc.lineAt(r.to).to;
    b.build(from, to);
  }
  return { decorations: b.out, memo: m };
}

export const inlinePreview = ViewPlugin.fromClass(
  class {
    decorations: DecorationSet;
    private memo: Memo | null = null;

    constructor(view: EditorView) {
      this.decorations = this.full(view);
    }

    private full(view: EditorView): DecorationSet {
      const { decorations, memo } = buildDecorations(view.state, view.visibleRanges, this.memo);
      this.memo = memo;
      return Decoration.set(decorations, true);
    }

    update(u: ViewUpdate): void {
      const s0 = u.startState;
      const s1 = u.state;
      const fullRebuild =
        u.docChanged ||
        u.viewportChanged ||
        hasVaultChange(u) ||
        syntaxTree(s0) !== syntaxTree(s1) ||
        foldedRanges(s0) !== foldedRanges(s1) ||
        s0.field(previewFocus, false) !== s1.field(previewFocus, false) ||
        s0.facet(editorContext) !== s1.facet(editorContext) ||
        s0.readOnly !== s1.readOnly ||
        s0.field(zoomField, false) !== s1.field(zoomField, false) ||
        calloutToggles(s0) !== calloutToggles(s1);
      if (fullRebuild) {
        this.decorations = this.full(u.view);
        return;
      }
      if (!u.selectionSet) return;
      const before = revealSelection(s0);
      const after = revealSelection(s1);
      if (sameSelection(before, after)) return;
      const delta = clipSpans(
        revealDelta(s1.doc, before, after, (p) => expandAt(s1, p)),
        u.view.visibleRanges,
      );
      if (!delta.length) return;
      const lineSpans = delta.map((d) => ({ from: s1.doc.lineAt(d.from).from, to: s1.doc.lineAt(d.to).to }));
      const { decorations, memo } = buildDecorations(s1, lineSpans, this.memo);
      this.memo = memo;
      let set = this.decorations;
      for (const d of lineSpans) set = set.update({ filter: (from) => from < d.from || from > d.to, filterFrom: d.from, filterTo: d.to });
      this.decorations = set.update({ add: decorations, sort: true });
    }
  },
  { decorations: (v) => v.decorations },
);
