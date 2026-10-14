/**
 * Block-level live preview: widgets that replace whole lines (tables, frontmatter, query
 * results) and collapsed callout bodies. CodeMirror only accepts such decorations from state
 * (not from view plugins), so they live in a StateField. Candidates (where tables etc. are) are
 * re-scanned when the document or its syntax tree changes; the decorations are rebuilt only
 * when what is revealed changes. Widgets compute their content lazily in `toDOM`, which
 * CodeMirror calls for visible widgets only.
 */
import { syntaxTree } from '@codemirror/language';
import { StateEffect, StateField, type EditorState, type Extension, type Range, type Transaction } from '@codemirror/state';
import { Decoration, EditorView, type DecorationSet } from '@codemirror/view';
import { parseYaml } from '@/core/markdown';
import { editorContext } from '../context';
import { parseCalloutHead } from './callouts';
import { h, icon } from './dom';
import { QueryWidget, querySource } from './query';
import { revealSelection, touches, type Span } from './reveal';
import { localeOf, previewFocus, translatorOf, vaultVersion } from './state';
import { TableWidget } from './tables';
import { PreviewWidget } from './widgets';

export interface BlockCandidate {
  kind: 'table' | 'frontmatter' | 'query' | 'callout';
  /** Whole lines covered (line start … line end). */
  from: number;
  to: number;
  /** Tables: start of the header row text (after indentation). */
  contentFrom: number;
  /** Callouts: '+' / '-' / '' and the end of the header line. */
  fold?: '' | '+' | '-';
  headTo?: number;
}

/** Toggle a foldable callout (by its header line start). */
export const toggleCallout = StateEffect.define<number>({ map: (v, m) => m.mapPos(v, -1) });
/** Show / hide the frontmatter's property list. */
export const setFrontmatterOpen = StateEffect.define<boolean>();

interface BlockState {
  candidates: BlockCandidate[];
  /** Callout header positions whose fold state was toggled by the user. */
  toggled: readonly number[];
  frontmatterOpen: boolean;
  /** What the decorations were built for (reveal state etc.). */
  key: string;
  decorations: DecorationSet;
}

/** Scan the syntax tree for block candidates (descends into lists only). */
export function scanBlocks(state: EditorState): BlockCandidate[] {
  const doc = state.doc;
  const out: BlockCandidate[] = [];
  const wholeLines = (from: number, to: number): Span | null => {
    const first = doc.lineAt(from);
    if (doc.sliceString(first.from, from).trim() !== '') return null;
    return { from: first.from, to: doc.lineAt(Math.min(to, doc.length)).to };
  };
  syntaxTree(state).iterate({
    enter(node) {
      switch (node.name) {
        case 'Document':
        case 'BulletList':
        case 'OrderedList':
        case 'ListItem':
          return true;
        case 'Frontmatter':
          out.push({ kind: 'frontmatter', from: 0, to: doc.lineAt(node.to).to, contentFrom: node.from });
          return false;
        case 'Table': {
          const r = wholeLines(node.from, node.to);
          if (r) out.push({ kind: 'table', ...r, contentFrom: node.from });
          return false;
        }
        case 'FencedCode': {
          const info = node.node.getChild('CodeInfo');
          const lang = info ? doc.sliceString(info.from, info.to).trim().toLowerCase() : '';
          if (lang === 'query') {
            const r = wholeLines(node.from, node.to);
            if (r) out.push({ kind: 'query', ...r, contentFrom: node.from });
          }
          return false;
        }
        case 'Blockquote': {
          const head = doc.lineAt(node.from);
          const c = parseCalloutHead(head.text);
          if (c && c.fold) {
            out.push({ kind: 'callout', from: head.from, to: doc.lineAt(node.to).to, contentFrom: head.from, fold: c.fold, headTo: head.to });
          }
          return false;
        }
        default:
          return false;
      }
    },
  });
  return out;
}

/** Whether a foldable callout is shown collapsed. */
export function calloutCollapsed(c: Pick<BlockCandidate, 'from' | 'fold'>, toggled: readonly number[]): boolean {
  const byDefault = c.fold === '-';
  return toggled.includes(c.from) ? !byDefault : byDefault;
}

// ---------------------------------------------------------------------------------------------
// Frontmatter widget
// ---------------------------------------------------------------------------------------------

function formatValue(v: unknown): string[] {
  if (Array.isArray(v)) return v.map((x) => String(x));
  if (v === null || v === undefined) return [];
  if (typeof v === 'object') return [JSON.stringify(v)];
  return [String(v)];
}

export class FrontmatterWidget extends PreviewWidget {
  constructor(
    readonly yaml: string,
    readonly open: boolean,
    readonly labels: { title: string; edit: string; toggle: string; empty: string },
  ) {
    super();
  }

  override eq(o: FrontmatterWidget): boolean {
    return o.yaml === this.yaml && o.open === this.open && o.labels.title === this.labels.title;
  }

  override get estimatedHeight(): number {
    return this.open ? 40 + this.yaml.split('\n').length * 30 : 40;
  }

  toDOM(): HTMLElement {
    let data: Record<string, unknown> = {};
    try {
      data = parseYaml(this.yaml);
    } catch {
      data = {};
    }
    const keys = Object.keys(data);
    const el = h('div', { class: `b-lp-props${this.open ? ' is-open' : ''}`, 'data-lp-root': '', 'data-lp': 'frontmatter' });
    const head = h(
      'div',
      { class: 'b-lp-props__head', 'data-lp': 'fm-toggle', role: 'button', 'aria-expanded': this.open ? 'true' : 'false', title: this.labels.toggle },
      icon(this.open ? 'chevronDown' : 'chevronRight', 'b-lp-icon b-lp-props__chevron'),
      h('span', { class: 'b-lp-props__title' }, this.labels.title),
    );
    if (!this.open) head.appendChild(h('span', { class: 'b-lp-props__summary' }, keys.length ? keys.join(' · ') : this.labels.empty));
    head.appendChild(h('span', { class: 'b-lp-props__edit', 'data-lp': 'fm-edit', title: this.labels.edit, 'aria-label': this.labels.edit }, icon('pencil', 'b-lp-icon')));
    el.appendChild(head);
    if (this.open) {
      const list = h('dl', { class: 'b-lp-props__list' });
      for (const k of keys) {
        const values = formatValue(data[k]);
        const dd = h('dd', { 'data-lp': 'fm-edit' });
        if (Array.isArray(data[k])) for (const v of values) dd.appendChild(h('span', { class: 'b-lp-props__chip' }, v));
        else dd.textContent = values.join(', ');
        list.append(h('dt', { 'data-lp': 'fm-edit' }, k), dd);
      }
      if (!keys.length) list.appendChild(h('dd', { class: 'b-lp-props__none' }, this.labels.empty));
      el.appendChild(list);
    }
    return el;
  }
}

// ---------------------------------------------------------------------------------------------
// The field
// ---------------------------------------------------------------------------------------------

function treeChanged(tr: Transaction): boolean {
  return syntaxTree(tr.startState) !== syntaxTree(tr.state);
}

function build(state: EditorState, candidates: BlockCandidate[], toggled: readonly number[], frontmatterOpen: boolean): { key: string; decorations: DecorationSet } {
  const sel = revealSelection(state);
  const doc = state.doc;
  const ctx = state.facet(editorContext);
  const t = translatorOf(state);
  const locale = localeOf(state);
  const version = state.field(vaultVersion, false) ?? 0;
  const editable = !state.readOnly;
  const keyParts: string[] = [locale, String(editable), String(frontmatterOpen), ctx ? 'c' : '-'];
  const ranges: Range<Decoration>[] = [];
  for (const c of candidates) {
    const revealed = touches(sel, c.from, c.to);
    switch (c.kind) {
      case 'frontmatter': {
        keyParts.push(`f${revealed ? 1 : 0}`);
        if (revealed) break;
        const lines = doc.sliceString(c.from, c.to).split('\n');
        const yaml = lines.slice(1, /^(---|\.\.\.)\s*$/.test(lines[lines.length - 1] ?? '') && lines.length > 1 ? -1 : undefined).join('\n');
        const widget = new FrontmatterWidget(yaml, frontmatterOpen, {
          title: t('preview.frontmatter.title'),
          edit: t('preview.frontmatter.edit'),
          toggle: t('preview.frontmatter.toggle'),
          empty: t('preview.frontmatter.empty'),
        });
        ranges.push(Decoration.replace({ widget, block: true }).range(c.from, c.to));
        break;
      }
      case 'table': {
        keyParts.push(`t${revealed ? 1 : 0}`);
        if (revealed) break;
        const widget = new TableWidget(doc.sliceString(c.contentFrom, c.to), doc.lineAt(c.from).number - 1, locale, {
          addRow: t('preview.table.addRow'),
          addColumn: t('preview.table.addColumn'),
          align: t('preview.table.align'),
          formula: t('preview.table.formula'),
          table: t('preview.table.label'),
        }, editable);
        ranges.push(Decoration.replace({ widget, block: true }).range(c.from, c.to));
        break;
      }
      case 'query': {
        keyParts.push(`q${revealed ? 1 : 0}`);
        if (revealed) break;
        const widget = new QueryWidget(querySource(doc.sliceString(c.from, c.to)), version, {
          title: t('preview.query.title'),
          unavailable: t('preview.query.unavailable'),
          empty: t('preview.query.empty'),
          edit: t('preview.query.edit'),
          error: t('preview.query.error'),
          count: (n) => t('preview.query.count', { count: n }),
          toggle: t('preview.task.toggle'),
          open: t('preview.query.open'),
        }, editable);
        ranges.push(Decoration.replace({ widget, block: true }).range(c.from, c.to));
        break;
      }
      case 'callout': {
        const collapsed = calloutCollapsed(c, toggled) && !touches(sel, (c.headTo ?? c.from) + 1, c.to);
        keyParts.push(`c${collapsed ? 1 : 0}`);
        if (collapsed && c.headTo !== undefined && c.headTo < c.to) ranges.push(Decoration.replace({}).range(c.headTo, c.to));
        break;
      }
    }
  }
  if (candidates.some((c) => c.kind === 'query')) keyParts.push(`v${version}`);
  return { key: keyParts.join('|'), decorations: Decoration.set(ranges, true) };
}

export const blockPreview = StateField.define<BlockState>({
  create(state) {
    const candidates = scanBlocks(state);
    return { candidates, toggled: [], frontmatterOpen: false, ...build(state, candidates, [], false) };
  },
  update(value, tr) {
    let { candidates, toggled, frontmatterOpen } = value;
    let effectsChanged = false;
    if (tr.docChanged && toggled.length) toggled = toggled.map((p) => tr.changes.mapPos(p, -1));
    for (const e of tr.effects) {
      if (e.is(toggleCallout)) {
        const p = tr.docChanged ? tr.changes.mapPos(e.value, -1) : e.value;
        toggled = toggled.includes(p) ? toggled.filter((x) => x !== p) : [...toggled, p];
        effectsChanged = true;
      } else if (e.is(setFrontmatterOpen)) {
        frontmatterOpen = e.value;
        effectsChanged = true;
      }
    }
    const rescan = tr.docChanged || treeChanged(tr);
    if (rescan) candidates = scanBlocks(tr.state);
    if (!rescan && !effectsChanged && !tr.selection && tr.startState.field(previewFocus, false) === tr.state.field(previewFocus, false) && !tr.reconfigured) {
      const v0 = tr.startState.field(vaultVersion, false);
      const v1 = tr.state.field(vaultVersion, false);
      if (v0 === v1 && tr.startState.readOnly === tr.state.readOnly) return value;
    }
    const next = build(tr.state, candidates, toggled, frontmatterOpen);
    if (!rescan && next.key === value.key) return { ...value, toggled, frontmatterOpen };
    return { candidates, toggled, frontmatterOpen, ...next };
  },
  provide: (f) => EditorView.decorations.from(f, (v) => v.decorations),
});

/** Candidates of the current state (for the inline plugin: what blocks render as widgets). */
export function blockCandidates(state: EditorState): readonly BlockCandidate[] {
  return state.field(blockPreview, false)?.candidates ?? [];
}

export function calloutToggles(state: EditorState): readonly number[] {
  return state.field(blockPreview, false)?.toggled ?? [];
}

export function blockExtension(): Extension {
  return blockPreview;
}
