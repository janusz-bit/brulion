/**
 * Decoration computation without a DOM: which syntax gets hidden / replaced for a given
 * selection (EditorState + buildDecorations, node environment).
 */
import { ensureSyntaxTree } from '@codemirror/language';
import { EditorSelection, EditorState, type Extension, type Range } from '@codemirror/state';
import type { Decoration } from '@codemirror/view';
import { describe, expect, it } from 'vitest';
import { createTranslator } from '@/core/i18n';
import { contextFor, editorContext } from '../context';
import { FakeWorkspace } from '../fake-workspace';
import { brulionLanguage } from '../setup';
import { blockExtension } from './blocks';
import { buildDecorations, strikeEnd, wikiDisplay } from './inline';
import { previewState, setPreviewFocus } from './state';

interface Deco {
  from: number;
  to: number;
  text: string;
  kind: string;
}

function state(doc: string, cursor: number | null, extra: Extension = []): EditorState {
  let s = EditorState.create({
    doc,
    selection: EditorSelection.cursor(cursor ?? 0),
    extensions: [brulionLanguage(), previewState(), blockExtension(), extra],
  });
  if (cursor !== null) s = s.update({ effects: setPreviewFocus.of(true) }).state;
  ensureSyntaxTree(s, s.doc.length, 5000);
  return s;
}

function decos(s: EditorState): Deco[] {
  const { decorations } = buildDecorations(s, [{ from: 0, to: s.doc.length }]);
  return decorations
    .map((r: Range<Decoration>) => {
      const spec = r.value.spec as { widget?: { constructor: { name: string } }; class?: string };
      const kind = spec.widget ? spec.widget.constructor.name : r.from === r.to && !spec.widget ? `line:${spec.class}` : `mark:${spec.class}`;
      return { from: r.from, to: r.to, text: s.sliceDoc(r.from, r.to), kind };
    })
    .sort((a, b) => a.from - b.from || a.to - b.to);
}

const hiddenTexts = (s: EditorState) => decos(s).filter((d) => d.kind === 'HiddenWidget').map((d) => d.text);
const widgets = (s: EditorState, name: string) => decos(s).filter((d) => d.kind === name);

describe('hiding Markdown syntax', () => {
  const DOC = 'Tekst **gruby** i *kursywa* ~~skreśl~~ ==ważne== `kod`\ndruga linia';

  it('hides all inline marks when the cursor is elsewhere', () => {
    const s = state(DOC, DOC.length);
    expect(hiddenTexts(s)).toEqual(['**', '**', '*', '*', '~~', '~~', '==', '==', '`', '`']);
  });

  it('reveals the marks of the element under the cursor only', () => {
    const s = state(DOC, DOC.indexOf('gruby') + 2);
    expect(hiddenTexts(s)).toEqual(['*', '*', '~~', '~~', '==', '==', '`', '`']);
  });

  it('cursor right after an element reveals it (Obsidian)', () => {
    const s = state(DOC, DOC.indexOf('**', 8) + 2);
    expect(hiddenTexts(s)).not.toContain('**');
  });

  it('an unfocused editor reveals nothing', () => {
    const s = state(DOC, null);
    expect(hiddenTexts(s)).toHaveLength(10);
  });

  it('heading marks hide unless the cursor is on the heading line', () => {
    const doc = '## Plan prac\ntekst';
    expect(hiddenTexts(state(doc, doc.length))).toEqual(['## ']);
    expect(hiddenTexts(state(doc, 5))).toEqual([]);
    expect(decos(state(doc, doc.length)).some((d) => d.kind === 'line:b-lp-h b-lp-h2')).toBe(true);
  });

  it('quote marks and block ids hide off their line', () => {
    const doc = '> cytat\n> dalej ^abc\n\nkoniec';
    expect(hiddenTexts(state(doc, doc.length))).toEqual(['> ', '> ', ' ^abc']);
    expect(hiddenTexts(state(doc, 9))).toEqual(['> ']);
  });

  it('links show their text; wiki links their alias or title', () => {
    const doc = 'Zob. [stronę](https://example.com) i [[Projekty/Ogród|ogród]] oraz [[Projekty/Dom]] i [[Dom]].';
    const s = state(doc, 0);
    expect(hiddenTexts(s)).toEqual(['[', '](https://example.com)', '[[Projekty/Ogród|', ']]', '[[', ']]']);
    expect(widgets(s, 'LinkWidget').map((d) => d.text)).toEqual(['[[Projekty/Dom]]']);
    expect(decos(s).filter((d) => d.kind.startsWith('mark:b-lp-link')).map((d) => d.text)).toEqual(['stronę', 'ogród', 'Dom']);
  });
});

describe('outline and tasks', () => {
  const DOC = '- [ ] zadanie 📅 2026-10-15\n\t- dziecko\n1. numer';

  it('replaces markers, indentation and checkboxes', () => {
    const s = state(DOC, null);
    expect(widgets(s, 'BulletWidget').map((d) => d.text)).toEqual(['- ', '- ']);
    expect(widgets(s, 'CheckboxWidget').map((d) => d.text)).toEqual(['[ ] ']);
    expect(widgets(s, 'OrderedWidget').map((d) => d.text)).toEqual(['1. ']);
    expect(hiddenTexts(s)).toEqual(['\t']);
    expect(widgets(s, 'ChipWidget').map((d) => d.text)).toEqual(['📅 2026-10-15']);
    expect(decos(s).filter((d) => d.kind.startsWith('line:b-lp-li')).map((d) => d.kind)).toEqual([
      'line:b-lp-li b-lp-li--task is-todo has-children',
      'line:b-lp-li',
      'line:b-lp-li b-lp-li--ordered',
    ]);
  });

  it('typing inside a task keeps bullet and checkbox rendered', () => {
    const s = state(DOC, DOC.indexOf('zadanie') + 3);
    expect(widgets(s, 'BulletWidget')).toHaveLength(2);
    expect(widgets(s, 'CheckboxWidget')).toHaveLength(1);
  });

  it('the cursor at the marker reveals the raw marker (Home)', () => {
    const s = state(DOC, 0);
    expect(widgets(s, 'BulletWidget')).toHaveLength(1);
    expect(decos(s).some((d) => d.kind === 'mark:b-lp-marker-raw' && d.text === '- ')).toBe(true);
  });

  it('a token chip shows its source while the cursor is in it', () => {
    const s = state(DOC, DOC.indexOf('2026') + 2);
    expect(widgets(s, 'ChipWidget')).toHaveLength(0);
  });

  it('done tasks get a strike-through up to their tokens', () => {
    const doc = '- [x] kupić mleko ✅ 2026-10-14';
    const strike = decos(state(doc, null)).find((d) => d.kind === 'mark:b-lp-task-text');
    expect(strike?.text).toBe('kupić mleko');
  });

  it('Logseq keywords become checkboxes', () => {
    const s = state('- DOING pisać\n- TODO dzwonić', null);
    expect(widgets(s, 'CheckboxWidget').map((d) => d.text)).toEqual(['DOING ', 'TODO ']);
  });
});

describe('vault-aware widgets', () => {
  const ws = new FakeWorkspace({ files: { 'a.md': '', 'Dom.md': '- ściana ^sc\n' } });
  const ctx = contextFor({ workspace: ws, t: createTranslator('pl'), locale: 'pl', now: () => new Date(2026, 9, 14), navigate: () => {}, openLink: () => {} }, 'a.md');

  it('marks unresolved wiki links and resolves block refs', () => {
    const s = state('[[Dom]] [[Nie ma]] ((sc)) ((zz))', null, [editorContext.of(ctx)]);
    const links = decos(s).filter((d) => d.kind.startsWith('mark:b-lp-link'));
    expect(links.map((d) => [d.text, d.kind])).toEqual([
      ['Dom', 'mark:b-lp-link b-lp-wikilink'],
      ['Nie ma', 'mark:b-lp-link b-lp-wikilink is-missing'],
    ]);
    expect(widgets(s, 'BlockRefWidget').map((d) => d.text)).toEqual(['((sc))', '((zz))']);
  });
});

describe('helpers', () => {
  it('wikiDisplay', () => {
    expect(wikiDisplay('Projekty/Remont kuchni')).toBe('Remont kuchni');
    expect(wikiDisplay('Dom#Kuchnia')).toBe('Dom › Kuchnia');
    expect(wikiDisplay('Dom#^abc')).toBe('Dom › ^abc');
    expect(wikiDisplay('#Sekcja')).toBe('Sekcja');
    expect(wikiDisplay('Notatka.md')).toBe('Notatka');
  });

  it('strikeEnd', () => {
    expect(strikeEnd('- [x] mleko ✅ 2026-10-14', 6)).toBe(11);
    expect(strikeEnd('- [x] mleko ^abc', 6)).toBe(11);
    expect(strikeEnd('- [x] mleko  ', 6)).toBe(11);
  });
});
