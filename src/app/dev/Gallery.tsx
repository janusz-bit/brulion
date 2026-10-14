/**
 * Component gallery for visual QA (screenshots in light/dark, PL/EN). Standalone: mount with
 * `mountGallery(el)` (see gallery-entry.tsx); the main app never imports it.
 *
 * URL parameters: `theme=light|dark|both|system` (default both), `locale=pl|en`,
 * `section=<id>` (render one section), `font=serif|sans`, `now=real` (use the real clock;
 * by default a fixed date keeps screenshots stable).
 */
import { StrictMode, useEffect, useMemo, useRef, useState, type ReactNode } from 'react';
import { createRoot } from 'react-dom/client';
import type { Locale } from '@/core/types';
import {
  Badge,
  Button,
  Checkbox,
  Chip,
  ConfirmDialog,
  DatePicker,
  DiffView,
  EmptyState,
  IconButton,
  Kbd,
  LocaleContext,
  Menu,
  MenuButton,
  MergeView,
  Modal,
  Popover,
  ProgressBar,
  ReminderDialog,
  SearchInput,
  SegmentedControl,
  Select,
  Spinner,
  Splitter,
  Switch,
  Tabs,
  TextArea,
  TextInput,
  ToastView,
  Tooltip,
  VirtualList,
  type DayMarks,
  type DiffMode,
  type MenuItem,
  type MergeHunk,
  type ReminderValue,
} from '@/app/components';

type ThemeMode = 'light' | 'dark' | 'both' | 'system';

const FIXED_NOW = new Date(2026, 9, 14, 10, 30);

const SECTIONS = [
  ['tokens', 'Tokens & typography'],
  ['buttons', 'Buttons'],
  ['inputs', 'Inputs'],
  ['toggles', 'Checkbox & switch'],
  ['badges', 'Badges, chips & keys'],
  ['tabs', 'Tabs & segmented control'],
  ['feedback', 'Spinner, progress, toasts'],
  ['empty', 'Empty state & tooltip'],
  ['menus', 'Popover & menu'],
  ['dialogs', 'Dialogs'],
  ['datepicker', 'Date picker'],
  ['reminder', 'Reminder dialog'],
  ['diff', 'Diff view'],
  ['merge', 'Merge view'],
  ['layout', 'Splitter & virtual list'],
] as const;
type SectionId = (typeof SECTIONS)[number][0];

function params(): URLSearchParams {
  return new URLSearchParams(typeof location !== 'undefined' ? location.search : '');
}

const GALLERY_CSS = `
.b-gallery { min-height: 100vh; background: var(--bg); color: var(--fg); }
.b-gallery__bar { position: sticky; top: 0; z-index: var(--z-sticky); display: flex; flex-wrap: wrap; align-items: center; gap: var(--space-2) var(--space-4); padding: var(--space-3) var(--space-4); padding-top: calc(var(--space-3) + var(--safe-top)); background: var(--bg-elev); border-bottom: 1px solid var(--border); }
.b-gallery__bar h1 { font-size: var(--text-md); margin-right: auto; }
.b-gallery__cols { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 34rem), 1fr)); }
.b-gallery__col { padding: var(--space-4); min-width: 0; display: grid; grid-template-columns: minmax(0, 1fr); gap: var(--space-4); align-content: start; }
.b-gallery__coltitle { font-size: var(--text-xs); color: var(--fg-faint); text-transform: uppercase; letter-spacing: .08em; }
.b-gallery__section { display: grid; grid-template-columns: minmax(0, 1fr); gap: var(--space-3); padding: var(--space-4); border: 1px solid var(--border); border-radius: var(--radius-lg); background: var(--bg); min-width: 0; }
.b-gallery__section > h2 { font-size: var(--text-sm); color: var(--fg-muted); font-weight: var(--weight-semibold); }
.b-gallery__row { display: flex; flex-wrap: wrap; align-items: center; gap: var(--space-2); }
.b-gallery__stack { display: grid; gap: var(--space-3); align-content: start; }
.b-gallery__grid2 { display: grid; gap: var(--space-3); grid-template-columns: repeat(auto-fit, minmax(14rem, 1fr)); }
.b-gallery__label { font-size: var(--text-xs); color: var(--fg-faint); }
.b-gallery__swatches { display: grid; grid-template-columns: repeat(auto-fill, minmax(7.5rem, 1fr)); gap: var(--space-2); }
.b-gallery__swatch { display: grid; gap: 2px; font-size: var(--text-2xs); color: var(--fg-muted); }
.b-gallery__swatch > span:first-child { height: 2rem; border-radius: var(--radius-sm); border: 1px solid var(--border); }
.b-gallery__editor { font-family: var(--font-editor); font-size: var(--editor-font-size); line-height: var(--leading-loose); max-width: var(--measure); }
.b-gallery__phone { width: 360px; max-width: 100%; border: 1px solid var(--border-strong); border-radius: 24px; padding: 12px 0 0; overflow: hidden; background: var(--bg-sunken); }
.b-gallery .b-tooltip.is-forced > .b-tooltip__bubble { opacity: 1; visibility: visible; }
.b-gallery__toasts { display: grid; gap: var(--space-2); max-width: 26rem; }
.b-gallery__box { height: 14rem; border: 1px solid var(--border); border-radius: var(--radius); overflow: hidden; background: var(--bg-elev); }
.b-gallery__pane { padding: var(--space-3); font-size: var(--text-sm); color: var(--fg-muted); }
.b-gallery__vrow { display: flex; align-items: center; gap: var(--space-2); height: 100%; padding: 0 var(--space-3); border-bottom: 1px solid var(--border-subtle); font-size: var(--text-sm); }
`;

function Section({ id, title, only, children }: { id: SectionId; title: string; only: string | null; children: ReactNode }) {
  if (only && only !== id) return null;
  return (
    <section className="b-gallery__section" id={`s-${id}`} data-section={id}>
      <h2>{title}</h2>
      {children}
    </section>
  );
}

function Row({ children, label }: { children: ReactNode; label?: string }) {
  return (
    <div className="b-gallery__stack">
      {label && <div className="b-gallery__label">{label}</div>}
      <div className="b-gallery__row">{children}</div>
    </div>
  );
}

const SWATCHES = [
  '--bg',
  '--bg-elev',
  '--bg-sunken',
  '--fg',
  '--fg-muted',
  '--fg-faint',
  '--border',
  '--primary',
  '--accent',
  '--link',
  '--tag-bg',
  '--reminder',
  '--danger',
  '--warning',
  '--success',
  '--selection',
];

const MENU_ITEMS: MenuItem[] = [
  { type: 'label', label: 'Strona' },
  { id: 'open', label: 'Otwórz w panelu bocznym', icon: 'columns', shortcut: 'Mod+Enter' },
  { id: 'rename', label: 'Zmień nazwę', icon: 'pencil', shortcut: 'F2' },
  { id: 'fav', label: 'Ulubione', checked: true },
  { id: 'history', label: 'Historia zmian', icon: 'undo', description: '12 wersji' },
  { type: 'separator' },
  { id: 'export', label: 'Eksportuj…', icon: 'external', disabled: true },
  { id: 'delete', label: 'Usuń stronę', icon: 'trash', danger: true },
];

const MARKS: DayMarks = {
  '2026-10-02': 'journal',
  '2026-10-07': ['journal', 'task'],
  '2026-10-12': 'overdue',
  '2026-10-14': ['journal', 'reminder'],
  '2026-10-15': ['task', 'reminder'],
  '2026-10-16': 'due',
  '2026-10-21': ['event', 'done'],
  '2026-10-27': 'reminder',
  '2026-11-02': 'journal',
};

const DIFF_LEFT = `# Lista zakupów

- [ ] mleko
- [ ] chleb
- [x] masło
- [ ] jajka

## Notatki
Zadzwonić do mamy w sobotę.
Spotkanie o 18:00 w kawiarni „Pod Gruszą”.
Kupić prezent dla Zosi.
Sprawdzić rozkład pociągów.
Oddać książki do biblioteki.
Opłacić rachunek za prąd.
`;

const DIFF_RIGHT = `# Lista zakupów

- [ ] mleko
- [x] chleb żytni
- [x] masło
- [ ] jajka (10 szt.)
- [ ] pomidory

## Notatki
Zadzwonić do mamy w niedzielę.
Spotkanie o 18:00 w kawiarni „Pod Gruszą”.
Kupić prezent dla Zosi.
Sprawdzić rozkład pociągów.
Oddać książki do biblioteki.
`;

const HUNKS: MergeHunk[] = [
  { kind: 'ok', lines: ['# Plan tygodnia', '', '## Poniedziałek', '- [ ] przegląd zadań', '- [ ] mail do Ani', '- [ ] zakupy'] },
  {
    kind: 'conflict',
    base: ['- [ ] siłownia 18:00'],
    local: ['- [x] siłownia 18:00 ✅ 2026-10-13'],
    remote: ['- [ ] siłownia 19:00'],
  },
  { kind: 'ok', lines: ['', '## Wtorek', '- [ ] dentysta 10:30', '- [ ] odebrać paczkę'] },
  {
    kind: 'conflict',
    base: [],
    local: ['- [ ] kupić bilety do kina'],
    remote: ['- [ ] zadzwonić do hydraulika', '- [ ] zapłacić czynsz'],
  },
  { kind: 'ok', lines: ['', '## Środa', '- [ ] basen'] },
];

const LIST_ITEMS = Array.from({ length: 10_000 }, (_, i) => ({
  id: i,
  title: `Notatka ${i + 1}${i % 7 === 0 ? ' — zażółć gęślą jaźń' : ''}`,
}));

function Column({ only, now, label }: { only: string | null; now: Date; label?: string }) {
  const [text, setText] = useState('Ania');
  const [search, setSearch] = useState('');
  const [search2, setSearch2] = useState('spotkanie');
  const [area, setArea] = useState('Pierwsza linia\nDruga linia z polskimi znakami: ąćęłńóśźż');
  const [select, setSelect] = useState('week');
  const [checked, setChecked] = useState(true);
  const [sw, setSw] = useState(true);
  const [sw2, setSw2] = useState(false);
  const [tab, setTab] = useState('backlinks');
  const [seg, setSeg] = useState('month');
  const [chips, setChips] = useState<string[]>(['praca', 'dom']);
  const [filter, setFilter] = useState(true);
  const [date, setDate] = useState<string | null>('2026-10-15');
  const [dateTime, setDateTime] = useState<string | null>('2026-10-16T09:00');
  const [diffMode, setDiffMode] = useState<DiffMode>('split');
  const [merged, setMerged] = useState<string | null>(null);
  const [menuOpen, setMenuOpen] = useState(false);
  const [popOpen, setPopOpen] = useState(false);
  const [modalOpen, setModalOpen] = useState(false);
  const [confirmOpen, setConfirmOpen] = useState(false);
  const [reminderOpen, setReminderOpen] = useState(false);
  const [reminder, setReminder] = useState<ReminderValue | null>(null);
  const menuAnchor = useRef<HTMLButtonElement | null>(null);
  const popAnchor = useRef<HTMLButtonElement | null>(null);

  return (
    <>
      {label && <div className="b-gallery__coltitle">{label}</div>}

      <Section id="tokens" title="Tokens & typography" only={only}>
        <div className="b-gallery__swatches">
          {SWATCHES.map((v) => (
            <div key={v} className="b-gallery__swatch">
              <span style={{ background: `var(${v})` }} />
              <span>{v}</span>
            </div>
          ))}
        </div>
        <h1 style={{ fontSize: 'var(--text-2xl)' }}>Brulion — notatki offline</h1>
        <p className="b-gallery__editor">
          Zażółć gęślą jaźń. To jest akapit w kroju edytora: <a href="#tokens">[[Link do strony]]</a>,{' '}
          <mark>zaznaczenie</mark>, <code>kod_inline()</code> i <Badge variant="tag">projekt</Badge>. Czytelny,
          spokojny tekst na papierze.
        </p>
        <p style={{ color: 'var(--fg-muted)', fontSize: 'var(--text-sm)' }}>
          Tekst pomocniczy (fg-muted) · <span style={{ color: 'var(--fg-faint)' }}>tekst przygaszony (fg-faint)</span>
        </p>
      </Section>

      <Section id="buttons" title="Buttons" only={only}>
        <Row label="Variants">
          <Button variant="primary" icon="plus">
            Nowa strona
          </Button>
          <Button variant="secondary">Anuluj</Button>
          <Button variant="ghost" icon="settings">
            Ustawienia
          </Button>
          <Button variant="danger" icon="trash">
            Usuń
          </Button>
        </Row>
        <Row label="Sizes, loading, disabled">
          <Button size="sm">Mały</Button>
          <Button size="md">Średni</Button>
          <Button size="lg" variant="primary">
            Duży
          </Button>
          <Button loading variant="primary">
            Zapisywanie
          </Button>
          <Button disabled>Niedostępny</Button>
          <Button iconEnd="chevron-down">Więcej</Button>
        </Row>
        <Row label="Icon buttons">
          <IconButton icon="menu" label="Menu" />
          <IconButton icon="search" label="Szukaj" variant="secondary" />
          <IconButton icon="star" label="Ulubione" pressed />
          <IconButton icon="bell" label="Przypomnienia" badge={3} />
          <IconButton icon="x" label="Zamknij" size="sm" />
          <IconButton icon="plus" label="Dodaj" variant="primary" size="lg" />
          <IconButton icon="trash" label="Usuń" variant="danger" />
          <IconButton icon="sun" label="Ładowanie" loading />
        </Row>
      </Section>

      <Section id="inputs" title="Inputs" only={only}>
        <div className="b-gallery__grid2">
          <TextInput label="Nazwa strony" value={text} onValueChange={setText} hint="Bez ukośników ani dwukropków." />
          <TextInput label="Folder" icon="folder" placeholder="np. Projekty/2026" optional />
          <TextInput label="Adres serwera" value="nextcloud.example" error="Nie można połączyć z serwerem." onChange={() => {}} />
          <Select
            label="Powtarzanie"
            value={select}
            onValueChange={setSelect}
            options={[
              { value: 'day', label: 'codziennie' },
              { value: 'week', label: 'co tydzień' },
              { value: 'month', label: 'co miesiąc' },
            ]}
          />
        </div>
        <SearchInput value={search} onValueChange={setSearch} shortcut="Mod+K" />
        <SearchInput value={search2} onValueChange={setSearch2} controlSize="lg" busy={false} />
        <TextInput controlSize="sm" placeholder="Mały" />
        <TextArea label="Opis" value={area} onValueChange={setArea} autoGrow />
        <TextArea mono rows={4} defaultValue={'```calc\nnetto = 1200 zł\nbrutto = netto * 1.23\n```'} />
      </Section>

      <Section id="toggles" title="Checkbox & switch" only={only}>
        <div className="b-gallery__grid2">
          <div className="b-gallery__stack">
            <Checkbox label="Pokaż ukończone zadania" checked={checked} onCheckedChange={setChecked} />
            <Checkbox label="Tylko z terminem" description="Ukrywa zadania bez daty." />
            <Checkbox label="Część wybrana" indeterminate />
            <Checkbox label="Niedostępne" disabled checked readOnly />
          </div>
          <div className="b-gallery__stack">
            <Switch checked={sw} onCheckedChange={setSw} label="Podgląd na żywo" description="Formatowanie w trakcie pisania." />
            <Switch checked={sw2} onCheckedChange={setSw2} label="Tryb Vim" icon="keyboard" />
            <Switch checked onCheckedChange={() => {}} label="Wyłączone" disabled />
            <Switch checked={sw} onCheckedChange={setSw} size="sm" aria-label="Mały przełącznik" />
          </div>
        </div>
      </Section>

      <Section id="badges" title="Badges, chips & keys" only={only}>
        <Row label="Badges">
          <Badge>12</Badge>
          <Badge variant="reminder">jutro 9:00</Badge>
          <Badge variant="alarm">7:00</Badge>
          <Badge variant="due">pt 17 paź</Badge>
          <Badge variant="overdue">wczoraj</Badge>
          <Badge variant="tag">praca</Badge>
          <Badge variant="accent">nowe</Badge>
          <Badge variant="success" icon="check">zrobione</Badge>
          <Badge variant="warning">konflikt</Badge>
          <Badge variant="info" size="sm">
            sync
          </Badge>
        </Row>
        <Row label="Chips">
          <Chip selected={filter} onClick={() => setFilter(!filter)} icon="tasks">
            Do zrobienia
          </Chip>
          <Chip icon="calendar">Ten tydzień</Chip>
          {chips.map((c) => (
            <Chip key={c} variant="tag" onRemove={() => setChips(chips.filter((x) => x !== c))}>
              {c}
            </Chip>
          ))}
          <Chip size="sm" disabled>
            Wyłączony
          </Chip>
        </Row>
        <Row label="Keyboard">
          <Kbd keys="Mod+K" apple={false} />
          <Kbd keys="Mod+Shift+F" apple={false} />
          <Kbd keys="Mod+K" apple />
          <Kbd keys="Alt+ArrowLeft" apple={false} />
          <Kbd>Esc</Kbd>
        </Row>
      </Section>

      <Section id="tabs" title="Tabs & segmented control" only={only}>
        <Tabs
          aria-label="Panel boczny"
          value={tab}
          onChange={setTab}
          items={[
            { id: 'backlinks', label: 'Odnośniki', icon: 'link', badge: 4 },
            { id: 'outline', label: 'Spis treści', icon: 'rows' },
            { id: 'calendar', label: 'Kalendarz', icon: 'calendar' },
            { id: 'off', label: 'Wyłączona', disabled: true },
          ]}
        />
        <Tabs
          variant="pill"
          fitted
          aria-label="Widok"
          value={tab}
          onChange={setTab}
          items={[
            { id: 'backlinks', label: 'Odnośniki' },
            { id: 'outline', label: 'Spis' },
            { id: 'calendar', label: 'Kalendarz' },
          ]}
        />
        <Row>
          <SegmentedControl
            aria-label="Zakres"
            value={seg}
            onChange={setSeg}
            options={[
              { value: 'month', label: 'Miesiąc' },
              { value: 'week', label: 'Tydzień' },
              { value: 'agenda', label: 'Agenda' },
            ]}
          />
          <SegmentedControl
            aria-label="Motyw"
            size="sm"
            value="system"
            onChange={() => {}}
            options={[
              { value: 'light', icon: 'sun', ariaLabel: 'Jasny' },
              { value: 'dark', icon: 'moon', ariaLabel: 'Ciemny' },
              { value: 'system', icon: 'monitor', ariaLabel: 'Systemowy' },
            ]}
          />
        </Row>
        <SegmentedControl
          variant="chips"
          size="sm"
          aria-label="Powtarzanie"
          value={seg === 'month' ? 'weekly' : 'none'}
          onChange={() => {}}
          options={[
            { value: 'none', label: 'nie powtarzaj' },
            { value: 'daily', label: 'codziennie' },
            { value: 'weekly', label: 'co tydzień' },
            { value: 'custom', label: 'niestandardowo…' },
          ]}
        />
      </Section>

      <Section id="feedback" title="Spinner, progress, toasts" only={only}>
        <Row>
          <Spinner size={14} />
          <Spinner />
          <Spinner size={24} />
          <span style={{ color: 'var(--accent)' }}>
            <Spinner size={20} label="Synchronizacja" />
          </span>
        </Row>
        <ProgressBar value={0.42} label="Indeksowanie" showLabel valueText="420 / 1000" />
        <ProgressBar value={3} max={4} tone="success" label="Synchronizacja" showLabel />
        <ProgressBar label="Wczytywanie" />
        <ProgressBar value={0.8} tone="reminder" size="sm" label="Pojemność" />
        <div className="b-gallery__toasts">
          <ToastView toast={{ kind: 'info', message: 'Zapisano stronę „Plan tygodnia”.' }} onClose={() => {}} />
          <ToastView
            toast={{ kind: 'success', title: 'Synchronizacja zakończona', message: '12 plików wysłano, 3 pobrano.' }}
            onClose={() => {}}
          />
          <ToastView
            toast={{ kind: 'warning', message: 'Konflikt w „Zakupy.md” — scal ręcznie.', action: { label: 'Scal', onClick: () => {} } }}
            onClose={() => {}}
          />
          <ToastView toast={{ kind: 'error', title: 'Błąd zapisu', message: 'Brak uprawnień do folderu.' }} onClose={() => {}} />
          <ToastView
            toast={{ kind: 'reminder', title: 'Przypomnienie · 09:00', message: 'Zadzwonić do mamy', action: { label: 'Drzemka 10 min', onClick: () => {} } }}
            onClose={() => {}}
          />
        </div>
      </Section>

      <Section id="empty" title="Empty state & tooltip" only={only}>
        <EmptyState
          icon="inbox"
          title="Brak zadań na dziś"
          body="Dodaj zadanie w dzienniku: wpisz „- [ ]” albo naciśnij Ctrl+Enter."
          action={
            <Button variant="primary" icon="plus">
              Nowe zadanie
            </Button>
          }
        />
        <EmptyState size="sm" icon="search" title="Nic nie znaleziono" body="Spróbuj innego słowa." />
        <Row label="Tooltip (forced visible, and on hover)">
          <div className="b-gallery__row" style={{ padding: '2.5rem 0 0 4.5rem' }}>
            <Tooltip content="Nowa strona (Ctrl+N)" className="is-forced">
              <IconButton icon="plus" label="Nowa strona" tooltip={false} variant="secondary" />
            </Tooltip>
            <Tooltip content="Podpowiedź na dole" side="bottom">
              <Button>Najedź</Button>
            </Tooltip>
          </div>
        </Row>
      </Section>

      <Section id="menus" title="Popover & menu" only={only}>
        <Row label="Inline (static) previews">
          <Menu open inline onClose={() => {}} anchor={null} items={MENU_ITEMS} aria-label="Menu strony" />
          <Popover open inline onClose={() => {}} anchor={null} aria-label="Podgląd" style={{ maxWidth: '16rem' }}>
            <div className="b-gallery__stack">
              <strong>Plan tygodnia</strong>
              <span style={{ fontSize: 'var(--text-sm)', color: 'var(--fg-muted)' }}>
                Podgląd strony po najechaniu na link. 3 odnośniki, ostatnia zmiana dziś 09:12.
              </span>
            </div>
          </Popover>
        </Row>
        <Row label="Live (click)">
          <MenuButton label="Akcje" icon="more" items={MENU_ITEMS} />
          <MenuButton label="Więcej" iconOnly items={MENU_ITEMS} variant="secondary" />
          <Button ref={menuAnchor} onClick={() => setMenuOpen((o) => !o)} iconEnd="chevron-down" aria-expanded={menuOpen}>
            Menu (kontrolowane)
          </Button>
          <Menu open={menuOpen} onClose={() => setMenuOpen(false)} anchor={menuAnchor} items={MENU_ITEMS} placement="bottom-end" />
          <Button ref={popAnchor} onClick={() => setPopOpen((o) => !o)} icon="calendar" aria-expanded={popOpen}>
            Wybierz datę
          </Button>
          <Popover open={popOpen} onClose={() => setPopOpen(false)} anchor={popAnchor} aria-label="Wybierz datę">
            <DatePicker
              value={date}
              onChange={setDate}
              now={now}
              marks={MARKS}
              autoFocus="input"
              onCommit={() => setPopOpen(false)}
            />
          </Popover>
        </Row>
      </Section>

      <Section id="dialogs" title="Dialogs" only={only}>
        <Row label="Live">
          <Button onClick={() => setModalOpen(true)}>Otwórz okno</Button>
          <Button variant="danger" onClick={() => setConfirmOpen(true)}>
            Usuń stronę…
          </Button>
          <Button variant="primary" icon="bell" onClick={() => setReminderOpen(true)}>
            Przypomnienie…
          </Button>
          {reminder && (
            <Badge variant={reminder.alarm ? 'alarm' : 'reminder'}>
              {reminder.at}
              {reminder.recurrence ? ` · ${reminder.recurrence}` : ''}
            </Badge>
          )}
        </Row>
        <Modal
          open={modalOpen}
          onClose={() => setModalOpen(false)}
          title="Zmień nazwę strony"
          description="Linki w innych notatkach zostaną zaktualizowane."
          footer={
            <>
              <Button variant="ghost" onClick={() => setModalOpen(false)}>
                Anuluj
              </Button>
              <Button variant="primary" onClick={() => setModalOpen(false)}>
                Zmień nazwę
              </Button>
            </>
          }
        >
          <TextInput label="Nowa nazwa" defaultValue="Plan tygodnia" data-autofocus />
        </Modal>
        <ConfirmDialog
          open={confirmOpen}
          danger
          title="Usunąć „Plan tygodnia”?"
          body="Strona trafi do kosza. Możesz ją przywrócić przez 30 dni."
          confirmLabel="Usuń"
          onConfirm={() => new Promise((r) => setTimeout(r, 600)).then(() => setConfirmOpen(false))}
          onCancel={() => setConfirmOpen(false)}
        />
        <ReminderDialog
          open={reminderOpen}
          onClose={() => setReminderOpen(false)}
          onSubmit={(v) => {
            setReminder(v);
            setReminderOpen(false);
          }}
          now={now}
          marks={MARKS}
          context="Zadzwonić do mamy w sprawie niedzieli"
        />
        <div className="b-gallery__label">Inline previews</div>
        <Modal
          open
          inline
          onClose={() => {}}
          size="sm"
          title="Usunąć „Plan tygodnia”?"
          role="alertdialog"
          footer={
            <>
              <Button variant="ghost">Anuluj</Button>
              <Button variant="danger">Usuń</Button>
            </>
          }
        >
          <p style={{ color: 'var(--fg-muted)' }}>Strona trafi do kosza. Możesz ją przywrócić przez 30 dni.</p>
        </Modal>
        <div className="b-gallery__label">Bottom sheet (phone, 360 px)</div>
        <div className="b-gallery__phone">
          <Modal
            open
            inline
            sheet="always"
            onClose={() => {}}
            title="Nowa notatka"
            footer={
              <>
                <Button variant="ghost">Anuluj</Button>
                <Button variant="primary">Utwórz</Button>
              </>
            }
          >
            <TextInput label="Tytuł" placeholder="np. Pomysły na prezent" />
          </Modal>
        </div>
      </Section>

      <Section id="datepicker" title="Date picker" only={only}>
        <div className="b-gallery__grid2">
          <div className="b-gallery__stack">
            <div className="b-gallery__label">Date + marks · value {date ?? '—'}</div>
            <DatePicker value={date} onChange={setDate} now={now} marks={MARKS} />
          </div>
          <div className="b-gallery__stack">
            <div className="b-gallery__label">With time + typed phrase · value {dateTime ?? '—'}</div>
            <DatePicker value={dateTime} onChange={setDateTime} withTime now={now} marks={MARKS} initialText="jutro o 9" />
          </div>
          <div className="b-gallery__stack">
            <div className="b-gallery__label">Week starts on Sunday, min/max, no text input</div>
            <DatePicker
              value="2026-10-20"
              onChange={() => {}}
              weekStartsOn={0}
              min="2026-10-05"
              max="2026-10-25"
              naturalInput={false}
              now={now}
            />
          </div>
          <div className="b-gallery__stack">
            <div className="b-gallery__label">Unrecognised phrase</div>
            <DatePicker value={null} onChange={() => {}} now={now} initialText="kiedyś tam" withTime />
          </div>
        </div>
      </Section>

      <Section id="reminder" title="Reminder dialog" only={only}>
        <ReminderDialog
          open
          inline
          onClose={() => {}}
          onSubmit={() => {}}
          onRemove={() => {}}
          now={now}
          marks={MARKS}
          initial={{ at: '2026-10-15T09:00', alarm: true, recurrence: 'every week on Thursday' }}
          context="Basen z Zosią"
        />
      </Section>

      <Section id="diff" title="Diff view" only={only}>
        <DiffView
          left={DIFF_LEFT}
          right={DIFF_RIGHT}
          leftLabel="Wersja z 12 paź 18:04"
          rightLabel="Bieżąca"
          mode={diffMode}
          onModeChange={setDiffMode}
          context={2}
        />
        <DiffView left={DIFF_LEFT} right={DIFF_RIGHT} leftLabel="Przed" rightLabel="Po" mode="unified" context={1} />
        <DiffView left="Bez zmian" right="Bez zmian" leftLabel="A" rightLabel="B" mode="split" />
      </Section>

      <Section id="merge" title="Merge view" only={only}>
        <MergeView hunks={HUNKS} onResolve={setMerged} onCancel={() => setMerged(null)} />
        {merged !== null && (
          <pre style={{ fontSize: 'var(--text-xs)', background: 'var(--bg-sunken)', padding: 'var(--space-3)', borderRadius: 'var(--radius)' }}>
            {merged}
          </pre>
        )}
      </Section>

      <Section id="layout" title="Splitter & virtual list" only={only}>
        <div className="b-gallery__box">
          <Splitter defaultSize={180} min={120} max={320} storageKey="gallery">
            <div className="b-gallery__pane">Lewy panel (przeciągnij lub użyj strzałek na pasku)</div>
            <Splitter direction="vertical" fixed="second" defaultSize={90} min={48} max={160}>
              <div className="b-gallery__pane">Treść</div>
              <div className="b-gallery__pane">Dolny panel</div>
            </Splitter>
          </Splitter>
        </div>
        <div className="b-gallery__box">
          <VirtualList
            items={LIST_ITEMS}
            rowHeight={36}
            getKey={(it) => it.id}
            aria-label="Wszystkie strony"
            role="list"
            rowRole="listitem"
            renderRow={(it) => (
              <div className="b-gallery__vrow">
                <span style={{ color: 'var(--fg-faint)' }}>#{it.id + 1}</span>
                {it.title}
              </div>
            )}
          />
        </div>
      </Section>
    </>
  );
}

export function Gallery({ initialTheme, initialLocale }: { initialTheme?: ThemeMode; initialLocale?: Locale }) {
  const p = useMemo(params, []);
  const [theme, setTheme] = useState<ThemeMode>(initialTheme ?? ((p.get('theme') as ThemeMode | null) ?? 'both'));
  const [locale, setLocale] = useState<Locale>(initialLocale ?? (p.get('locale') === 'en' ? 'en' : 'pl'));
  const [font, setFont] = useState<'serif' | 'sans'>(p.get('font') === 'sans' ? 'sans' : 'serif');
  const only = p.get('section');
  const now = p.get('now') === 'real' ? new Date() : FIXED_NOW;

  // The document theme drives portals (live dialogs, popovers outside a themed column).
  useEffect(() => {
    const root = document.documentElement;
    root.dataset.theme = theme === 'both' ? 'light' : theme;
    root.dataset.editorFont = font;
    root.lang = locale;
  }, [theme, font, locale]);

  const columns: { theme: 'light' | 'dark' | 'system'; label?: string }[] =
    theme === 'both' ? [{ theme: 'light', label: 'Light' }, { theme: 'dark', label: 'Dark' }] : [{ theme }];

  return (
    <LocaleContext.Provider value={locale}>
      <style>{GALLERY_CSS}</style>
      <div className="b-gallery">
        <header className="b-gallery__bar">
          <h1>Brulion · components</h1>
          <SegmentedControl
            size="sm"
            aria-label="Theme"
            value={theme}
            onChange={setTheme}
            options={[
              { value: 'light', icon: 'sun', ariaLabel: 'Light', title: 'Light' },
              { value: 'dark', icon: 'moon', ariaLabel: 'Dark', title: 'Dark' },
              { value: 'both', icon: 'columns', ariaLabel: 'Side by side', title: 'Side by side' },
              { value: 'system', icon: 'monitor', ariaLabel: 'System', title: 'System' },
            ]}
          />
          <SegmentedControl
            size="sm"
            aria-label="Language"
            value={locale}
            onChange={setLocale}
            options={[
              { value: 'pl', label: 'PL' },
              { value: 'en', label: 'EN' },
            ]}
          />
          <SegmentedControl
            size="sm"
            aria-label="Editor font"
            value={font}
            onChange={setFont}
            options={[
              { value: 'serif', label: 'Serif' },
              { value: 'sans', label: 'Sans' },
            ]}
          />
          <Select
            controlSize="sm"
            aria-label="Section"
            value={only ?? ''}
            onValueChange={(v) => {
              const q = new URLSearchParams(location.search);
              if (v) q.set('section', v);
              else q.delete('section');
              location.search = q.toString();
            }}
            options={[{ value: '', label: 'All sections' }, ...SECTIONS.map(([id, title]) => ({ value: id, label: title }))]}
          />
        </header>
        <div className="b-gallery__cols">
          {columns.map((c) => (
            <div key={c.theme} className="b-gallery__col" data-theme={c.theme} data-theme-scope="" data-editor-font={font}>
              <Column only={only} now={now} label={c.label} />
            </div>
          ))}
        </div>
      </div>
    </LocaleContext.Provider>
  );
}

/** Mount the gallery into `el`; returns an unmount function. */
export function mountGallery(el: HTMLElement): () => void {
  const root = createRoot(el);
  root.render(
    <StrictMode>
      <Gallery />
    </StrictMode>,
  );
  return () => root.unmount();
}
