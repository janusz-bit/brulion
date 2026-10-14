import { CompletionContext, type CompletionResult, type CompletionSource } from '@codemirror/autocomplete';
import { EditorSelection, EditorState, type TransactionSpec } from '@codemirror/state';
import type { EditorView } from '@codemirror/view';
import { describe, expect, it } from 'vitest';
import { createTranslator } from '@/core/i18n';
import {
  blockRefQueryAt,
  blockRefSource,
  dateCompletions,
  dateSource,
  linkTargetFor,
  tagQueryAt,
  tagSource,
  wikiLinkSource,
  wikiQueryAt,
  withCurrentLines,
} from './autocomplete';
import { contextFor, editorContext, type EditorContext } from './context';
import { FakeWorkspace } from './fake-workspace';
import { brulionLanguage } from './setup';

const NOW = new Date(2026, 9, 14, 10, 30); // Wednesday

const ws = new FakeWorkspace({
  files: {
    'Projekty/Dom.md': '---\naliases: [Remont]\n---\n# Dom\n\n## Kuchnia\n- [ ] kupić farbę #remont\n- zadzwonić do hydraulika ^hydr\n',
    'Projekty/Ogród.md': '# Ogród\n- posadzić tulipany #ogród #remont\n',
    'Praca/Dom.md': '# Inny dom\n',
    'journals/2026-10-14.md': '- spotkanie #praca\n',
  },
});

function ctxFor(path: string): EditorContext {
  return contextFor(
    {
      workspace: ws,
      t: createTranslator('pl'),
      locale: 'pl',
      now: () => NOW,
      navigate: () => {},
      openLink: () => {},
    },
    path,
  );
}

function stateAt(input: string, path = 'journals/2026-10-14.md'): EditorState {
  const pos = input.indexOf('|');
  const doc = input.slice(0, pos) + input.slice(pos + 1);
  return EditorState.create({
    doc,
    selection: EditorSelection.cursor(pos),
    extensions: [brulionLanguage(), editorContext.of(ctxFor(path))],
  });
}

async function complete(source: CompletionSource, input: string, explicit = false): Promise<CompletionResult | null> {
  const state = stateAt(input);
  return source(new CompletionContext(state, state.selection.main.head, explicit));
}

describe('query detection', () => {
  it('wiki links', () => {
    expect(wikiQueryAt('see [[Pro')).toEqual({ embed: false, kind: 'page', page: '', query: 'Pro', offset: 6 });
    expect(wikiQueryAt('![[pho')).toMatchObject({ embed: true, kind: 'page', query: 'pho' });
    expect(wikiQueryAt('[[Dom#Kuch')).toMatchObject({ kind: 'heading', page: 'Dom', query: 'Kuch', offset: 6 });
    expect(wikiQueryAt('[[Dom#^hy')).toMatchObject({ kind: 'block', page: 'Dom', query: 'hy', offset: 7 });
    expect(wikiQueryAt('[[#')).toMatchObject({ kind: 'heading', page: '', query: '' });
    expect(wikiQueryAt('[[Dom|ali')).toBeNull();
    expect(wikiQueryAt('[[Dom]] i')).toBeNull();
  });
  it('block refs', () => {
    expect(blockRefQueryAt('x ((hydr')).toEqual({ query: 'hydr', offset: 4 });
    expect(blockRefQueryAt('!((')).toEqual({ query: '', offset: 3 });
    expect(blockRefQueryAt('((a)) b')).toBeNull();
  });
  it('tags', () => {
    expect(tagQueryAt('tekst #rem')).toEqual({ query: 'rem', offset: 7 });
    expect(tagQueryAt('#życ')).toEqual({ query: 'życ', offset: 1 });
    expect(tagQueryAt('(#a')).toEqual({ query: 'a', offset: 2 });
    expect(tagQueryAt('a#b')).toBeNull();
  });
});

describe('[[ page completion', () => {
  it('suggests pages with folders, alias options and "create page"', async () => {
    const res = await complete(wikiLinkSource, '- [[Re|');
    expect(res).not.toBeNull();
    const labels = res!.options.map((o) => o.label);
    expect(labels).toContain('Remont');
    const alias = res!.options.find((o) => o.label === 'Remont')!;
    expect(alias.detail).toMatch(/Dom/);
    const create = res!.options.find((o) => o.type === 'create')!;
    expect(create.label).toBe('Re');
    expect(create.displayLabel).toBe('Utwórz stronę „Re”');
  });
  it('folder shown as detail, ambiguous titles link by path', async () => {
    const res = await complete(wikiLinkSource, '[[Ogr|');
    const ogrod = res!.options.find((o) => o.label === 'Ogród')!;
    expect(ogrod.detail).toBe('Projekty');
    expect(linkTargetFor(ctxFor('x.md'), 'Projekty/Ogród.md', 'Ogród')).toBe('Ogród');
    // "Dom" resolves to the shortest path (Praca/Dom.md), so the other one links by path.
    expect(linkTargetFor(ctxFor('x.md'), 'Praca/Dom.md', 'Dom')).toBe('Dom');
    expect(linkTargetFor(ctxFor('x.md'), 'Projekty/Dom.md', 'Dom')).toBe('Projekty/Dom');
  });
  it('headings of another page after #', async () => {
    const res = await complete(wikiLinkSource, '[[Projekty/Dom#ku|');
    expect(res!.options.map((o) => o.label)).toEqual(['Kuchnia']);
  });
  it('blocks after #^', async () => {
    const res = await complete(wikiLinkSource, '[[Projekty/Dom#^hydr|');
    expect(res!.options[0]!.label).toBe('zadzwonić do hydraulika');
    expect(res!.options[0]!.detail).toBe('^hydr');
  });
  it('nothing inside code', async () => {
    expect(await complete(wikiLinkSource, '`[[Pro|`')).toBeNull();
  });
});

/** Apply a completion option like CodeMirror does (string or function apply). */
function accept(input: string, res: CompletionResult, label: string): string {
  let state = stateAt(input);
  const option = res.options.find((o) => o.label === label)!;
  const to = res.to ?? state.selection.main.head;
  const view = {
    get state() {
      return state;
    },
    dispatch(spec: TransactionSpec) {
      state = state.update(spec).state;
    },
  } as unknown as EditorView;
  if (typeof option.apply === 'function') option.apply(view, option, res.from, to);
  else {
    const insert = option.apply ?? option.label;
    state = state.update({ changes: { from: res.from, to, insert }, selection: { anchor: res.from + insert.length } }).state;
  }
  const doc = state.doc.toString();
  const head = state.selection.main.head;
  return doc.slice(0, head) + '|' + doc.slice(head);
}

describe('completing inside existing links and tags', () => {
  it('replaces the rest of the link target', async () => {
    const input = 'zob. [[Ogr|xx]] dalej';
    const res = (await complete(wikiLinkSource, input))!;
    expect(accept(input, res, 'Ogród')).toBe('zob. [[Ogród]]| dalej');
  });
  it('keeps an existing alias', async () => {
    const input = 'zob. [[Ogr|xx|ogródek]] dalej';
    const res = (await complete(wikiLinkSource, input))!;
    // (the first `|` is the cursor, the second one the alias separator)
    expect(accept(input, res, 'Ogród')).toBe('zob. [[Ogród||ogródek]] dalej');
  });
  it('still consumes auto-paired brackets when typing a new link', async () => {
    const input = '- [[Ogr|]]';
    const res = (await complete(wikiLinkSource, input))!;
    expect(accept(input, res, 'Ogród')).toBe('- [[Ogród]]|');
  });
  it('replaces the rest of a tag', async () => {
    const input = 'x #rem|ntx y';
    const res = (await complete(tagSource, input))!;
    expect(accept(input, res, 'remont')).toBe('x #remont| y');
  });
});

describe('(( block completion', () => {
  it('finds blocks across the vault', async () => {
    const res = await complete(blockRefSource, '((tulip|');
    expect(res!.options.map((o) => o.label)).toEqual(['posadzić tulipany #ogród #remont']);
    expect(res!.options[0]!.detail).toBe('Ogród');
  });
});

describe('block references to the note being edited', () => {
  it('use the editor text, not the saved one, to find the block line', () => {
    const path = 'Projekty/Dom.md';
    const saved = ws.index.content(path)!;
    // Unsaved edit: a new line at the top shifts every block by one line.
    const state = EditorState.create({ doc: `nowa linia\n${saved}`, extensions: [brulionLanguage(), editorContext.of(ctxFor(path))] });
    const refs = ws.index.suggestBlocks('hydraulika');
    expect(refs).toHaveLength(1);
    const fresh = withCurrentLines(state, ctxFor(path), refs);
    expect(fresh).toHaveLength(1);
    expect(fresh[0]!.block.line).toBe(refs[0]!.block.line + 1);
    expect(state.doc.line(fresh[0]!.block.line + 1).text).toBe('- zadzwonić do hydraulika ^hydr');
    // Blocks of other notes are left alone; blocks deleted in the editor are dropped.
    expect(withCurrentLines(state, ctxFor('x.md'), refs)).toEqual(refs);
    const gone = EditorState.create({ doc: '# Dom\n', extensions: [brulionLanguage()] });
    expect(withCurrentLines(gone, ctxFor(path), refs)).toEqual([]);
  });
});

describe('# tag completion', () => {
  it('suggests existing tags by count', async () => {
    const res = await complete(tagSource, 'x #re|');
    expect(res!.options[0]!.label).toBe('remont');
    expect(res!.options[0]!.detail).toBe('2 użycia');
  });
  it('waits for a character unless explicit', async () => {
    expect(await complete(tagSource, 'x #|')).toBeNull();
    expect((await complete(tagSource, 'x #|', true))!.options.length).toBeGreaterThan(1);
  });
});

describe('date completions', () => {
  it('maps natural phrases after token emoji to canonical dates', () => {
    expect(dateCompletions('- [ ] zadanie 📅 jutro', NOW)).toEqual({
      fromOffset: 17,
      options: [{ label: '2026-10-15', insert: '2026-10-15', kind: 'date', human: 'jutro' }],
    });
    const r = dateCompletions('leki ⏰ jutro o 9', NOW)!;
    expect(r.options[0]!.insert).toBe('2026-10-15 09:00');
    expect(dateCompletions('⏳ w piątek', NOW)!.options[0]!.insert).toBe('2026-10-16');
    expect(dateCompletions('🛫 15.11', NOW)!.options[0]!.insert).toBe('2026-11-15');
    expect(dateCompletions('📅 next friday', NOW, { locale: 'en' })!.options[0]!.insert).toBe('2026-10-23');
  });
  it('needs the whole phrase recognised', () => {
    expect(dateCompletions('📅 jut', NOW)).toBeNull();
    expect(dateCompletions('📅 jutro coś', NOW)).toBeNull();
    expect(dateCompletions('📅 2026-10-15', NOW)).toBeNull();
    expect(dateCompletions('bez tokenu jutro', NOW)).toBeNull();
  });
  it('@ offers a journal link, the date and a reminder', () => {
    const r = dateCompletions('spotkanie @jutro o 18', NOW, { journalTitle: (d) => d })!;
    expect(r.fromOffset).toBe(10);
    expect(r.options.map((o) => o.insert)).toEqual(['[[2026-10-15]]', '2026-10-15 18:00', '⏰ 2026-10-15 18:00']);
    expect(dateCompletions('mail@jutro', NOW)).toBeNull();
  });
  it('as a completion source', async () => {
    const res = await complete(dateSource, '- [ ] a 📅 pojutrze|');
    expect(res!.options[0]!.label).toBe('2026-10-16');
    expect(res!.from).toBe(11);
  });
});
