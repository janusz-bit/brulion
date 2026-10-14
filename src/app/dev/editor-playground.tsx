/**
 * Editor playground (editor.html): <NoteEditor> on an in-memory Polish vault (FakeWorkspace),
 * plus a compact editor like the journal stream. Dev/test page for screenshots (e2e/editor.spec.ts)
 * and manual checks; the app never imports it.
 *
 * URL parameters: `theme=light|dark|system` (default system), `locale=pl|en`, `note=<path>`,
 * `zoom=<0-based line>`, `w=mobile` (412 px column), `readonly=1`, `now=real` (default: a fixed
 * clock, 2026-10-14 10:30, so screenshots are stable), `font=sans|serif`.
 */
import '../styles/theme.css';
import '../styles/components.css';
import '../styles/editor.css';
import { EditorView } from '@codemirror/view';
import { StrictMode, useMemo, useState } from 'react';
import { createRoot, type Root } from 'react-dom/client';
import { Button, LocaleContext, SegmentedControl, Select, Switch, translatorFor } from '@/app/components';
import { EditorHostProvider, type EditorHost, type EditorRoute } from '@/app/editor/context';
import { FakeWorkspace } from '@/app/editor/fake-workspace';
import { NoteEditor } from '@/app/editor/NoteEditor';
import { pageTitle } from '@/core/paths';
import type { Locale, VaultPath } from '@/core/types';

const FIXED_NOW = new Date(2026, 9, 14, 10, 30);

const PLAN_PNG =
  'iVBORw0KGgoAAAANSUhEUgAAAKAAAABgCAIAAAAVRe7OAAABaElEQVR42u3csQnCQBQG4Gxi4wAu4Ab2jucQGcEdHMAipYUIFjYRxEIIRySa3Lv74Lf70+TjXvE405y6mxSc5vl73C/p6MTtAAbsZQHWAawDWAewzs+AV+uNBApgwEPAxl3+HcDVAw/m9Zhdbv5JSznBRvT7gfN+FyWAAQMGDBgwYMCAAQMOAXxtt1MCGDBgwIBrBB654QwEvMhOeCLw8rtoJ9iIBgwYMGDAbnTk2KnrBAMGDBgwYMA6gAEDdi/6212068rx7kU7wUY0YMBgAGfVORy7OQMYMGDAgAEDBgwYMGDAgP8PbBedyMzAdtFOsBENGDBgwIABAwYMGDBgwIABAwYM2C7aLtou2gk2ogEDBgwYMGDAgAEDBgwYMGDAgAEDtou2i7aL9g9/wIABAwYDWAewDmDAgAGPAfZCAesA1sn2e9FS+PeinZgCR7RECWDAxl2JHcCAvSzAOoB1AOsA1gGs89HpAZhuTGkGAz9pAAAAAElFTkSuQmCC';

function bytesOf(base64: string): Uint8Array {
  const bin = atob(base64);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}

const TODAY = 'journals/2026-10-14.md';
const PROJECT = 'Projekty/Remont kuchni.md';

const VAULT: Record<VaultPath, string> = {
  [TODAY]: `- Poranna kawa z Anią — omówiłyśmy [[Remont kuchni]] i budżet #rodzina
- [ ] Zadzwonić do hydraulika w sprawie zlewu 📅 2026-10-15 ⏰ 2026-10-14 16:00 ⏫
- [ ] Podlać kwiaty na balkonie 🔁 every week 📅 2026-10-14
- [x] Odebrać paczkę z paczkomatu ✅ 2026-10-14
- [/] Przeczytać rozdział 3 „Sztuki prowadzenia notatek” #książki
- Spotkanie zespołu o 11:00 #praca
	- ustalić termin wdrożenia ^wdrozenie
	- przypomnieć Markowi o raporcie ⏰! 2026-10-14 12:30
- Płytki: ((plytki)) — zamówić do piątku
- Pomysł: ==notatki głosowe== w aplikacji? %%do przemyślenia%%
`,
  'journals/2026-10-13.md': `- Długi spacer po Parku Szczytnickim, 9 km #zdrowie
- [x] Wysłać fakturę za wrzesień ✅ 2026-10-13
- Rozmowa z [[Anna Nowak|Anią]]: poleca ekipę od płytek
`,
  [PROJECT]: `---
aliases: [Kuchnia, Remont]
tags: [projekt, dom]
status: w toku
---
# Remont kuchni

> [!tip] Cel
> Nowe blaty, płytki nad zlewem i lepsze oświetlenie — do końca listopada.

kwota:: 18000
odpowiedzialna:: [[Anna Nowak]]

## Plan prac
- Demontaż starych szafek
	- [x] Odłączyć zmywarkę ✅ 2026-10-10
	- [x] Wynieść stare blaty ✅ 2026-10-11
- Instalacje #remont
	- [ ] Hydraulik: przesunięcie odpływu 📅 2026-10-15 ⏳ 2026-10-14
	- [ ] Elektryk: dodatkowe gniazdka nad blatem 📅 2026-10-20
- Wykończenie
	- [ ] Wybrać płytki 🔼 📅 2026-10-17 ^plytki
	- [ ] Zamówić blat dębowy 📅 2026-10-24
		- wymiary: 240 × 62 cm, grubość 4 cm
	- [ ] Montaż oświetlenia LED pod szafkami

## Kosztorys
| Pozycja | Ilość | Cena | Razem |
| --- | --- | --- | --- |
| Płytki (m²) | 6 | 89 | =[Ilość]*[Cena] |
| Blat dębowy | 1 | 2400 | =[Ilość]*[Cena] |
| Fuga i klej | 3 | 45 | =[Ilość]*[Cena] |
| **Suma** | | | =sum(above) |

\`\`\`calc
metraż = 6
cena = 89
płytki = metraż * cena
płytki + 10%
blat = 2400
suma
\`\`\`

## Otwarte zadania
\`\`\`query
type:tasks status:open path:Projekty sort:due
\`\`\`

## Plan i inspiracje
![[plan-kuchni.png]]
![[Projekt ogrodu#Rabaty]]

Termin wdrożenia z notatki zespołu: !((wdrozenie))

> [!warning] Uwaga
> Przed montażem blatu sprawdzić poziom szafek.
`,
  'Projekty/Projekt ogrodu.md': `# Projekt ogrodu
tags:: ogród, #dom

## Rabaty
- Tulipany i narcyzy przy ścieżce #ogród
- Lawenda pod oknem kuchni

## Warzywnik
- [ ] Przekopać grządki 🛫 2026-10-18 🔁 every year
`,
  'Ludzie/Anna Nowak.md': `---
aliases: [Ania]
---
# Anna Nowak
telefon:: 600 100 200
- Poznałyśmy się na kursie ceramiki #ludzie
- Urodziny ⏰ 2026-11-03 09:00 🔁 every year
`,
  'Zakupy.md': `# Zakupy
- [ ] mleko owsiane
- [ ] chleb żytni
- [ ] kawa ziarnista ⏫
`,
  'templates/Spotkanie.md': `## Spotkanie {{date}} {{time}}
uczestnicy::
- Notatki
	- {{cursor}}
- Ustalenia
`,
  'templates/journal.md': '- {{cursor}}\n',
};

function params(): URLSearchParams {
  return new URLSearchParams(typeof location !== 'undefined' ? location.search : '');
}

const PLAYGROUND_CSS = `
.pg { min-height: 100vh; display: flex; flex-direction: column; background: var(--bg); color: var(--fg); }
.pg__bar { position: sticky; top: 0; z-index: var(--z-sticky); display: flex; flex-wrap: wrap; align-items: center; gap: var(--space-2) var(--space-3); padding: var(--space-2) var(--space-4); padding-top: calc(var(--space-2) + var(--safe-top)); background: var(--bg-elev); border-bottom: 1px solid var(--border); }
.pg__bar h1 { font-size: var(--text-md); margin-right: auto; }
.pg__bar .b-field { margin: 0; }
.pg__main { flex: 1; display: flex; flex-direction: column; width: 100%; margin: 0 auto; }
.pg[data-w='mobile'] .pg__main { max-width: 412px; border-inline: 1px solid var(--border); }
.pg__page { height: 62vh; min-height: 22rem; border-bottom: 1px solid var(--border); }
.pg__stream { padding: var(--space-4) 0 var(--space-12); }
.pg__stream .b-editor { --editor-pad-x: var(--space-6); }
.pg__stream h2 { max-width: var(--measure); margin: 0 auto var(--space-2); padding: 0 var(--space-6); font-size: var(--text-sm); font-weight: var(--weight-semibold); color: var(--fg-muted); text-transform: uppercase; letter-spacing: 0.06em; }
.pg__log { max-width: var(--measure); margin: var(--space-3) auto 0; padding: 0 var(--space-6); font-size: var(--text-xs); color: var(--fg-muted); min-height: 1.2em; }
@media (max-width: 767.98px) { .pg__stream h2, .pg__log { padding: 0 var(--space-4); } .pg__stream .b-editor { --editor-pad-x: var(--space-4); } }
`;

function Playground() {
  const p = params();
  const [locale, setLocale] = useState<Locale>(p.get('locale') === 'en' ? 'en' : 'pl');
  const [theme, setTheme] = useState<string>(p.get('theme') ?? 'system');
  const [note, setNote] = useState<VaultPath>(p.get('note') ?? PROJECT);
  const [readOnly, setReadOnly] = useState(p.get('readonly') === '1');
  const [log, setLog] = useState('');
  const zoomParam = p.get('zoom');
  const zoomLine = zoomParam !== null && zoomParam !== '' ? Number(zoomParam) : undefined;
  const realNow = p.get('now') === 'real';

  const workspace = useMemo(() => new FakeWorkspace({ files: { ...VAULT, 'assets/plan-kuchni.png': bytesOf(PLAN_PNG) } }), []);
  const t = translatorFor(locale);

  const host = useMemo((): EditorHost => {
    const go = (path: VaultPath, line?: number) => {
      setNote(path);
      setLog(`→ ${path}${line !== undefined ? `:${line + 1}` : ''}`);
    };
    return {
      workspace,
      t,
      locale,
      now: () => (realNow ? new Date() : FIXED_NOW),
      navigate: (route: EditorRoute, opts) => {
        if (route.view === 'page') go(route.path, route.line);
        else setLog(`navigate ${JSON.stringify(route)}${opts?.newPane ? ' (panel)' : ''}`);
      },
      openLink: (target, opts) => {
        const path = workspace.index.resolveLink(target, opts?.from);
        if (path) go(path);
        else setLog(`nowa strona: ${target}`);
      },
    };
  }, [workspace, t, locale, realNow]);

  // Theme on <html> so portals (dialogs, toolbar) follow it too.
  if (typeof document !== 'undefined') {
    document.documentElement.dataset.theme = theme;
    document.documentElement.lang = locale;
    const font = p.get('font');
    if (font === 'sans' || font === 'serif') document.documentElement.dataset.editorFont = font;
  }

  const notes = workspace.index.paths().filter((x) => !x.startsWith('templates/'));
  const simulateExternal = () => {
    void workspace.read(note).then((text) => {
      workspace.externalWrite(note, `${text.replace(/\n?$/, '\n')}- Dopisane na telefonie (synchronizacja)\n`);
      setLog('zmiana z innego urządzenia');
    });
  };

  return (
    <LocaleContext.Provider value={locale}>
      <EditorHostProvider value={host}>
        <style>{PLAYGROUND_CSS}</style>
        <div className="pg" data-w={p.get('w') ?? undefined}>
          <header className="pg__bar">
            <h1>Brulion — edytor</h1>
            <Select
              aria-label="Notatka"
              value={note}
              onChange={(e) => setNote(e.currentTarget.value)}
              options={notes.map((x) => ({ value: x, label: pageTitle(x) }))}
            />
            <SegmentedControl
              aria-label="Motyw"
              value={theme}
              onChange={setTheme}
              options={[
                { value: 'light', label: 'Jasny' },
                { value: 'dark', label: 'Ciemny' },
                { value: 'system', label: 'System' },
              ]}
            />
            <SegmentedControl
              aria-label="Język"
              value={locale}
              onChange={(v) => setLocale(v as Locale)}
              options={[
                { value: 'pl', label: 'PL' },
                { value: 'en', label: 'EN' },
              ]}
            />
            <Switch checked={readOnly} onCheckedChange={setReadOnly} label="Tylko odczyt" />
            <Button size="sm" variant="secondary" onClick={simulateExternal}>
              Zmiana z sync
            </Button>
          </header>
          <main className="pg__main">
            <section className="pg__page" data-testid="page-editor">
              <NoteEditor key={note} path={note} zoomLine={zoomLine} readOnly={readOnly} placeholder={t('editor.placeholder')} />
            </section>
            <p className="pg__log" aria-live="polite">
              {log}
            </p>
            <section className="pg__stream" data-testid="journal-stream">
              <h2>{locale === 'pl' ? 'Dziennik — wczoraj' : 'Journal — yesterday'}</h2>
              <NoteEditor path="journals/2026-10-13.md" compact readOnly={readOnly} />
            </section>
          </main>
        </div>
      </EditorHostProvider>
    </LocaleContext.Provider>
  );
}

/** Test/debug hook: `window.brulionPlayground.view('[data-testid=page-editor]')`. */
declare global {
  interface Window {
    brulionPlayground?: { view(scope: string): EditorView | null; root?: Root };
  }
}

export function mountEditorPlayground(el: HTMLElement): void {
  // Re-executed by HMR in dev: reuse the root instead of creating a second one.
  const root = window.brulionPlayground?.root ?? createRoot(el);
  window.brulionPlayground = {
    root,
    view: (scope) => {
      const dom = document.querySelector(`${scope} .cm-editor`);
      return dom instanceof HTMLElement ? EditorView.findFromDOM(dom) : null;
    },
  };
  root.render(
    <StrictMode>
      <Playground />
    </StrictMode>,
  );
}

const rootEl =
  typeof document !== 'undefined'
    ? (document.getElementById('root') ??
      (() => {
        const div = document.createElement('div');
        div.id = 'root';
        document.body.appendChild(div);
        return div;
      })())
    : null;
if (rootEl) mountEditorPlayground(rootEl);
