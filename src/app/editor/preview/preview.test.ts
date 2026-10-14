// @vitest-environment jsdom
/**
 * Live preview in a real EditorView (jsdom): a rich note renders without exceptions and with
 * the expected widgets; reveal on selection; clicks on widgets act (links, checkboxes,
 * bullets, folds, tables) without moving the cursor into links.
 */
import { foldedRanges, forceParsing } from '@codemirror/language';
import { EditorSelection } from '@codemirror/state';
import { EditorView } from '@codemirror/view';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { createTranslator } from '@/core/i18n';
import '../testutil';
import { contextFor, type EditorDialogRequest, type EditorHost, type EditorRoute } from '../context';
import { FakeWorkspace } from '../fake-workspace';
import { editorExtensions } from '../setup';
import { zoomField } from '../zoom';
import { setPreviewFocus } from './state';

const NOTE = 'Projekty/Remont.md';

const RICH = `---
aliases: [Kuchnia]
status: w toku
---
# Remont kuchni

> [!tip] Cel
> Nowe blaty i **lepsze** oświetlenie.

kwota:: 18000

## Plan prac
- Demontaż szafek
	- [x] Odłączyć zmywarkę ✅ 2026-10-10
	- [ ] Hydraulik 📅 2026-10-12 ⏰ 2026-10-14 16:00 ⏫
		- szczegóły ^szczegoly
- [/] Wybrać płytki [[Projekt ogrodu|ogród]] i [[Brak strony]] #remont 🔁 every week
- DOING Zamówić blat
1. pierwszy
2. drugi

Tekst z *kursywą*, ~~skreśleniem~~, ==zakreśleniem== i \`kodem\`, [link](https://example.com) oraz ((plytki)) %%ukryte%%.
Wynik: \`= 2 * 21\`

`;

const LOWER = `| Pozycja | Ilość | Cena | Razem |
| --- | --: | --: | --: |
| Płytki | 6 | 89 | =[Ilość]*[Cena] |
| **Suma** | | | =sum(above) |

\`\`\`calc
a = 6
a * 7
\`\`\`

\`\`\`query
type:tasks status:open
\`\`\`

![[Projekt ogrodu#Rabaty]]
!((plytki))
![[plan.png|300]]

---
`;

const OTHER = `# Projekt ogrodu

## Rabaty
- Tulipany przy ścieżce #ogród
- [ ] Wybrać płytki do ogrodu ^plytki
`;

interface Harness {
  view: EditorView;
  ws: FakeWorkspace;
  opened: { target: string; newPane?: boolean }[];
  routes: { route: EditorRoute; newPane?: boolean }[];
  dialogs: EditorDialogRequest[];
}

const views: EditorView[] = [];

afterEach(() => {
  for (const v of views.splice(0)) v.destroy();
  document.body.innerHTML = '';
});

function setup(doc = RICH, opts: { focus?: boolean; cursor?: number; readOnly?: boolean } = {}): Harness {
  const ws = new FakeWorkspace({ files: { [NOTE]: doc, 'Projekty/Projekt ogrodu.md': OTHER } });
  const opened: Harness['opened'] = [];
  const routes: Harness['routes'] = [];
  const dialogs: EditorDialogRequest[] = [];
  const host: EditorHost = {
    workspace: ws,
    t: createTranslator('pl'),
    locale: 'pl',
    now: () => new Date(2026, 9, 14, 10, 30),
    navigate: (route, o) => routes.push({ route, newPane: o?.newPane }),
    openLink: (target, o) => opened.push({ target, newPane: o?.newPane }),
  };
  const parent = document.createElement('div');
  document.body.appendChild(parent);
  const view = new EditorView({
    doc,
    selection: EditorSelection.cursor(opts.cursor ?? 0),
    extensions: editorExtensions({ context: contextFor(host, NOTE), dialogs: (r) => dialogs.push(r), readOnly: opts.readOnly }),
    parent,
  });
  views.push(view);
  forceParsing(view, view.state.doc.length, 10_000);
  if (opts.focus) view.dispatch({ effects: setPreviewFocus.of(true) });
  return { view, ws, opened, routes, dialogs };
}

function q(view: EditorView, sel: string): HTMLElement[] {
  return [...view.dom.querySelectorAll<HTMLElement>(sel)];
}

/** What an element shows: its text without hidden sources, plus CSS-drawn labels. */
function shown(el: Element): string {
  let out = '';
  const walk = (n: Node) => {
    if (n.nodeType === 3) {
      out += n.textContent ?? '';
      return;
    }
    if (!(n instanceof Element) || n.classList.contains('b-lp-src')) return;
    const label = n.getAttribute('data-lp-label');
    if (label !== null) out += label;
    n.childNodes.forEach(walk);
  };
  walk(el);
  return out;
}

function lineEl(view: EditorView, text: string): HTMLElement {
  const el = q(view, '.cm-line').find((l) => l.textContent?.includes(text));
  if (!el) throw new Error(`no line with ${text}`);
  return el;
}

function mouse(el: Element, type = 'mousedown', init: MouseEventInit = {}): MouseEvent {
  const e = new MouseEvent(type, { bubbles: true, cancelable: true, button: 0, ...init });
  el.dispatchEvent(e);
  return e;
}

describe('live preview (EditorView in jsdom)', () => {
  it('renders a rich note without exceptions and with the expected widgets', () => {
    const errors = vi.spyOn(console, 'error').mockImplementation(() => {});
    const { view } = setup();
    expect(errors).not.toHaveBeenCalled();
    errors.mockRestore();
    expect(q(view, '.b-lp-props')).toHaveLength(1);
    // Outline
    expect(q(view, '.b-lp-bullet').length).toBeGreaterThanOrEqual(4);
    expect(q(view, '.b-lp-number')).toHaveLength(2);
    expect(q(view, '.b-lp-checkbox').length).toBeGreaterThanOrEqual(3);
    expect(q(view, '.b-lp-task--doing')).toHaveLength(2);
    expect(q(view, '.b-lp-task__keyword').map(shown)).toEqual(['DOING']);
    // Chips
    expect(q(view, '.b-lp-chip--due')).toHaveLength(1);
    expect(q(view, '.b-lp-chip--due')[0]!.classList.contains('is-overdue')).toBe(true);
    expect(q(view, '.b-lp-chip--reminder')).toHaveLength(1);
    expect(shown(q(view, '.b-lp-chip--recurrence')[0]!)).toContain('co tydzień');
    expect(q(view, '.b-lp-priority--high')).toHaveLength(1);
    // Links, refs, tags
    expect(q(view, '.b-lp-wikilink').map(shown)).toEqual(expect.arrayContaining(['ogród', 'Brak strony']));
    expect(q(view, '.b-lp-wikilink.is-missing')).toHaveLength(1);
    expect(q(view, '.b-lp-tag')).toHaveLength(1);
    expect(shown(q(view, '.b-lp-blockref')[0]!)).toBe('Wybrać płytki do ogrodu');
    expect(q(view, '.b-lp-url').length).toBeGreaterThanOrEqual(1);
    // Headings, callout, inline calc
    expect(q(view, '.b-lp-h1')).toHaveLength(1);
    expect(q(view, '.b-lp-callout--tip.is-head')).toHaveLength(1);
    expect(shown(q(view, '.b-lp-calc')[0]!)).toBe('42');
  });

  it('renders tables, calc results, queries, embeds and rules', () => {
    const errors = vi.spyOn(console, 'error').mockImplementation(() => {});
    const { view } = setup(LOWER);
    expect(errors).not.toHaveBeenCalled();
    errors.mockRestore();
    expect(q(view, '.b-lp-table')).toHaveLength(1);
    expect(q(view, '.b-lp-table td.is-formula').map((e) => e.textContent)).toEqual(['534', '534']);
    expect(q(view, '.b-lp-query')).toHaveLength(1);
    expect(q(view, '.b-lp-calcres').map(shown)).toEqual(['6', '42']);
    expect(q(view, '.b-lp-embed--note')).toHaveLength(2);
    expect(q(view, '.b-lp-embed--note .b-lp-embed__body')[0]!.textContent).toContain('Tulipany');
    expect(q(view, '.b-lp-embed--image')).toHaveLength(1);
    expect(q(view, '.b-lp-hr')).toHaveLength(1);
  });

  it('reveals syntax only where the focused selection is', () => {
    const { view } = setup(RICH, { focus: true, cursor: RICH.indexOf('Plan prac') });
    // Heading marks of the active line are visible, other headings hide theirs.
    expect(lineEl(view, 'Plan prac').querySelector('.b-lp-hide')).toBeNull();
    expect(lineEl(view, 'Remont kuchni').querySelector('.b-lp-hide')).not.toBeNull();
    const strong = RICH.indexOf('**lepsze**') + 3;
    view.dispatch({ selection: EditorSelection.cursor(strong) });
    expect(lineEl(view, 'lepsze').querySelectorAll('.b-lp-hide')).toHaveLength(0);
    expect(lineEl(view, 'Plan prac').querySelector('.b-lp-hide')).not.toBeNull();
    // Losing focus renders everything again.
    view.dispatch({ effects: setPreviewFocus.of(false) });
    expect(lineEl(view, 'lepsze').querySelectorAll('.b-lp-hide').length).toBeGreaterThan(0);
  });

  it('opens links, tags, URLs and block refs without moving the cursor', () => {
    const { view, ws, opened, routes } = setup(RICH, { focus: true, cursor: 0 });
    const before = view.state.selection.main.head;
    const alias = q(view, '.b-lp-wikilink').find((e) => shown(e) === 'ogród')!;
    expect(mouse(alias).defaultPrevented).toBe(true);
    mouse(q(view, '.b-lp-wikilink.is-missing')[0]!, 'mousedown', { ctrlKey: true });
    expect(opened).toEqual([
      { target: 'Projekt ogrodu', newPane: false },
      { target: 'Brak strony', newPane: true },
    ]);
    mouse(q(view, '.b-lp-tag')[0]!);
    expect(routes[0]).toEqual({ route: { view: 'tags', tag: 'remont' }, newPane: false });
    mouse(q(view, '.b-lp-url')[0]!);
    expect(ws.opened).toEqual(['https://example.com']);
    mouse(q(view, '.b-lp-blockref')[0]!, 'mousedown', { button: 1 });
    expect(routes[1]).toMatchObject({ route: { view: 'page', path: 'Projekty/Projekt ogrodu.md', blockId: 'plytki' }, newPane: true });
    expect(view.state.selection.main.head).toBe(before);
  });

  it('toggles tasks with the checkbox, also Logseq keywords', () => {
    const { view } = setup();
    const line = () => view.state.doc.toString().split('\n').find((l) => l.includes('Hydraulik'))!;
    const box = q(view, '.b-lp-checkbox').find((b) => b.closest('.cm-line')?.textContent?.includes('Hydraulik'))!;
    mouse(box);
    expect(line()).toMatch(/^\t- \[x\] Hydraulik .* ✅ 2026-10-14$/);
    const doing = q(view, '.b-lp-task--doing').find((b) => b.closest('.cm-line')?.textContent?.includes('DOING'))!;
    mouse(doing.querySelector('.b-lp-checkbox')!);
    expect(view.state.doc.toString()).toContain('- DONE Zamówić blat');
  });

  it('does not edit a read-only note', () => {
    const { view } = setup(RICH, { readOnly: true });
    mouse(q(view, '.b-lp-checkbox')[1]!);
    expect(view.state.doc.toString()).toBe(RICH);
  });

  it('zooms into a block from its bullet and folds from its triangle', () => {
    const { view } = setup();
    const lineOf = (text: string) => view.state.doc.lineAt(view.state.doc.toString().indexOf(text)).number - 1;
    const bullet = q(view, '.b-lp-bullet').find((b) => b.closest('.cm-line')?.textContent?.includes('Demontaż'))!;
    expect(bullet.querySelector('.b-lp-fold')).not.toBeNull();
    mouse(bullet.querySelector('.b-lp-fold')!);
    let folds = 0;
    foldedRanges(view.state).between(0, view.state.doc.length, () => {
      folds++;
    });
    expect(folds).toBe(1);
    mouse(q(view, '.b-lp-bullet').find((b) => b.closest('.cm-line')?.textContent?.includes('Demontaż'))!.querySelector('.b-lp-bullet__dot')!);
    const zoom = view.state.field(zoomField);
    expect(zoom).not.toBeNull();
    expect(view.state.doc.lineAt(zoom!.from).number - 1).toBe(lineOf('- Demontaż'));
  });

  it('edits dates and reminders from their chips', () => {
    const { view, dialogs } = setup();
    mouse(q(view, '.b-lp-chip--due')[0]!);
    const date = dialogs.at(-1)!;
    expect(date).toMatchObject({ kind: 'date', initial: '2026-10-12' });
    if (date.kind === 'date') date.onSubmit('2026-10-20');
    expect(view.state.doc.toString()).toContain('📅 2026-10-20 ⏰');
    mouse(q(view, '.b-lp-chip--reminder')[0]!);
    const rem = dialogs.at(-1)!;
    expect(rem).toMatchObject({ kind: 'reminder', initial: { at: '2026-10-14T16:00', alarm: false } });
    if (rem.kind === 'reminder') rem.onSubmit({ at: '2026-10-15T07:00', alarm: true });
    expect(view.state.doc.toString()).toContain('⏰! 2026-10-15 07:00');
  });

  it('puts the cursor into a clicked table cell and edits the table from its toolbar', () => {
    const { view } = setup(LOWER);
    const cell = q(view, '.b-lp-table td').find((td) => td.textContent === '89')!;
    mouse(cell);
    expect(view.state.doc.sliceString(view.state.selection.main.head - 2, view.state.selection.main.head)).toBe('89');
    view.dispatch({ selection: EditorSelection.cursor(view.state.doc.length) });
    mouse(q(view, '[data-lp="table-add-row"]')[0]!);
    const table = view.state.doc.toString().split('\n\n')[0]!.split('\n');
    expect(table).toHaveLength(5);
    expect(table[4]).toMatch(/^\|\s+\|\s+\|\s+\|\s+\|$/);
  });

  it('lists query results and toggles their tasks in the other file', async () => {
    const { view, ws } = setup(LOWER);
    const rows = q(view, '.b-lp-query__row--task');
    expect(rows.map((r) => r.querySelector('.b-lp-query__text')!.textContent)).toContain('Wybrać płytki do ogrodu');
    mouse(rows.find((r) => r.textContent?.includes('Wybrać płytki'))!.querySelector('.b-lp-checkbox')!);
    await vi.waitFor(() => expect(ws.writes.at(-1)?.path).toBe('Projekty/Projekt ogrodu.md'));
    expect(ws.writes.at(-1)!.content).toContain('- [x] Wybrać płytki do ogrodu ✅ 2026-10-14 ^plytki');
  });
});
