import { history, undo } from '@codemirror/commands';
import { markdown, markdownLanguage } from '@codemirror/lang-markdown';
import { codeFolding, foldEffect, indentUnit } from '@codemirror/language';
import { EditorSelection, EditorState, Text, Transaction, type StateCommand } from '@codemirror/state';
import { describe, expect, it } from 'vitest';
import { editorContext, type EditorContext } from './context';
import { detectEol, normalizeEol, withEol } from './diff';
import {
  cycleTask,
  deleteListMarker,
  indentItems,
  insertNewlineContinueList,
  insertSoftBreak,
  itemAt,
  itemEndLine,
  moveItemDown,
  moveItemUp,
  nextMarker,
  outdentItems,
  outlineFoldRange,
  parentItem,
  parseListLine,
  previousSibling,
  selectBlock,
  usesTabs,
} from './outliner';
import { brulionMarkdown } from './syntax';
import { crumbText, zoomCrumbs, zoomExtension, zoomField, zoomIn, zoomOut, zoomRangeForLine, zoomToLine } from './zoom';

const NOW = new Date(2026, 9, 14, 10, 30);
const ctx = { now: () => NOW } as unknown as EditorContext;

/**
 * Editor state from a string with markers: `|` = cursor, `«…»` = selection (anchor «, head »).
 * Several `|` make several cursors.
 */
function make(input: string, unit = '\t'): EditorState {
  let doc = '';
  const ranges: { anchor: number; head: number }[] = [];
  let anchor = -1;
  for (const ch of input) {
    if (ch === '|') ranges.push({ anchor: doc.length, head: doc.length });
    else if (ch === '«') anchor = doc.length;
    else if (ch === '»') ranges.push({ anchor, head: doc.length });
    else doc += ch;
  }
  if (ranges.length === 0) ranges.push({ anchor: 0, head: 0 });
  return EditorState.create({
    doc,
    selection: EditorSelection.create(ranges.map((r) => EditorSelection.range(r.anchor, r.head))),
    extensions: [
      EditorState.allowMultipleSelections.of(true),
      markdown({ base: markdownLanguage, extensions: brulionMarkdown }),
      indentUnit.of(unit),
      editorContext.of(ctx),
      zoomExtension(),
    ],
  });
}

function show(state: EditorState): string {
  const doc = state.doc.toString();
  const marks: [number, string][] = [];
  for (const r of state.selection.ranges) {
    if (r.empty) marks.push([r.head, '|']);
    else {
      marks.push([r.anchor, '«']);
      marks.push([r.head, '»']);
    }
  }
  marks.sort((a, b) => b[0] - a[0]);
  let out = doc;
  for (const [pos, m] of marks) out = out.slice(0, pos) + m + out.slice(pos);
  return out;
}

/** Run a command; returns [handled, resulting text with markers]. */
function exec(cmd: StateCommand, input: string, unit?: string): [boolean, string] {
  let state = make(input, unit);
  const handled = cmd({
    state,
    dispatch: (tr) => {
      state = tr.state;
    },
  });
  return [handled, show(state)];
}

const run = (cmd: StateCommand, input: string, unit?: string) => exec(cmd, input, unit)[1];

describe('parseListLine', () => {
  it('parses bullets, numbers and checkboxes', () => {
    expect(parseListLine('- a')).toMatchObject({ marker: '-', indentCols: 0, contentFrom: 2, checkbox: null });
    expect(parseListLine('\t* [x] zrobione')).toMatchObject({ marker: '*', indentCols: 4, checkbox: 'x', contentFrom: 7 });
    expect(parseListLine('  12) dwanaście')).toMatchObject({ number: 12, delimiter: ')', indentCols: 2, contentFrom: 6 });
    expect(parseListLine('-')).toMatchObject({ marker: '-', contentFrom: 1 });
    expect(parseListLine('- [ ]')).toMatchObject({ checkbox: ' ', contentFrom: 5 });
  });
  it('rejects non-items', () => {
    expect(parseListLine('-a')).toBeNull();
    expect(parseListLine('---')).toBeNull();
    expect(parseListLine('- - -')).toBeNull();
    expect(parseListLine('tekst')).toBeNull();
    expect(parseListLine('1.5 kg')).toBeNull();
  });
  it('nextMarker', () => {
    expect(nextMarker(parseListLine('- a')!)).toBe('- ');
    expect(nextMarker(parseListLine('9. a')!)).toBe('10. ');
    expect(nextMarker(parseListLine('+ [x] a')!)).toBe('+ [ ] ');
  });
});

describe('outline navigation', () => {
  const doc = Text.of(['- a', '\t- b', '\t\tcontinued', '', '\t- c', '- d', 'para', '  indented']);
  it('itemEndLine includes children and inner blank lines', () => {
    expect(itemEndLine(doc, 1)).toBe(5);
    expect(itemEndLine(doc, 2)).toBe(3);
    expect(itemEndLine(doc, 6)).toBe(6);
  });
  it('siblings, parents and owners', () => {
    expect(previousSibling(doc, 5)).toBe(2);
    expect(previousSibling(doc, 2)).toBeNull();
    expect(previousSibling(doc, 6)).toBe(1);
    expect(parentItem(doc, 5)).toBe(1);
    expect(parentItem(doc, 1)).toBeNull();
    expect(itemAt(doc, 3)).toBe(2);
    expect(itemAt(doc, 7)).toBeNull();
    expect(itemAt(doc, 4)).toBeNull();
  });
  it('detects indentation style', () => {
    expect(usesTabs(doc)).toBe(true);
    expect(usesTabs(Text.of(['- a', '  - b']))).toBe(false);
    expect(usesTabs(Text.of(['- a']), false)).toBe(false);
  });
});

describe('Enter', () => {
  it('continues a bullet list', () => {
    expect(run(insertNewlineContinueList, '- zakupy|')).toBe('- zakupy\n- |');
    expect(run(insertNewlineContinueList, '* jeden|')).toBe('* jeden\n* |');
    expect(run(insertNewlineContinueList, '\t- głęboko|')).toBe('\t- głęboko\n\t- |');
  });
  it('splits the item at the cursor', () => {
    expect(run(insertNewlineContinueList, '- ala| ma kota')).toBe('- ala\n- |ma kota');
  });
  it('continues tasks unchecked', () => {
    expect(run(insertNewlineContinueList, '- [x] zrobione|')).toBe('- [x] zrobione\n- [ ] |');
    expect(run(insertNewlineContinueList, '- [ ] zadanie 📅 2026-10-15|')).toBe('- [ ] zadanie 📅 2026-10-15\n- [ ] |');
  });
  it('increments numbered lists and renumbers the following items', () => {
    expect(run(insertNewlineContinueList, '1. a|\n2. b\n3. c')).toBe('1. a\n2. |\n3. b\n4. c');
    expect(run(insertNewlineContinueList, '1) a|')).toBe('1) a\n2) |');
  });
  it('an empty nested item outdents with its children', () => {
    expect(run(insertNewlineContinueList, '- a\n\t- |\n\t\t- c')).toBe('- a\n- |\n\t- c');
    expect(run(insertNewlineContinueList, '- a\n  - |', '  ')).toBe('- a\n- |');
  });
  it('an empty top-level item ends the list', () => {
    expect(run(insertNewlineContinueList, '- a\n- |')).toBe('- a\n|');
    expect(run(insertNewlineContinueList, '- a\n- [ ] |')).toBe('- a\n|');
  });
  it('at the end of an item with children inserts the first child', () => {
    expect(run(insertNewlineContinueList, '- rodzic|\n\t- dziecko')).toBe('- rodzic\n\t- |\n\t- dziecko');
    expect(run(insertNewlineContinueList, '- r|\n\t1. x\n\t2. y')).toBe('- r\n\t1. |\n\t2. x\n\t3. y');
  });
  it('is not handled outside lists, before the marker or in code', () => {
    expect(exec(insertNewlineContinueList, 'zwykły tekst|')[0]).toBe(false);
    expect(exec(insertNewlineContinueList, '|- a')[0]).toBe(false);
    expect(exec(insertNewlineContinueList, '```\n- a|\n```')[0]).toBe(false);
  });
  it('handles several cursors', () => {
    expect(run(insertNewlineContinueList, '- a|\n- b|')).toBe('- a\n- |\n- b\n- |');
  });
  it('replaces a selection inside the item', () => {
    expect(run(insertNewlineContinueList, '- a«bc»d')).toBe('- a\n- |d');
  });
  it('Shift-Enter inserts an aligned soft break', () => {
    expect(run(insertSoftBreak, '- tekst|')).toBe('- tekst\n  |');
    expect(run(insertSoftBreak, '\t1. tekst|')).toBe('\t1. tekst\n\t   |');
    expect(exec(insertSoftBreak, 'akapit|')[0]).toBe(false);
  });
});

describe('Backspace after a marker', () => {
  it('removes the marker of an empty item', () => {
    expect(run(deleteListMarker, '- a\n- |')).toBe('- a\n|');
    expect(run(deleteListMarker, '- a\n\t- |')).toBe('- a\n|');
  });
  it('removes a checkbox first', () => {
    expect(run(deleteListMarker, '- [ ] |zadanie')).toBe('- |zadanie');
  });
  it('is not handled elsewhere', () => {
    expect(exec(deleteListMarker, '- |tekst')[0]).toBe(false);
    expect(exec(deleteListMarker, '- te|kst')[0]).toBe(false);
    expect(exec(deleteListMarker, 'te|kst')[0]).toBe(false);
  });
});

describe('Tab / Shift-Tab', () => {
  it('indents an item with its subtree under the previous sibling (tabs)', () => {
    expect(run(indentItems, '- a\n- b|\n\t- c\n- d')).toBe('- a\n\t- b|\n\t\t- c\n- d');
  });
  it('indents with spaces to the parent content column', () => {
    expect(run(indentItems, '- a\n- b|', '  ')).toBe('- a\n  - b|');
    // The item starts a new nested list: numbered from 1.
    expect(run(indentItems, '1. a\n2. b|', '  ')).toBe('1. a\n   1. b|');
    expect(run(indentItems, '- a\n  - b\n  - c|\n    - d', '  ')).toBe('- a\n  - b\n    - c|\n      - d');
  });
  it('keeps blank lines inside the subtree untouched', () => {
    expect(run(indentItems, '- a\n- b|\n\n\t- c')).toBe('- a\n\t- b|\n\n\t\t- c');
  });
  it('cannot indent the first item (consumes the key)', () => {
    expect(exec(indentItems, '- a|\n- b')).toEqual([true, '- a|\n- b']);
  });
  it('indents every selected item once', () => {
    expect(run(indentItems, '- a\n- «b\n\t- x\n- c»')).toBe('- a\n\t- «b\n\t\t- x\n\t- c»');
  });
  it('works from a continuation line', () => {
    expect(run(indentItems, '- a\n- b\n  cont|')).toBe('- a\n\t- b\n\t  cont|');
  });
  it('is not handled outside lists', () => {
    expect(exec(indentItems, 'tekst|')[0]).toBe(false);
  });
  it('outdents with the subtree', () => {
    expect(run(outdentItems, '- a\n\t- b|\n\t\t- c\n- d')).toBe('- a\n- b|\n\t- c\n- d');
  });
  it('following siblings become children of the outdented item', () => {
    expect(run(outdentItems, '- a\n\t- b|\n\t- c')).toBe('- a\n- b|\n\t- c');
  });
  it('outdents spaces to the parent column', () => {
    expect(run(outdentItems, '1. a\n   - b|\n     - c', '  ')).toBe('1. a\n- b|\n  - c');
  });
  it('does nothing at the top level', () => {
    expect(exec(outdentItems, '- a|')).toEqual([true, '- a|']);
  });
  it('crumb text strips markup', () => {
    expect(crumbText('\t- [ ] [[Strona|alias]] i ==to== ^abc')).toBe('alias i to');
    expect(crumbText('## Nagłówek')).toBe('Nagłówek');
  });
  it('mixed tabs and spaces', () => {
    expect(run(outdentItems, '- a\n\t- b\n\t    - c|')).toBe('- a\n\t- b\n\t- c|');
  });
});

describe('move items', () => {
  it('moves an item with its subtree past the previous sibling', () => {
    expect(run(moveItemUp, '- a\n\t- a1\n- b|\n\t- b1')).toBe('- b|\n\t- b1\n- a\n\t- a1');
  });
  it('moves down past the next sibling', () => {
    expect(run(moveItemDown, '- a|\n\t- a1\n- b\n\t- b1\n- c')).toBe('- b\n\t- b1\n- a|\n\t- a1\n- c');
  });
  it('keeps blank lines between siblings in place', () => {
    expect(run(moveItemUp, '- a\n\n- b|')).toBe('- b|\n\n- a');
  });
  it('keeps ordered numbering', () => {
    expect(run(moveItemUp, '1. a\n2. b|\n3. c')).toBe('1. b|\n2. a\n3. c');
    expect(run(moveItemDown, '1. a|\n2. b\n3. c')).toBe('1. b\n2. a|\n3. c');
  });
  it('stays at the list boundaries', () => {
    expect(exec(moveItemUp, '- a|\n- b')).toEqual([true, '- a|\n- b']);
    expect(exec(moveItemDown, '- a\n\t- x|\n- b')).toEqual([true, '- a\n\t- x|\n- b']);
  });
  it('moves nested items among their siblings only', () => {
    expect(run(moveItemDown, '- a\n\t- x|\n\t- y\n- b')).toBe('- a\n\t- y\n\t- x|\n- b');
  });
  it('moves plain lines like moveLineUp/Down', () => {
    expect(run(moveItemUp, 'jeden\ndwa|')).toBe('dwa|\njeden');
    expect(run(moveItemDown, 'jeden|\ndwa')).toBe('dwa\njeden|');
  });
  it('moves a selection of siblings together', () => {
    expect(run(moveItemUp, '- a\n- «b\n- c»')).toBe('- «b\n- c»\n- a');
  });
});

describe('cycle task (Mod-Enter)', () => {
  it('plain line → task → done → todo', () => {
    const a = run(cycleTask, '- kup mleko|');
    expect(a).toBe('- [ ] kup mleko|');
    const b = run(cycleTask, a);
    expect(b).toBe('- [x] kup mleko ✅ 2026-10-14|');
    expect(run(cycleTask, b)).toBe('- [ ] kup mleko|');
  });
  it('turns a paragraph into a task', () => {
    expect(run(cycleTask, 'zadzwoń|')).toBe('- [ ] zadzwoń|');
  });
  it('completing a recurring task inserts the next occurrence above', () => {
    const out = run(cycleTask, '- [ ] podlać kwiaty 🔁 every week 📅 2026-10-14|');
    expect(out).toBe('- [ ] podlać kwiaty 🔁 every week 📅 2026-10-21\n- [x] podlać kwiaty 🔁 every week 📅 2026-10-14 ✅ 2026-10-14|');
  });
  it('toggles every selected item', () => {
    expect(run(cycleTask, '- «a\n- b»')).toBe('- [ ] «a\n- [ ] b»');
  });
  it('works on Logseq keywords', () => {
    expect(run(cycleTask, '- TODO x|')).toMatch(/^- DONE x/);
  });
});

describe('select block', () => {
  it('selects the item with children, then the parent', () => {
    const once = run(selectBlock, '- a\n\t- b|\n\t\t- c\n- d');
    expect(once).toBe('- a\n«\t- b\n\t\t- c»\n- d');
    expect(run(selectBlock, once)).toBe('«- a\n\t- b\n\t\t- c»\n- d');
  });
  it('selects a paragraph outside lists', () => {
    expect(run(selectBlock, 'x\n\nab|c\nde\n\ny')).toBe('x\n\n«abc\nde»\n\ny');
  });
});

describe('folding ranges', () => {
  it('folds list items with children', () => {
    const doc = Text.of(['- a', '\t- b', '- c']);
    expect(outlineFoldRange(doc, 1)).toEqual({ from: 3, to: 8 });
    expect(outlineFoldRange(doc, 3)).toBeNull();
  });
  it('folds heading sections up to the next heading of the same level', () => {
    const doc = Text.of(['# A', 'x', '## B', 'y', '', '# C']);
    expect(outlineFoldRange(doc, 1)).toEqual({ from: 3, to: doc.line(4).to });
    expect(outlineFoldRange(doc, 3)).toEqual({ from: doc.line(3).to, to: doc.line(4).to });
    expect(outlineFoldRange(doc, 6)).toBeNull();
  });
});

describe('zoom', () => {
  it('computes the range of an item and its children', () => {
    const doc = Text.of(['- a', '\t- b', '\t\t- c', '- d']);
    expect(zoomRangeForLine(doc, 1)).toEqual({ from: doc.line(2).from, to: doc.line(3).to });
    expect(zoomRangeForLine(doc, 2)).toEqual({ from: doc.line(3).from, to: doc.line(3).to });
  });

  function zoomed(input: string, line0: number): EditorState {
    const s = make(input);
    return s.update({ effects: zoomToLine(s, line0) }).state;
  }

  it('blocks edits outside the zoomed item and keeps the selection inside', () => {
    let s = zoomed('- a\n\t- b|\n\t\t- c\n- d', 1);
    const z = s.field(zoomField)!;
    expect(s.sliceDoc(z.from, z.to)).toBe('\t- b\n\t\t- c');
    s = s.update({ changes: { from: 0, insert: 'X' } }).state;
    expect(s.doc.toString()).toBe('- a\n\t- b\n\t\t- c\n- d');
    s = s.update({ changes: { from: s.doc.length, insert: 'Y' } }).state;
    expect(s.doc.toString()).toBe('- a\n\t- b\n\t\t- c\n- d');
    s = s.update({ selection: EditorSelection.cursor(0) }).state;
    expect(s.selection.main.head).toBe(z.from);
  });

  it('allows edits at the zoom boundaries and grows with them', () => {
    let s = zoomed('- a\n\t- b|\n\t\t- c\n- d', 1);
    const z = s.field(zoomField)!;
    s = s.update({ changes: { from: z.to, insert: '\n\t\t- nowe' } }).state;
    expect(s.doc.toString()).toBe('- a\n\t- b\n\t\t- c\n\t\t- nowe\n- d');
    const z2 = s.field(zoomField)!;
    expect(s.sliceDoc(z2.from, z2.to)).toBe('\t- b\n\t\t- c\n\t\t- nowe');
  });

  it('external transactions bypass the filter', () => {
    let s = zoomed('- a\n\t- b|\n- d', 1);
    s = s.update({ changes: { from: 0, insert: 'X' }, filter: false }).state;
    expect(s.doc.toString()).toBe('X- a\n\t- b\n- d');
    const z = s.field(zoomField)!;
    expect(s.sliceDoc(z.from, z.to)).toBe('\t- b');
  });

  it('zoom commands and breadcrumbs', () => {
    let s = make('- Projekt\n\t- Etap 1 ^e1\n\t\t- **[[Zadanie]]** x|\n- inne');
    const step = (cmd: StateCommand) => {
      cmd({ state: s, dispatch: (tr) => (s = tr.state) });
    };
    step(zoomIn);
    expect(zoomCrumbs(s)).toEqual([
      { line: 0, text: 'Projekt' },
      { line: 1, text: 'Etap 1' },
      { line: 2, text: 'Zadanie x' },
    ]);
    step(zoomOut);
    expect(zoomCrumbs(s)!.map((c) => c.line)).toEqual([0, 1]);
    step(zoomOut);
    expect(zoomCrumbs(s)!.map((c) => c.line)).toEqual([0]);
    step(zoomOut);
    expect(zoomCrumbs(s)).toBeNull();
  });
});

describe('line endings', () => {
  it('CRLF documents keep CRLF through an outliner edit', () => {
    const file = '- a\r\n\t- b\r\n- c\r\n';
    const eol = detectEol(file);
    expect(eol).toBe('\r\n');
    let state = make(normalizeEol(file));
    state = state.update({ selection: EditorSelection.cursor(state.doc.line(3).to) }).state;
    moveItemUp({ state, dispatch: (tr) => (state = tr.state) });
    expect(withEol(state.doc.toString(), eol)).toBe('- c\r\n- a\r\n\t- b\r\n');
  });
  it('CodeMirror splits CRLF lines (no stray \\r in list detection)', () => {
    const state = EditorState.create({ doc: '- a\r\n- b' });
    expect(state.doc.lines).toBe(2);
    expect(parseListLine(state.doc.line(1).text)).not.toBeNull();
  });
});

describe('review regressions', () => {
  it('Tab with spaces keeps children nested when they are indented with a tab', () => {
    // b's child c is indented with a tab (4 columns); spaces in front of that tab would be
    // swallowed by its tab stop and c would become b's sibling.
    expect(run(indentItems, '- r\n  - a\n  - b|\n\t- c', '  ')).toBe('- r\n  - a\n    - b|\n\t  - c');
  });

  it('moving an ordered item keeps the cursor in its text when the number grows', () => {
    expect(run(moveItemDown, '9. a|\n10. b')).toBe('9. b\n10. a|');
    expect(run(moveItemDown, '9. a|b\n10. c')).toBe('9. c\n10. a|b');
    expect(run(moveItemUp, '9. a\n10. b|c')).toBe('9. b|c\n10. a');
  });

  it('Backspace on an empty task removes the checkbox first', () => {
    expect(run(deleteListMarker, '- [ ] |')).toBe('- |');
    expect(run(deleteListMarker, '\t- [x]|')).toBe('\t- |');
    expect(run(deleteListMarker, '- |')).toBe('|');
  });

  function withHistory(input: string): { get: () => EditorState; step: (c: StateCommand) => boolean } {
    let s = EditorState.create({
      doc: input.replace('|', ''),
      selection: EditorSelection.cursor(input.indexOf('|')),
      extensions: [markdown({ base: markdownLanguage, extensions: brulionMarkdown }), indentUnit.of('\t'), editorContext.of(ctx), zoomExtension(), history(), codeFolding()],
    });
    return { get: () => s, step: (c) => c({ state: s, dispatch: (tr) => (s = tr.state) }) };
  }

  it('every structural command is its own undo step', () => {
    for (const [cmd, input] of [
      [outdentItems, '- a\n\t- b\n\t\t- c|'],
      [indentItems, '- a\n- b\n\t- x\n- c|'],
      [moveItemUp, '- a\n- b\n- c|'],
      [cycleTask, '- a|'],
    ] as const) {
      const h = withHistory(input);
      const start = h.get().doc.toString();
      h.step(cmd);
      const once = h.get().doc.toString();
      h.step(cmd);
      expect(h.get().doc.toString(), cmd.name).not.toBe(once);
      h.step(undo);
      expect(h.get().doc.toString(), cmd.name).toBe(once);
      h.step(undo);
      expect(h.get().doc.toString(), cmd.name).toBe(start);
    }
  });

  it('Enter at the end of a folded item adds a sibling after its hidden children', () => {
    const enterFolded = (input: string, foldTo: number) => {
      let s = withHistory(input).get();
      s = s.update({ effects: foldEffect.of({ from: s.doc.line(1).to, to: s.doc.line(foldTo).to }) }).state;
      insertNewlineContinueList({ state: s, dispatch: (tr) => (s = tr.state) });
      return show(s);
    };
    expect(enterFolded('- rodzic|\n\t- dziecko\n\t- drugie\n- dalej', 3)).toBe('- rodzic\n\t- dziecko\n\t- drugie\n- |\n- dalej');
    expect(enterFolded('1. a| \n\t- x\n2. b', 2)).toBe('1. a\n\t- x\n2. |\n3. b');
    // Not folded: the new item is the first child (unchanged behaviour).
    expect(run(insertNewlineContinueList, '- rodzic|\n\t- dziecko')).toBe('- rodzic\n\t- |\n\t- dziecko');
  });

  it('undo of an edit outside the zoomed item zooms out instead of changing hidden text', () => {
    const h = withHistory('- a\n- b\n- c|');
    let s = h.get();
    s = s.update({ changes: { from: 0, insert: 'X' }, userEvent: 'input.type' }).state;
    s = s.update({ effects: zoomToLine(s, 2) }).state;
    expect(s.field(zoomField)).not.toBeNull();
    undo({ state: s, dispatch: (tr) => (s = tr.state) });
    expect(s.doc.toString()).toBe('- a\n- b\n- c');
    expect(s.field(zoomField)).toBeNull();
  });

  it('an external change deleting the zoomed item zooms out', () => {
    let s = withHistory('- a\n- b\n- c|').get();
    s = s.update({ effects: zoomToLine(s, 1) }).state;
    expect(s.field(zoomField)).not.toBeNull();
    s = s.update({ changes: { from: 3, to: 7 }, filter: false, annotations: Transaction.remote.of(true) }).state;
    expect(s.doc.toString()).toBe('- a\n- c');
    expect(s.field(zoomField)).toBeNull();
  });

  it('undo inside the zoomed item keeps the zoom', () => {
    let s = withHistory('- a\n- b\n- c|').get();
    s = s.update({ effects: zoomToLine(s, 2) }).state;
    s = s.update({ changes: { from: s.doc.length, insert: 'Z' }, selection: EditorSelection.cursor(s.doc.length + 1), userEvent: 'input.type' }).state;
    undo({ state: s, dispatch: (tr) => (s = tr.state) });
    expect(s.doc.toString()).toBe('- a\n- b\n- c');
    expect(s.field(zoomField)).not.toBeNull();
  });

  it('Enter at the end of an item line puts the new item after its continuation lines', () => {
    // The continuation stays with its item instead of moving to the new empty one.
    expect(run(insertNewlineContinueList, '- notatka|\n  szczegóły spotkania\n- dalej')).toBe('- notatka\n  szczegóły spotkania\n- |\n- dalej');
    expect(run(insertNewlineContinueList, '- a|\n  opis\n  - dziecko')).toBe('- a\n  opis\n  - |\n  - dziecko');
    expect(run(insertNewlineContinueList, '1. a|\n   opis\n2. b')).toBe('1. a\n   opis\n2. |\n3. b');
    // A code block inside the item is part of its text.
    expect(run(insertNewlineContinueList, '- kod|\n  ```\n  x = 1\n  ```\n- b')).toBe('- kod\n  ```\n  x = 1\n  ```\n- |\n- b');
  });

  it('Enter at the end of the last continuation line starts a new item', () => {
    expect(run(insertNewlineContinueList, '- a\n  więcej tekstu|\n- b')).toBe('- a\n  więcej tekstu\n- |\n- b');
    expect(run(insertNewlineContinueList, '\t- a\n\t  ciąg dalszy|  \n\t\t- dziecko')).toBe('\t- a\n\t  ciąg dalszy\n\t\t- |\n\t\t- dziecko');
    // Elsewhere in the item's text (and in quotes) Enter stays a line break of the item.
    expect(exec(insertNewlineContinueList, '- a\n  więcej| tekstu\n- b')[0]).toBe(false);
    expect(exec(insertNewlineContinueList, '- a\n  linia 1|\n  linia 2')[0]).toBe(false);
    expect(exec(insertNewlineContinueList, '- a\n  > cytat|')[0]).toBe(false);
    expect(exec(insertNewlineContinueList, 'akapit\n  wcięty|')[0]).toBe(false);
  });

  it('Enter at the start of an item adds an empty item above and keeps the task state', () => {
    expect(run(insertNewlineContinueList, '- [x] |zrobione ✅ 2026-10-10\n- b')).toBe('- [ ] \n- [x] |zrobione ✅ 2026-10-10\n- b');
    expect(run(insertNewlineContinueList, '- |ala ^id1')).toBe('- \n- |ala ^id1');
    expect(run(insertNewlineContinueList, '1. |a\n2. b')).toBe('1. \n2. |a\n3. b');
    expect(run(insertNewlineContinueList, '\t9) |a\n\t10) b')).toBe('\t9) \n\t10) |a\n\t11) b');
  });

  it('parentItem looks through continuation lines; outdent goes to the right level', () => {
    const doc = Text.of(['- a', '  tekst', '    - dziecko']);
    expect(parentItem(doc, 3)).toBe(1);
    expect(run(outdentItems, '- a\n  tekst\n    - dzie|cko', '  ')).toBe('- a\n  tekst\n- dzie|cko');
    expect(itemAt(doc, 2)).toBe(1);
  });

  it('indent renumbers ordered lists: the new nested list and the one left behind', () => {
    expect(run(indentItems, '1. a\n2. b|\n3. c', '  ')).toBe('1. a\n   1. b|\n2. c');
    expect(run(indentItems, '1. a\n   1. x\n2. b|\n3. c', '  ')).toBe('1. a\n   1. x\n   2. b|\n2. c');
    // A list starting at 5 keeps its start.
    expect(run(indentItems, '5. a\n6. b|\n7. c\n8. d', '  ')).toBe('5. a\n   1. b|\n6. c\n7. d');
    expect(run(indentItems, '1. a\n2. «b\n3. c»\n4. d', '  ')).toBe('1. a\n   1. «b\n   2. c»\n2. d');
    expect(run(indentItems, '1) a\n2) b|\n3) c')).toBe('1) a\n\t1) b|\n2) c');
    // Bullets are left alone, also next to numbers.
    expect(run(indentItems, '- a\n- b|\n- c', '  ')).toBe('- a\n  - b|\n- c');
    expect(run(indentItems, '1. a\n- b|\n2. c', '  ')).toBe('1. a\n   - b|\n2. c');
  });

  it('outdent renumbers: the item follows its former parent, its former next siblings become its children', () => {
    expect(run(outdentItems, '1. a\n   1. b|\n2. c', '  ')).toBe('1. a\n2. b|\n3. c');
    expect(run(outdentItems, '1. a\n   1. x\n   2. b|\n   3. y\n2. c', '  ')).toBe('1. a\n   1. x\n2. b|\n   1. y\n3. c');
    expect(run(outdentItems, '1. a\n   1. b|\n      1. b1\n   2. x\n2. c', '  ')).toBe('1. a\n2. b|\n   1. b1\n   2. x\n3. c');
  });

  it('indent / outdent with renumbering is one undo step', () => {
    for (const [cmd, input] of [
      [indentItems, '1. a\n2. b|\n3. c'],
      [outdentItems, '1. a\n\t1. b|\n2. c'],
    ] as const) {
      const h = withHistory(input);
      const start = h.get().doc.toString();
      h.step(cmd);
      expect(h.get().doc.toString()).not.toBe(start);
      h.step(undo);
      expect(h.get().doc.toString(), cmd.name).toBe(start);
    }
  });
});
