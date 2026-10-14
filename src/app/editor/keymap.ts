/**
 * Editor key bindings (docs/UI.md "Keyboard"): outliner editing, task toggle, folding, zoom and
 * Markdown formatting. App-level shortcuts (Ctrl+K palette, Ctrl+O switcher, Ctrl+J today,
 * Alt+←/→ back/forward, Ctrl+Shift+F search) are left to the shell: CodeMirror's own
 * Alt+←/→ binding is removed so those reach it.
 *
 * | Key | Action |
 * |---|---|
 * | Enter / Shift-Enter | continue list / line break inside the item |
 * | Backspace | after a marker: drop the checkbox / the empty item's marker |
 * | Tab / Shift-Tab | indent / outdent item with children (plain text: indent) |
 * | Alt-↑ / Alt-↓ | move item with children (plain lines: move line) |
 * | Mod-Enter | cycle task: plain → [ ] → [x] ✅ → [ ] |
 * | Mod-. / Mod-Shift-. | fold/unfold item or section / fold all ↔ unfold all |
 * | Mod-Alt-→ / Mod-Alt-← / Alt-. | zoom into item / zoom out / toggle |
 * | Mod-Shift-a | select block (again: parent block) |
 * | Alt-Enter | open the link / tag / block reference at the cursor |
 * | Mod-b / Mod-i / Mod-Shift-h / Mod-e | bold / italic / ==highlight== / `code` |
 */
import { defaultKeymap, historyKeymap, indentLess, indentMore, insertTab } from '@codemirror/commands';
import { deleteMarkupBackward, insertNewlineContinueMarkup } from '@codemirror/lang-markdown';
import { foldKeymap, toggleFold } from '@codemirror/language';
import { searchKeymap } from '@codemirror/search';
import { EditorSelection, Prec, type Extension, type StateCommand } from '@codemirror/state';
import { keymap, type KeyBinding } from '@codemirror/view';
import {
  cycleTask,
  deleteListMarker,
  indentItems,
  insertNewlineContinueList,
  insertSoftBreak,
  moveItemDown,
  moveItemUp,
  outdentItems,
  selectBlock,
  toggleFoldAll,
} from './outliner';
import { openLinkAtCursor } from './links';
import { toggleZoom, zoomIn, zoomOut } from './zoom';

/** Length of the run of `ch` ending at `pos` (backwards) or starting at `pos` (forwards). */
function runLength(text: string, pos: number, ch: string, dir: -1 | 1): number {
  let n = 0;
  for (let i = dir < 0 ? pos - 1 : pos; i >= 0 && i < text.length && text[i] === ch; i += dir) n++;
  return n;
}

/**
 * Whether runs of `before`/`after` mark characters carry `mark`: `*` (italic) lives in odd runs
 * (`*x*`, `***x***`), `**` in runs of two or more; other marks need at least their length.
 * Without this, Mod-i on `**bold**` would eat one star of the bold instead of adding italic.
 */
function carries(mark: string, before: number, after: number): boolean {
  if (mark === '*' || mark === '_') return before % 2 === 1 && after % 2 === 1;
  return before >= mark.length && after >= mark.length;
}

/** Wrap/unwrap the selection (or the word at the cursor) in `mark`. */
export function toggleInline(mark: string): StateCommand {
  return ({ state, dispatch }) => {
    if (state.readOnly) return false;
    const ch = mark[0]!;
    const repeated = mark === ch.repeat(mark.length);
    const tr = state.changeByRange((range) => {
      let { from, to } = range;
      if (range.empty) {
        const word = state.wordAt(range.head);
        if (word) ({ from, to } = word);
      }
      const len = mark.length;
      const line = state.doc.lineAt(from);
      const text = line.text;
      const f = from - line.from;
      const t = Math.min(to, line.to) - line.from;
      // Marks just outside the range: `**|x|**`.
      const outside = repeated
        ? carries(mark, runLength(text, f, ch, -1), runLength(text, t, ch, 1))
        : state.sliceDoc(from - len, from) === mark && state.sliceDoc(to, to + len) === mark;
      if (outside && to <= line.to) {
        return {
          changes: [
            { from: from - len, to: from },
            { from: to, to: to + len },
          ],
          range: EditorSelection.range(range.anchor - len, range.head - len),
        };
      }
      // Marks just inside the range: `|**x**|`.
      const inner = state.sliceDoc(from, to);
      const inside =
        inner.length >= 2 * len &&
        (repeated
          ? carries(mark, runLength(inner, 0, ch, 1), runLength(inner, inner.length, ch, -1)) &&
            runLength(inner, 0, ch, 1) < inner.length
          : inner.startsWith(mark) && inner.endsWith(mark));
      if (inside) {
        return {
          changes: { from, to, insert: inner.slice(len, inner.length - len) },
          range: EditorSelection.range(from, to - 2 * len),
        };
      }
      return {
        changes: [
          { from, insert: mark },
          { from: to, insert: mark },
        ],
        range: range.empty && from === to ? EditorSelection.cursor(from + len) : EditorSelection.range(from + len, to + len),
      };
    });
    dispatch(state.update(tr, { userEvent: 'input.format', scrollIntoView: true }));
    return true;
  };
}

/** Keys that must reach the app shell instead of CodeMirror's defaults. */
const SHELL_KEYS = new Set(['Alt-ArrowLeft', 'Alt-ArrowRight', 'Mod-Alt-ArrowLeft', 'Mod-Alt-ArrowRight']);

function withoutShellKeys(bindings: readonly KeyBinding[]): KeyBinding[] {
  return bindings.filter((b) => !(b.key && SHELL_KEYS.has(b.key)) && !(b.mac && SHELL_KEYS.has(b.mac)));
}

/** Brulion bindings (high precedence) before CodeMirror's standard keymaps. */
export const brulionKeymap: readonly KeyBinding[] = [
  { key: 'Enter', run: insertNewlineContinueList },
  { key: 'Enter', run: insertNewlineContinueMarkup },
  { key: 'Shift-Enter', run: insertSoftBreak },
  { key: 'Backspace', run: deleteListMarker },
  { key: 'Backspace', run: deleteMarkupBackward },
  { key: 'Tab', run: indentItems, shift: outdentItems },
  {
    key: 'Tab',
    run: (target) => (target.state.selection.ranges.some((r) => !r.empty) ? indentMore(target) : insertTab(target)),
    shift: indentLess,
  },
  { key: 'Alt-ArrowUp', run: moveItemUp },
  { key: 'Alt-ArrowDown', run: moveItemDown },
  { key: 'Mod-Enter', run: cycleTask },
  { key: 'Mod-.', run: toggleFold },
  { key: 'Mod-Shift-.', run: toggleFoldAll },
  { key: 'Mod->', run: toggleFoldAll },
  { key: 'Mod-Alt-ArrowRight', run: zoomIn },
  { key: 'Mod-Alt-ArrowLeft', run: zoomOut },
  { key: 'Alt-.', run: toggleZoom },
  { key: 'Mod-Shift-a', run: selectBlock },
  { key: 'Alt-Enter', run: openLinkAtCursor },
  { key: 'Mod-b', run: toggleInline('**') },
  { key: 'Mod-i', run: toggleInline('*') },
  { key: 'Mod-Shift-h', run: toggleInline('==') },
  { key: 'Mod-e', run: toggleInline('`') },
];

export function editorKeymaps(): Extension {
  return [
    Prec.high(keymap.of([...brulionKeymap])),
    keymap.of([...withoutShellKeys(defaultKeymap), ...historyKeymap, ...searchKeymap, ...foldKeymap]),
  ];
}
