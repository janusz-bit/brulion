import { insertBracket } from '@codemirror/autocomplete';
import { EditorSelection, EditorState } from '@codemirror/state';
import { describe, expect, it } from 'vitest';
import { createTranslator } from '@/core/i18n';
import { contextFor } from './context';
import { FakeWorkspace } from './fake-workspace';
import { CM_PHRASES, cmPhrases, editorExtensions } from './setup';

function state(doc = '', extra: Parameters<typeof editorExtensions>[0] = {}): EditorState {
  return EditorState.create({ doc, selection: EditorSelection.cursor(doc.length), extensions: editorExtensions(extra) });
}

function typeBracket(s: EditorState, ch: string): EditorState {
  const tr = insertBracket(s, ch);
  if (tr) return tr.state;
  return s.update(s.replaceSelection(ch)).state;
}

describe('editorExtensions', () => {
  it('builds a state without a view (no DOM needed)', () => {
    const s = state('# Tytuł\n- [ ] zadanie');
    expect(s.doc.lines).toBe(2);
    expect(s.tabSize).toBe(4);
  });

  it('auto-pairs [[ and (( with the cursor inside', () => {
    let s = state('- ');
    s = typeBracket(typeBracket(s, '['), '[');
    expect(s.doc.toString()).toBe('- [[]]');
    expect(s.selection.main.head).toBe(4);
    let r = state('zob. ');
    r = typeBracket(typeBracket(r, '('), '(');
    expect(r.doc.toString()).toBe('zob. (())');
    expect(r.selection.main.head).toBe(7);
  });

  it('does not pair quotes or braces in notes', () => {
    expect(insertBracket(state('a '), '"')).toBeNull();
    expect(insertBracket(state('a '), '{')).toBeNull();
  });

  it('read-only option', () => {
    expect(state('x', { readOnly: true }).readOnly).toBe(true);
  });

  it('Polish phrases for CodeMirror UI', () => {
    const ws = new FakeWorkspace();
    const ctx = contextFor(
      { workspace: ws, t: createTranslator('pl'), locale: 'pl', now: () => new Date(), navigate: () => {}, openLink: () => {} },
      'a.md',
    );
    expect(state('', { context: ctx }).phrase('replace all')).toBe('zamień wszystkie');
    expect(state('', { context: ctx, locale: 'en' }).phrase('replace all')).toBe('replace all');
    const pl = cmPhrases(createTranslator('pl'));
    for (const p of CM_PHRASES) expect(pl[p], p).not.toMatch(/^editor\.cm\./);
  });
});
