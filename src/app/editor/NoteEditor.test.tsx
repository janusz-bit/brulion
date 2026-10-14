// @vitest-environment jsdom
import './testutil';
import { EditorSelection } from '@codemirror/state';
import { EditorView } from '@codemirror/view';
import { act, StrictMode, type ReactNode } from 'react';
import { afterEach, describe, expect, it } from 'vitest';
import { cleanup, click, render } from '@/app/components/render.testutil';
import { createTranslator } from '@/core/i18n';
import { editorContext, EditorHostProvider, type EditorHost, type EditorRoute, type EditorWorkspace } from './context';
import { FakeWorkspace } from './fake-workspace';
import { getFocusedEditor } from './focus';
import { NoteEditor } from './NoteEditor';
import { EditorSession, lineOfBlockId, type SessionCallbacks } from './session';
import { editorExtensions } from './setup';
import { zoomField } from './zoom';

const NOW = new Date(2026, 9, 14, 10, 30);
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

function hostFor(workspace: FakeWorkspace, routes: EditorRoute[] = []): EditorHost {
  return {
    workspace,
    t: createTranslator('pl'),
    locale: 'pl',
    now: () => NOW,
    navigate: (r) => routes.push(r),
    openLink: () => {},
  };
}

function wrap(host: EditorHost, node: ReactNode, strict = false): ReactNode {
  const inner = <EditorHostProvider value={host}>{node}</EditorHostProvider>;
  return strict ? <StrictMode>{inner}</StrictMode> : inner;
}

/** Let the async load (and React updates) finish. */
async function settle(ms = 20): Promise<void> {
  await act(async () => {
    await sleep(ms);
  });
}

function viewIn(container: HTMLElement): EditorView {
  const el = container.querySelector('.cm-editor');
  if (!el) throw new Error('no editor');
  const view = EditorView.findFromDOM(el as HTMLElement);
  if (!view) throw new Error('no view');
  return view;
}

function type(view: EditorView, text: string, at = view.state.doc.length): void {
  act(() => {
    view.dispatch({ changes: { from: at, insert: text }, selection: EditorSelection.cursor(at + text.length), userEvent: 'input.type' });
  });
}

afterEach(() => cleanup());

describe('<NoteEditor>', () => {
  it('loads the note into one CodeMirror view (also under StrictMode)', async () => {
    const ws = new FakeWorkspace({ files: { 'Dom.md': '# Dom\n- kuchnia\n' } });
    const r = render(wrap(hostFor(ws), <NoteEditor path="Dom.md" />, true));
    await settle();
    expect(r.container.querySelectorAll('.cm-editor')).toHaveLength(1);
    expect(viewIn(r.container).state.doc.toString()).toBe('# Dom\n- kuchnia\n');
    expect(r.container.querySelector('.b-editor')?.getAttribute('data-status')).toBe('ready');
  });

  it('saves debounced edits and flushes on unmount', async () => {
    const ws = new FakeWorkspace({ files: { 'a.md': 'start' } });
    const r = render(wrap(hostFor(ws), <NoteEditor path="a.md" />));
    await settle();
    const view = viewIn(r.container);
    type(view, ' 1');
    await settle(100);
    expect(ws.writes).toHaveLength(0);
    await settle(450);
    expect(ws.writes.map((w) => w.content)).toEqual(['start 1']);
    type(view, ' 2');
    r.unmount();
    await sleep(20);
    expect(ws.writes.map((w) => w.content)).toEqual(['start 1', 'start 1 2']);
  });

  it('follows external changes with a minimal diff when there are no local edits', async () => {
    const ws = new FakeWorkspace({ files: { 'a.md': 'linia 1\nlinia 2\nlinia 3' } });
    const r = render(wrap(hostFor(ws), <NoteEditor path="a.md" />));
    await settle();
    const view = viewIn(r.container);
    act(() => view.dispatch({ selection: EditorSelection.cursor(4) }));
    ws.externalWrite('a.md', 'linia 1\nlinia 2 zmieniona\nlinia 3\nlinia 4');
    await settle();
    expect(view.state.doc.toString()).toBe('linia 1\nlinia 2 zmieniona\nlinia 3\nlinia 4');
    expect(view.state.selection.main.head).toBe(4);
    await settle(450);
    expect(ws.writes).toHaveLength(0);
  });

  it('3-way merges external changes into unsaved edits', async () => {
    const ws = new FakeWorkspace({ files: { 'a.md': 'a\nb\nc\n' } });
    const r = render(wrap(hostFor(ws), <NoteEditor path="a.md" />));
    await settle();
    const view = viewIn(r.container);
    type(view, 'A', 1); // local: "aA\nb\nc\n"
    ws.externalWrite('a.md', 'a\nb\nc zdalnie\n');
    await settle();
    expect(view.state.doc.toString()).toBe('aA\nb\nc zdalnie\n');
    expect(r.container.querySelector('.b-editor__notice')?.textContent).toMatch(/scalone/);
    await settle(450);
    expect(ws.writes.at(-1)?.content).toBe('aA\nb\nc zdalnie\n');
  });

  it('keeps CRLF line endings', async () => {
    const ws = new FakeWorkspace({ files: { 'w.md': '- a\r\n- b\r\n' } });
    const r = render(wrap(hostFor(ws), <NoteEditor path="w.md" />));
    await settle();
    const view = viewIn(r.container);
    expect(view.state.doc.lines).toBe(3);
    type(view, '- c');
    r.unmount();
    await sleep(20);
    expect(ws.writes.at(-1)?.content).toBe('- a\r\n- b\r\n- c');
  });

  it('opens missing notes empty and creates them on the first edit only', async () => {
    const ws = new FakeWorkspace();
    const r = render(wrap(hostFor(ws), <NoteEditor path="journals/2026-10-14.md" placeholder="Zacznij pisać…" />));
    await settle();
    const view = viewIn(r.container);
    expect(view.state.doc.toString()).toBe('');
    expect(r.container.querySelector('.cm-placeholder')?.textContent).toBe('Zacznij pisać…');
    await settle(450);
    expect(ws.writes).toHaveLength(0);
    type(view, '- pierwszy wpis');
    await settle(450);
    expect(await ws.read('journals/2026-10-14.md')).toBe('- pierwszy wpis');
  });

  it('places the cursor on a block id target', async () => {
    const ws = new FakeWorkspace({ files: { 'b.md': '- a\n- b ^cel\n- c' } });
    const r = render(wrap(hostFor(ws), <NoteEditor path="b.md" blockId="cel" />));
    await settle();
    const view = viewIn(r.container);
    expect(view.state.doc.lineAt(view.state.selection.main.head).number).toBe(2);
  });

  it('zooms into a list item and zooms out from the breadcrumb', async () => {
    const ws = new FakeWorkspace({ files: { 'p.md': '- Projekt\n\t- Etap 1\n\t\t- zadanie\n- Inne' } });
    const zooms: (number | null)[] = [];
    const r = render(wrap(hostFor(ws), <NoteEditor path="p.md" zoomLine={1} onZoomChange={(l) => zooms.push(l)} />));
    await settle();
    const view = viewIn(r.container);
    const z = view.state.field(zoomField)!;
    expect(view.state.sliceDoc(z.from, z.to)).toBe('\t- Etap 1\n\t\t- zadanie');
    const crumbs = r.container.querySelector('.b-editor__crumbs')!;
    expect(crumbs.textContent).toBe('p›Projekt›Etap 1');
    click(crumbs.querySelector('button'));
    expect(view.state.field(zoomField)).toBeNull();
    expect(r.container.querySelector('.b-editor__crumbs')).toBeNull();
    expect(zooms.at(-1)).toBeNull();
  });

  it('read-only notes are not editable and never saved', async () => {
    const ws = new FakeWorkspace({ files: { 'r.md': 'x' } });
    const r = render(wrap(hostFor(ws), <NoteEditor path="r.md" readOnly />));
    await settle();
    const view = viewIn(r.container);
    expect(view.state.readOnly).toBe(true);
    expect(view.contentDOM.getAttribute('contenteditable')).toBe('false');
  });

  it('saves after read-only is switched off', async () => {
    const ws = new FakeWorkspace({ files: { 'r.md': 'x' } });
    const host = hostFor(ws);
    const r = render(wrap(host, <NoteEditor path="r.md" readOnly />));
    await settle();
    r.rerender(wrap(host, <NoteEditor path="r.md" />));
    await settle();
    const view = viewIn(r.container);
    expect(view.state.readOnly).toBe(false);
    type(view, 'y');
    await settle(450);
    expect(ws.writes.map((w) => w.content)).toEqual(['xy']);
  });

  it('switching notes keeps one view and saves the previous note', async () => {
    const ws = new FakeWorkspace({ files: { 'a.md': 'A', 'b.md': 'B' } });
    const host = hostFor(ws);
    const r = render(wrap(host, <NoteEditor path="a.md" />));
    await settle();
    type(viewIn(r.container), '1');
    r.rerender(wrap(host, <NoteEditor path="b.md" />));
    await settle();
    expect(r.container.querySelectorAll('.cm-editor')).toHaveLength(1);
    expect(viewIn(r.container).state.doc.toString()).toBe('B');
    expect(await ws.read('a.md')).toBe('A1');
  });

  it('registers the focused editor for the command palette', async () => {
    const ws = new FakeWorkspace({ files: { 'f.md': 'x' } });
    const r = render(wrap(hostFor(ws), <NoteEditor path="f.md" autoFocus />));
    await settle();
    const view = viewIn(r.container);
    act(() => view.focus());
    await settle();
    expect(getFocusedEditor()?.path).toBe('f.md');
    r.unmount();
    expect(getFocusedEditor()).toBeNull();
  });
});

describe('EditorSession', () => {
  it('ignores the echo of its own writes and serialises saves', async () => {
    const ws = new FakeWorkspace({ files: { 's.md': 'a' }, writeDelay: 30 });
    const parent = document.createElement('div');
    document.body.appendChild(parent);
    const s = new EditorSession({ path: 's.md', workspace: ws, parent, extensions: () => [], saveDelay: 10 });
    await s.start();
    s.view!.dispatch({ changes: { from: 1, insert: 'b' } });
    await sleep(15); // save of "ab" in flight
    s.view!.dispatch({ changes: { from: 2, insert: 'c' } });
    await s.flush();
    expect(ws.writes.map((w) => w.content)).toEqual(['ab', 'abc']);
    expect(s.view!.state.doc.toString()).toBe('abc');
    expect(s.dirty).toBe(false);
    await s.destroy();
  });

  function sessionFor(ws: FakeWorkspace, path: string, saveDelay = 10): EditorSession {
    const parent = document.createElement('div');
    document.body.appendChild(parent);
    return new EditorSession({ path, workspace: ws, parent, extensions: () => editorExtensions({}), saveDelay });
  }

  it('keeps the cursor when external edits land above and below it', async () => {
    const ws = new FakeWorkspace({ files: { 'a.md': 'l1\nl2\nl3\nl4\nl5' } });
    const s = sessionFor(ws, 'a.md');
    await s.start();
    const v = s.view!;
    v.dispatch({ selection: EditorSelection.cursor(v.state.doc.line(3).from + 1) });
    ws.externalWrite('a.md', 'l1 nowa\nl2\nl3\nl4\nl5 też');
    await sleep(20);
    expect(v.state.doc.toString()).toBe('l1 nowa\nl2\nl3\nl4\nl5 też');
    expect(v.state.selection.main.head).toBe(v.state.doc.line(3).from + 1);
    await s.destroy();
  });

  it('keeps the cursor when merging an external change into unsaved edits', async () => {
    const ws = new FakeWorkspace({ files: { 'a.md': 'l1\nl2\nl3\nl4\nl5' } });
    const s = sessionFor(ws, 'a.md', 1000);
    await s.start();
    const v = s.view!;
    const end3 = v.state.doc.line(3).to;
    v.dispatch({ changes: { from: end3, insert: 'Q' }, selection: EditorSelection.cursor(end3 + 1) });
    ws.externalWrite('a.md', 'l1 x\nl2\nl3\nl4\nl5 y');
    await sleep(20);
    expect(v.state.doc.toString()).toBe('l1 x\nl2\nl3Q\nl4\nl5 y');
    expect(v.state.selection.main.head).toBe(v.state.doc.line(3).to);
    await s.destroy();
    expect(await ws.read('a.md')).toBe('l1 x\nl2\nl3Q\nl4\nl5 y');
  });

  it('a note re-opened while its last save is still being written loads the saved text', async () => {
    const ws = new FakeWorkspace({ files: { 'a.md': 'a' }, writeDelay: 30 });
    const s = sessionFor(ws, 'a.md', 1000);
    await s.start();
    s.view!.dispatch({ changes: { from: 1, insert: 'B' } });
    void s.destroy();
    const s2 = sessionFor(ws, 'a.md', 1000);
    await s2.start();
    expect(s2.text()).toBe('aB');
    await s2.destroy();
    expect(ws.writes.map((w) => w.content)).toEqual(['aB']);
  });

  it('follows a rename: saves to the new path and links resolve from it', async () => {
    const ws = new FakeWorkspace({ files: { 'stara/Notatka.md': 'x' } });
    const host = hostFor(ws);
    const r = render(wrap(host, <NoteEditor path="stara/Notatka.md" />));
    await settle();
    const view = viewIn(r.container);
    await act(async () => {
      await ws.externalRename('stara/Notatka.md', 'nowa/Notatka.md');
    });
    expect(view.state.facet(editorContext)?.path).toBe('nowa/Notatka.md');
    // An unfocused editor does not become "the focused editor" by being renamed.
    expect(getFocusedEditor()).toBeNull();
    type(view, 'y');
    await settle(450);
    expect(ws.writes.map((w) => [w.path, w.content])).toEqual([['nowa/Notatka.md', 'xy']]);
    r.unmount();
  });

  it('removes its listeners when destroyed', async () => {
    const ws = new FakeWorkspace({ files: { 'a.md': 'a' } });
    const s = sessionFor(ws, 'a.md');
    await s.start();
    expect(ws.listenerCount).toBe(1);
    await s.destroy();
    expect(ws.listenerCount).toBe(0);
    // Destroyed while still loading (StrictMode's first mount).
    const s2 = sessionFor(ws, 'a.md');
    const started = s2.start();
    await s2.destroy();
    await started;
    expect(ws.listenerCount).toBe(0);
    expect(s2.view).toBeNull();
  });

  it('finds block ids', () => {
    expect(lineOfBlockId('a\n- b ^x1\nc', 'x1')).toBe(1);
    expect(lineOfBlockId('```\ncode\n```\n^kod', 'kod')).toBe(2);
    expect(lineOfBlockId('- task\n  id:: 64f-1\n', '64f-1')).toBe(0);
    expect(lineOfBlockId('a', 'nope')).toBeNull();
  });
});

describe('EditorSession saving and external changes (review regressions)', () => {
  function open(ws: EditorWorkspace, path: string, saveDelay = 10, callbacks: SessionCallbacks = {}): EditorSession {
    const parent = document.createElement('div');
    document.body.appendChild(parent);
    return new EditorSession({ path, workspace: ws, parent, extensions: () => editorExtensions({}), saveDelay, callbacks });
  }
  const typeAt = (s: EditorSession, at: number, text: string) => s.view!.dispatch({ changes: { from: at, insert: text }, userEvent: 'input.type' });

  /** The same workspace without `modify` (plain read + write saves). */
  function withoutModify(ws: FakeWorkspace): EditorWorkspace {
    return {
      config: ws.config,
      index: ws.index,
      platform: ws.platform,
      read: (p) => ws.read(p),
      write: (p, c) => ws.write(p, c),
      onFileChanged: (cb) => ws.onFileChanged(cb),
      assetUrl: (p) => ws.assetUrl(p),
      saveAttachment: (n, d) => ws.saveAttachment(n, d),
      ensureBlockId: (p, l) => ws.ensureBlockId(p, l),
    };
  }

  it('switching to read-only saves the edits typed before', async () => {
    const ws = new FakeWorkspace({ files: { 'a.md': 'a' } });
    const s = open(ws, 'a.md', 1000);
    await s.start();
    typeAt(s, 1, 'b');
    s.setReadOnly(true);
    await s.destroy();
    expect(await ws.read('a.md')).toBe('ab');
  });

  it('two editors of one note: a queued save merges instead of overwriting the other editor', async () => {
    for (const plain of [false, true]) {
      const ws = new FakeWorkspace({ files: { 'a.md': 'l1\nl2\nl3\nl4' }, writeDelay: 5 });
      const wsFor = () => (plain ? withoutModify(ws) : ws);
      const a = open(wsFor(), 'a.md', 20);
      const b = open(wsFor(), 'a.md', 20);
      await a.start();
      await b.start();
      typeAt(a, 2, 'A');
      typeAt(b, b.view!.state.doc.length, 'B');
      // Editor a never loses its own edit while the saves cross (it used to flash back).
      let lost = false;
      for (let i = 0; i < 30; i++) {
        await sleep(10);
        if (!a.text()!.includes('l1A')) lost = true;
      }
      expect(lost, plain ? 'without modify' : 'with modify').toBe(false);
      const writes = ws.writes.length;
      await sleep(100);
      expect(ws.writes.length, 'no save loop').toBe(writes);
      expect(await ws.read('a.md')).toBe('l1A\nl2\nl3\nl4B');
      expect(a.text()).toBe('l1A\nl2\nl3\nl4B');
      expect(b.text()).toBe('l1A\nl2\nl3\nl4B');
      await a.destroy();
      await b.destroy();
    }
  });

  it('a slow re-read of its own older save never reverts newer typing', async () => {
    class SlowReads extends FakeWorkspace {
      delay = 0;
      override async read(path: string): Promise<string> {
        const text = await super.read(path);
        if (this.delay) await sleep(this.delay);
        return text;
      }
    }
    const ws = new SlowReads({ files: { 'a.md': 'a' } });
    const s = open(ws, 'a.md', 1000);
    await s.start();
    ws.delay = 40;
    typeAt(s, 1, 'b');
    void s.flush(); // its echo starts a slow read of "ab"
    await sleep(15);
    typeAt(s, 2, 'c');
    void s.flush();
    for (let i = 0; i < 10; i++) {
      await sleep(10);
      expect(s.text()).toBe('abc');
    }
    await s.destroy();
    expect(await ws.read('a.md')).toBe('abc');
  });

  it('a change landing while the note loads is picked up', async () => {
    class SlowFirstRead extends FakeWorkspace {
      first = true;
      override async read(path: string): Promise<string> {
        const text = await super.read(path);
        if (this.first) {
          this.first = false;
          await sleep(30);
        }
        return text;
      }
    }
    const ws = new SlowFirstRead({ files: { 'a.md': 'stare' } });
    const s = open(ws, 'a.md');
    const started = s.start();
    await sleep(5);
    ws.externalWrite('a.md', 'nowe z synchronizacji');
    await started;
    await sleep(20);
    expect(s.text()).toBe('nowe z synchronizacji');
    await s.destroy();
  });

  it('a save queued behind a slow one writes the newest text, and nothing unchanged is written', async () => {
    const ws = new FakeWorkspace({ files: { 'a.md': 'x' }, writeDelay: 30 });
    const s = open(ws, 'a.md', 1000);
    await s.start();
    typeAt(s, 1, '1');
    void s.flush();
    typeAt(s, 2, '2');
    void s.flush();
    typeAt(s, 3, '3');
    await s.flush();
    await s.flush();
    expect(ws.writes.map((w) => w.content)).toEqual(['x1', 'x123']);
    await s.destroy();
    expect(ws.writes).toHaveLength(2);
  });

  it('an in-app modify of the note (quick capture, task toggle) merges with unsaved typing', async () => {
    const ws = new FakeWorkspace({ files: { 'j.md': '- rano\n' } });
    const merged: boolean[] = [];
    const s = open(ws, 'j.md', 1000, { onMerged: (c) => merged.push(c) });
    await s.start();
    typeAt(s, 6, ' kawa');
    await ws.modify('j.md', (c) => `${c}- [ ] kupić mleko\n`);
    await sleep(20);
    expect(s.text()).toBe('- rano kawa\n- [ ] kupić mleko\n');
    await s.destroy();
    expect(await ws.read('j.md')).toBe('- rano kawa\n- [ ] kupić mleko\n');
    expect(merged).toEqual([true]);
  });
});
