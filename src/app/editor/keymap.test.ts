import { EditorSelection, EditorState } from '@codemirror/state';
import { describe, expect, it } from 'vitest';
import { toggleInline } from './keymap';

function run(mark: string, input: string): string {
  let doc = '';
  let anchor = -1;
  let head = -1;
  for (const ch of input) {
    if (ch === '|') anchor = head = doc.length;
    else if (ch === '«') anchor = doc.length;
    else if (ch === '»') head = doc.length;
    else doc += ch;
  }
  let state = EditorState.create({ doc, selection: EditorSelection.range(anchor, head) });
  toggleInline(mark)({ state, dispatch: (tr) => (state = tr.state) });
  const r = state.selection.main;
  const out = state.doc.toString();
  if (r.empty) return out.slice(0, r.head) + '|' + out.slice(r.head);
  return out.slice(0, r.from) + '«' + out.slice(r.from, r.to) + '»' + out.slice(r.to);
}

describe('toggleInline (Mod-b / Mod-i / Mod-Shift-h / Mod-e)', () => {
  it('wraps the word at the cursor or the selection', () => {
    expect(run('**', 'a pię|kny b')).toBe('a **«piękny»** b');
    expect(run('==', 'a «ważne» b')).toBe('a ==«ważne»== b');
    expect(run('`', '|')).toBe('`|`');
  });
  it('unwraps when the marks are already there', () => {
    expect(run('**', 'a **«x»** b')).toBe('a «x» b');
    expect(run('*', 'a *«x»* b')).toBe('a «x» b');
    expect(run('**', 'a «**x**» b')).toBe('a «x» b');
  });
  it('italic on bold text adds italic instead of eating a bold star', () => {
    expect(run('*', 'a **«x»** b')).toBe('a ***«x»*** b');
    expect(run('*', 'a «**x**» b')).toBe('a *«**x**»* b');
  });
  it('bold-italic text: each command removes only its own marks', () => {
    expect(run('*', 'a ***«x»*** b')).toBe('a **«x»** b');
    expect(run('**', 'a ***«x»*** b')).toBe('a *«x»* b');
  });
});
