// @vitest-environment jsdom
import './testutil';
import { CompletionContext } from '@codemirror/autocomplete';
import { history, undo } from '@codemirror/commands';
import { EditorSelection, EditorState } from '@codemirror/state';
import { EditorView } from '@codemirror/view';
import { afterEach, describe, expect, it } from 'vitest';
import { createTranslator } from '@/core/i18n';
import { contextFor, editorContext, editorDialogs, type EditorDialogRequest } from './context';
import { DialogController } from './dialogs';
import { FakeWorkspace } from './fake-workspace';
import { brulionLanguage } from './setup';
import { filterSlashCommands, lineContextText, SLASH_COMMANDS, slashCompletionSource, slashQueryAt } from './slash';
import { rangeTracker } from './tracker';

const NOW = new Date(2026, 9, 14, 10, 30);
const ids = (q: string, locale: 'pl' | 'en' = 'pl') => filterSlashCommands(q, locale).map((c) => c.id);

describe('slash filtering', () => {
  it('lists everything for an empty query', () => {
    expect(ids('')).toHaveLength(SLASH_COMMANDS.length);
  });
  it('matches Polish names without diacritics', () => {
    expect(ids('prz')[0]).toBe('reminder');
    expect(ids('zad')).toContain('task');
    expect(ids('dzis')[0]).toBe('today');
    expect(ids('naglowek 2')[0]).toBe('h2');
    expect(ids('obl')[0]).toBe('calc');
  });
  it('matches English names and keywords in Polish UI', () => {
    expect(ids('remind')[0]).toBe('reminder');
    expect(ids('table')[0]).toBe('table');
    expect(ids('todo')).toContain('task');
  });
  it('prefers the UI language label', () => {
    expect(ids('ta', 'en')[0]).toBe('task');
    expect(ids('ta', 'pl')[0]).toBe('table');
  });
  it('returns nothing for nonsense', () => {
    expect(ids('qqqxz')).toEqual([]);
  });
  it('detects the trigger only at a line start or after whitespace', () => {
    expect(slashQueryAt('/prz')).toEqual({ offset: 0, query: 'prz' });
    expect(slashQueryAt('- zadanie /ter')).toEqual({ offset: 10, query: 'ter' });
    expect(slashQueryAt('http://x')).toBeNull();
    expect(slashQueryAt('a/b')).toBeNull();
    expect(slashQueryAt('/a b')).toBeNull();
  });
});

const ws = new FakeWorkspace({ files: { 'templates/spotkanie.md': '## {{title}}\n- {{cursor}}\n' } });
let views: EditorView[] = [];
afterEach(() => {
  for (const v of views) v.destroy();
  views = [];
});

function viewOf(input: string, dialogs?: (r: EditorDialogRequest) => void): EditorView {
  const pos = input.indexOf('|');
  const ctx = contextFor(
    { workspace: ws, t: createTranslator('pl'), locale: 'pl', now: () => NOW, navigate: () => {}, openLink: () => {} },
    'journals/2026-10-14.md',
  );
  const view = new EditorView({
    state: EditorState.create({
      doc: input.slice(0, pos) + input.slice(pos + 1),
      selection: EditorSelection.cursor(pos),
      extensions: [brulionLanguage(), editorContext.of(ctx), rangeTracker, dialogs ? editorDialogs.of(dialogs) : []],
    }),
    parent: document.body,
  });
  views.push(view);
  return view;
}

function show(view: EditorView): string {
  const s = view.state;
  const { anchor, head } = s.selection.main;
  const doc = s.doc.toString();
  if (anchor === head) return doc.slice(0, head) + '|' + doc.slice(head);
  const [a, b] = [Math.min(anchor, head), Math.max(anchor, head)];
  return doc.slice(0, a) + '«' + doc.slice(a, b) + '»' + doc.slice(b);
}

/** Type `input` (with `/query|`), run the command `id` on the trigger. */
function runCommand(input: string, id: string, dialogs?: (r: EditorDialogRequest) => void): EditorView {
  const view = viewOf(input, dialogs);
  const head = view.state.selection.main.head;
  const line = view.state.doc.lineAt(head);
  const hit = slashQueryAt(line.text.slice(0, head - line.from))!;
  SLASH_COMMANDS.find((c) => c.id === id)!.run(view, line.from + hit.offset, head);
  return view;
}

describe('slash commands', () => {
  it('Zadanie turns the line into a task', () => {
    expect(show(runCommand('/zad|', 'task'))).toBe('- [ ] |');
    expect(show(runCommand('- kupić mleko /zad|', 'task'))).toBe('- [ ] kupić mleko |');
    expect(show(runCommand('\t- x /zad|', 'task'))).toBe('\t- [ ] x |');
  });
  it('Dziś / Jutro insert journal links', () => {
    expect(show(runCommand('spotkanie /dzis|', 'today'))).toBe('spotkanie [[2026-10-14]]|');
    expect(show(runCommand('/jutro|', 'tomorrow'))).toBe('[[2026-10-15]]|');
  });
  it('headings, quote and priority', () => {
    expect(show(runCommand('Tytuł /h1|', 'h2'))).toBe('## Tytuł |');
    expect(show(runCommand('# Stary /h|', 'h3'))).toBe('### Stary |');
    expect(show(runCommand('mądre słowa /cyt|', 'quote'))).toBe('> mądre słowa |');
    expect(show(runCommand('- [ ] pilne /prio|', 'priorityHigh'))).toBe('- [ ] pilne ⏫|');
  });
  it('table skeleton with the cursor in the first header cell', () => {
    // "| " + cursor + " |  |  |"
    expect(show(runCommand('/tab|', 'table'))).toBe('| | |  |  |\n| --- | --- | --- |\n|  |  |  |\n|  |  |  |');
  });
  it('calc and query blocks', () => {
    expect(show(runCommand('/obl|', 'calc'))).toBe('```calc\n|\n```');
    // Only valid query syntax (no `# comment` line, which the query language would search for).
    expect(show(runCommand('/zap|', 'query'))).toBe('```query\ntype:tasks status:open due:<=+7d sort:due|\n```');
  });
  it('horizontal rule after a paragraph gets a blank line (no setext heading)', () => {
    expect(show(runCommand('akapit\n/lin|', 'hr'))).toBe('akapit\n\n---\n|');
  });
  it('property selects the key placeholder', () => {
    expect(show(runCommand('/wla|', 'property'))).toBe('«klucz»:: ');
  });
  it('time inserts HH:mm', () => {
    expect(show(runCommand('o /godz|', 'time'))).toBe('o 10:30|');
  });
  it('callouts', () => {
    expect(show(runCommand('/call|', 'calloutTip'))).toBe('> [!tip] \n> |');
  });
  it('dialog commands insert the result where the trigger was', () => {
    let request: EditorDialogRequest | null = null;
    const view = runCommand('- [ ] zadzwonić /term|', 'due', (r) => (request = r));
    expect(show(view)).toBe('- [ ] zadzwonić |');
    expect(request!.kind).toBe('date');
    (request as unknown as Extract<EditorDialogRequest, { kind: 'date' }>).onSubmit('2026-10-20T09:00');
    expect(show(view)).toBe('- [ ] zadzwonić 📅 2026-10-20 09:00|');
  });
  it('reminder dialog result with recurrence', () => {
    let request: EditorDialogRequest | null = null;
    const view = runCommand('leki /przyp|', 'reminder', (r) => (request = r));
    expect(request!.kind).toBe('reminder');
    (request as unknown as Extract<EditorDialogRequest, { kind: 'reminder' }>).onSubmit({
      at: '2026-10-15T08:00',
      alarm: true,
      recurrence: 'every day',
    });
    expect(show(view)).toBe('leki ⏰! 2026-10-15 08:00 🔁 every day|');
  });
  it('a dialog command is one undo step and undo does not bring the /query back', () => {
    let request: EditorDialogRequest | null = null;
    const view = new EditorView({
      state: EditorState.create({
        doc: '- [ ] zadzwonić ',
        selection: EditorSelection.cursor(16),
        extensions: [brulionLanguage(), rangeTracker, history(), editorDialogs.of((r) => (request = r))],
      }),
      parent: document.body,
    });
    views.push(view);
    view.dispatch({ changes: { from: 16, insert: '/term' }, selection: EditorSelection.cursor(21), userEvent: 'input.type' });
    SLASH_COMMANDS.find((c) => c.id === 'due')!.run(view, 16, 21);
    (request as unknown as Extract<EditorDialogRequest, { kind: 'date' }>).onSubmit('2026-10-20');
    expect(view.state.doc.toString()).toBe('- [ ] zadzwonić 📅 2026-10-20');
    undo(view);
    expect(view.state.doc.toString()).toBe('- [ ] zadzwonić ');
    undo(view);
    expect(view.state.doc.toString()).not.toContain('/term');
  });
  it('a cancelled dialog releases its tracked position', () => {
    let request: EditorDialogRequest | null = null;
    const view = runCommand('- [ ] x /term|', 'due', (r) => (request = r));
    expect(view.state.field(rangeTracker).size).toBe(1);
    request!.onCancel!();
    expect(view.state.field(rangeTracker).size).toBe(0);
  });
  it('template dialog inserts the filled template at {{cursor}}', async () => {
    let request: EditorDialogRequest | null = null;
    const view = runCommand('/szab|', 'template', (r) => (request = r));
    const req = request as unknown as Extract<EditorDialogRequest, { kind: 'template' }>;
    expect(req.templates).toEqual(['templates/spotkanie.md']);
    req.onSubmit('templates/spotkanie.md');
    await new Promise((r) => setTimeout(r, 10));
    expect(show(view)).toBe('## 2026-10-14\n- |\n');
  });
});

describe('DialogController', () => {
  it('reports cancelled, replaced and closed-by-teardown dialogs', () => {
    const c = new DialogController();
    const log: string[] = [];
    const req = (name: string): EditorDialogRequest => ({
      kind: 'recurrence',
      onSubmit: () => log.push(`${name}:submit`),
      onCancel: () => log.push(`${name}:cancel`),
    });
    c.open(req('a'));
    c.close();
    c.open(req('b'));
    c.open(req('c'));
    c.close(true);
    expect(log).toEqual(['a:cancel', 'b:cancel']);
    expect(c.current()).toBeNull();
  });
});

describe('lineContextText', () => {
  it('strips markers, tokens and the trigger', () => {
    expect(lineContextText('\t- [ ] Zadzwonić 📅 2026-10-15 ⏫ ^abc')).toBe('Zadzwonić');
    expect(lineContextText('- leki /przyp')).toBe('leki');
    expect(lineContextText('Akapit')).toBe('Akapit');
  });
});

describe('slash completion source', () => {
  it('offers filtered commands from the trigger position', async () => {
    const view = viewOf('- a /prz|');
    const res = await slashCompletionSource()(new CompletionContext(view.state, view.state.selection.main.head, false));
    expect(res!.from).toBe(4);
    expect(res!.options[0]!.label).toBe('Przypomnienie…');
  });
  it('not inside inline code or comments', async () => {
    for (const input of ['ścieżka `cd /us|r`', 'ukryte %%notatka /pr|%%']) {
      const view = viewOf(input);
      expect(await slashCompletionSource()(new CompletionContext(view.state, view.state.selection.main.head, false)), input).toBeNull();
    }
  });
  it('not inside code blocks', async () => {
    const view = viewOf('```\n/prz|\n```');
    expect(await slashCompletionSource()(new CompletionContext(view.state, view.state.selection.main.head, false))).toBeNull();
  });
});
