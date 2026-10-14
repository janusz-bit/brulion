// @vitest-environment jsdom
import './testutil';
import { EditorSelection, EditorState } from '@codemirror/state';
import { EditorView } from '@codemirror/view';
import { afterEach, describe, expect, it } from 'vitest';
import { createTranslator } from '@/core/i18n';
import { contextFor, editorContext, type EditorRoute } from './context';
import { FakeWorkspace } from './fake-workspace';
import { linkAt, openLinkAtCursor } from './links';
import { brulionLanguage } from './setup';

const ws = new FakeWorkspace({ files: { 'Dom.md': '- kuchnia ^k1\n' } });
const routes: EditorRoute[] = [];
const opened: string[] = [];
let views: EditorView[] = [];
afterEach(() => {
  for (const v of views) v.destroy();
  views = [];
  routes.length = 0;
  opened.length = 0;
});

function view(doc: string, pos: number): EditorView {
  const ctx = contextFor(
    {
      workspace: ws,
      t: createTranslator('pl'),
      locale: 'pl',
      now: () => new Date(2026, 9, 14),
      navigate: (r) => routes.push(r),
      openLink: (target) => opened.push(target),
    },
    'a.md',
  );
  const v = new EditorView({
    state: EditorState.create({ doc, selection: EditorSelection.cursor(pos), extensions: [brulionLanguage(), editorContext.of(ctx)] }),
    parent: document.body,
  });
  views.push(v);
  return v;
}

describe('links', () => {
  it('finds link targets under the cursor', () => {
    const s = (doc: string, pos: number) => linkAt(view(doc, pos).state, pos);
    expect(s('x [[Dom#Kuchnia|d]] y', 5)).toEqual({ kind: 'page', target: 'Dom#Kuchnia' });
    expect(s('![[plan.png]]', 1)).toEqual({ kind: 'page', target: 'plan.png' });
    expect(s('a ((k1)) b', 4)).toEqual({ kind: 'block', id: 'k1' });
    expect(s('#życie x', 2)).toEqual({ kind: 'tag', tag: 'życie' });
    expect(s('[strona](https://example.com)', 2)).toEqual({ kind: 'url', url: 'https://example.com' });
    expect(s('[notatka](Inne%20miejsce.md)', 2)).toEqual({ kind: 'page', target: 'Inne miejsce.md' });
    expect(s('zwykły tekst', 3)).toBeNull();
  });
  it('relative links are pages also when the cursor is on their URL part', () => {
    const s = (doc: string, pos: number) => linkAt(view(doc, pos).state, pos);
    expect(s('[notatka](Inne%20miejsce.md)', 14)).toEqual({ kind: 'page', target: 'Inne miejsce.md' });
    expect(s('[strona](https://example.com)', 15)).toEqual({ kind: 'url', url: 'https://example.com' });
    expect(s('zob. https://example.com/a teraz', 10)).toEqual({ kind: 'url', url: 'https://example.com/a' });
  });
  it('malformed percent-encoding does not throw', () => {
    expect(linkAt(view('[zepsuty](plik%E0%A4%A.md)', 2).state, 2)).toEqual({ kind: 'page', target: 'plik%E0%A4%A.md' });
  });
  it('opens them through the editor context', () => {
    expect(openLinkAtCursor(view('[[Dom]]', 3))).toBe(true);
    expect(opened).toEqual(['Dom']);
    expect(openLinkAtCursor(view('((k1))', 3))).toBe(true);
    expect(routes).toEqual([{ view: 'page', path: 'Dom.md', blockId: 'k1', line: 0 }]);
    expect(openLinkAtCursor(view('#praca', 2))).toBe(true);
    expect(routes.at(-1)).toEqual({ view: 'tags', tag: 'praca' });
    expect(openLinkAtCursor(view('nic', 1))).toBe(false);
  });
});
