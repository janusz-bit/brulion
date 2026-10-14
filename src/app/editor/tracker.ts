/**
 * Track document ranges across edits while something asynchronous happens (a dialog is open,
 * a block id is being written): `trackRange(view, from, to)` returns a handle whose `range()`
 * follows later changes. Needs `rangeTracker` in the editor's extensions; without it the
 * original positions (clamped to the document) are returned.
 */
import { StateEffect, StateField } from '@codemirror/state';
import type { EditorView } from '@codemirror/view';

interface Tracked {
  from: number;
  to: number;
}

const addTracked = StateEffect.define<{ id: number; from: number; to: number }>();
const removeTracked = StateEffect.define<number>();

export const rangeTracker = StateField.define<ReadonlyMap<number, Tracked>>({
  create: () => new Map(),
  update(value, tr) {
    let next: Map<number, Tracked> | null = null;
    if (tr.docChanged && value.size > 0) {
      next = new Map();
      for (const [id, r] of value) {
        const from = tr.changes.mapPos(r.from, -1);
        next.set(id, { from, to: Math.max(from, tr.changes.mapPos(r.to, 1)) });
      }
    }
    for (const e of tr.effects) {
      if (e.is(addTracked)) (next ??= new Map(value)).set(e.value.id, { from: e.value.from, to: e.value.to });
      else if (e.is(removeTracked)) (next ??= new Map(value)).delete(e.value);
    }
    return next ?? value;
  },
});

let nextId = 1;

export interface TrackedRange {
  /** Current position of the range (original one when the tracker is not installed). */
  range(): Tracked;
  dispose(): void;
}

export function trackRange(view: EditorView, from: number, to = from): TrackedRange {
  const id = nextId++;
  const installed = view.state.field(rangeTracker, false) !== undefined;
  if (installed) view.dispatch({ effects: addTracked.of({ id, from, to }) });
  return {
    range() {
      const r = installed ? view.state.field(rangeTracker, false)?.get(id) : undefined;
      const len = view.state.doc.length;
      const f = Math.min(r?.from ?? from, len);
      return { from: f, to: Math.max(f, Math.min(r?.to ?? to, len)) };
    },
    dispose() {
      if (installed && !view.dom.isConnected) return;
      if (installed && view.state.field(rangeTracker, false)?.has(id)) view.dispatch({ effects: removeTracked.of(id) });
    },
  };
}
