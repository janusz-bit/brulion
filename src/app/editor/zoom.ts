/**
 * Outliner zoom: show only one list item (or heading section) with its children. The zoom range
 * lives in a StateField (mapped through edits); everything outside is hidden with block-replace
 * decorations, edits outside are dropped by a change filter and the selection is clamped into
 * the range. External/merge transactions pass `filter: false` and are never blocked; undo/redo
 * that reach outside the range zoom out.
 */
import {
  EditorSelection,
  EditorState,
  StateEffect,
  StateField,
  Transaction,
  type Extension,
  type StateCommand,
  type Text,
} from '@codemirror/state';
import { Decoration, EditorView, WidgetType, type DecorationSet } from '@codemirror/view';
import { itemAt, itemEndLine, outlineFoldRange, parentItem, parseListLine } from './outliner';

/** Zoomed range: from the start of the item's first line to the end of its last line. */
export interface ZoomRange {
  from: number;
  to: number;
}

export const setZoom = StateEffect.define<ZoomRange | null>({
  map: (value, mapping) => (value ? { from: mapping.mapPos(value.from, -1), to: mapping.mapPos(value.to, 1) } : null),
});

function clampRange(doc: Text, z: ZoomRange): ZoomRange | null {
  const from = Math.max(0, Math.min(z.from, doc.length));
  const to = Math.max(from, Math.min(z.to, doc.length));
  if (from === 0 && to === doc.length) return null;
  return { from: doc.lineAt(from).from, to: doc.lineAt(to).to };
}

export const zoomField = StateField.define<ZoomRange | null>({
  create: () => null,
  update(value, tr) {
    for (const e of tr.effects) if (e.is(setZoom)) return e.value ? clampRange(tr.newDoc, e.value) : null;
    if (!value || !tr.docChanged) return value;
    const from = tr.changes.mapPos(value.from, -1);
    const to = tr.changes.mapPos(value.to, 1);
    // The zoomed item was deleted by an external change (sync, another editor): zoom out
    // rather than show an empty page. A user emptying it keeps the zoom (and can type again).
    if (to <= from && value.to > value.from && tr.annotation(Transaction.remote)) return null;
    return to >= from ? { from, to } : null;
  },
});

/** Range of the item/section at a 0-based line, or null when the line is neither. */
export function zoomRangeForLine(doc: Text, line0: number): ZoomRange | null {
  const n = line0 + 1;
  if (n < 1 || n > doc.lines) return null;
  const item = itemAt(doc, n);
  if (item !== null) return { from: doc.line(item).from, to: doc.line(itemEndLine(doc, item)).to };
  const fold = outlineFoldRange(doc, n);
  if (fold) return { from: doc.line(n).from, to: fold.to };
  return null;
}

class HiddenWidget extends WidgetType {
  override eq(): boolean {
    return true;
  }
  toDOM(): HTMLElement {
    const el = document.createElement('div');
    el.className = 'b-zoom-hidden';
    el.setAttribute('aria-hidden', 'true');
    return el;
  }
  override get estimatedHeight(): number {
    return 0;
  }
}

const hidden = Decoration.replace({ block: true, widget: new HiddenWidget() });

function hiddenRanges(state: EditorState): DecorationSet {
  const z = state.field(zoomField, false);
  if (!z) return Decoration.none;
  const doc = state.doc;
  const ranges = [];
  // Whole hidden lines; a lone empty line next to the zoom also takes the line break with it.
  if (z.from > 0) ranges.push(hidden.range(0, z.from > 1 ? z.from - 1 : z.from));
  if (z.to < doc.length) ranges.push(hidden.range(z.to + 1 < doc.length ? z.to + 1 : z.to, doc.length));
  return Decoration.set(ranges, true);
}

const zoomDecorations = EditorView.decorations.compute([zoomField, 'doc'], hiddenRanges);

/**
 * Drop transactions that edit outside the zoom range (its boundaries stay editable, so typing
 * at the start of the zoomed line or appending after its last line works).
 */
const zoomChangeFilter = EditorState.changeFilter.of((tr) => {
  const z = tr.startState.field(zoomField, false);
  if (!z || !tr.docChanged) return true;
  if (tr.effects.some((e) => e.is(setZoom))) return true;
  let outside = false;
  tr.changes.iterChangedRanges((fromA, toA) => {
    if (fromA < z.from || toA > z.to) outside = true;
  });
  return !outside;
});

/** Keep the selection inside the zoom range. */
const zoomSelectionFilter = EditorState.transactionFilter.of((tr) => {
  if (!tr.selection) return tr;
  const z0 = tr.startState.field(zoomField, false);
  const zoomEffect = tr.effects.find((e) => e.is(setZoom));
  if (!z0 && !zoomEffect) return tr;
  let z: ZoomRange | null;
  if (zoomEffect) z = zoomEffect.value ? clampRange(tr.newDoc, zoomEffect.value) : null;
  else z = { from: tr.changes.mapPos(z0!.from, -1), to: tr.changes.mapPos(z0!.to, 1) };
  if (!z) return tr;
  const sel = tr.selection;
  const clamp = (p: number) => Math.max(z!.from, Math.min(z!.to, p));
  if (sel.ranges.every((r) => clamp(r.anchor) === r.anchor && clamp(r.head) === r.head)) return tr;
  const fixed = EditorSelection.create(
    sel.ranges.map((r) => EditorSelection.range(clamp(r.anchor), clamp(r.head))),
    sel.mainIndex,
  );
  return [tr, { selection: fixed, sequential: true }];
});

/**
 * Undo/redo skip filters (CodeMirror's history dispatches with `filter: false`), so they can
 * change hidden text or put the cursor there: zoom out then, so the change is visible.
 */
const zoomOutOnHistory = EditorState.transactionExtender.of((tr) => {
  if (!(tr.isUserEvent('undo') || tr.isUserEvent('redo'))) return null;
  const z = tr.startState.field(zoomField, false);
  if (!z) return null;
  let outside = false;
  tr.changes.iterChangedRanges((fromA, toA) => {
    if (fromA < z.from || toA > z.to) outside = true;
  });
  if (!outside && tr.selection) {
    const nz = { from: tr.changes.mapPos(z.from, -1), to: tr.changes.mapPos(z.to, 1) };
    outside = tr.selection.ranges.some((r) => r.from < nz.from || r.to > nz.to);
  }
  return outside ? { effects: setZoom.of(null) } : null;
});

/** The zoom extension (field, decorations, filters). */
export function zoomExtension(): Extension {
  return [zoomField, zoomDecorations, zoomChangeFilter, zoomSelectionFilter, zoomOutOnHistory];
}

/** Effects zooming to the item at a 0-based line (null range = zoom out). */
export function zoomToLine(state: EditorState, line0: number | null): StateEffect<ZoomRange | null> {
  const range = line0 === null ? null : zoomRangeForLine(state.doc, line0);
  return setZoom.of(range);
}

/** Zoom into the item at the cursor. */
export const zoomIn: StateCommand = ({ state, dispatch }) => {
  const line = state.doc.lineAt(state.selection.main.head).number - 1;
  const range = zoomRangeForLine(state.doc, line);
  if (!range) return false;
  const current = state.field(zoomField, false);
  if (current && current.from === range.from && current.to === range.to) return true;
  dispatch(state.update({ effects: setZoom.of(range), selection: EditorSelection.cursor(state.selection.main.head), scrollIntoView: true }));
  return true;
};

/** Zoom out one level (to the parent item), or completely. */
export const zoomOut: StateCommand = ({ state, dispatch }) => {
  const z = state.field(zoomField, false);
  if (!z) return false;
  const doc = state.doc;
  const root = doc.lineAt(z.from).number;
  const parent = parseListLine(doc.line(root).text) ? parentItem(doc, root) : null;
  const range = parent !== null ? { from: doc.line(parent).from, to: doc.line(itemEndLine(doc, parent)).to } : null;
  dispatch(state.update({ effects: setZoom.of(range), scrollIntoView: true }));
  return true;
};

/** Alt-. : zoom into the item at the cursor, or out when already zoomed there. */
export const toggleZoom: StateCommand = (target) => {
  const { state } = target;
  const z = state.field(zoomField, false);
  if (z) {
    const range = zoomRangeForLine(state.doc, state.doc.lineAt(state.selection.main.head).number - 1);
    if (!range || (range.from === z.from && range.to === z.to)) return zoomOut(target);
  }
  return zoomIn(target);
};

/** Breadcrumb entries of the current zoom: ancestors (outermost first) and the zoomed item. */
export interface ZoomCrumb {
  /** 0-based line of the item. */
  line: number;
  text: string;
}

/** Plain text of an item/heading line for breadcrumbs. */
export function crumbText(lineText: string): string {
  const li = parseListLine(lineText);
  let s = li ? lineText.slice(li.contentFrom) : lineText.replace(/^ {0,3}#{1,6}[ \t]+/, '');
  s = s
    .replace(/[ \t]\^[A-Za-z0-9-]+[ \t]*$/, '')
    .replace(/!?\[\[([^\]|]*)\|([^\]]*)\]\]/g, '$2')
    .replace(/!?\[\[([^\]]*)\]\]/g, '$1')
    .replace(/[*_~=`]/g, '')
    .trim();
  return s.length > 60 ? `${s.slice(0, 57)}…` : s;
}

export function zoomCrumbs(state: EditorState): ZoomCrumb[] | null {
  const z = state.field(zoomField, false);
  if (!z) return null;
  const doc = state.doc;
  const root = doc.lineAt(z.from).number;
  const chain: number[] = [];
  if (parseListLine(doc.line(root).text)) {
    for (let p = parentItem(doc, root); p !== null; p = parentItem(doc, p)) chain.unshift(p);
  }
  chain.push(root);
  return chain.map((n) => ({ line: n - 1, text: crumbText(doc.line(n).text) }));
}
