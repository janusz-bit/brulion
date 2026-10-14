# Brulion — UI architecture (src/app)

React 19 + zustand + CodeMirror 6. One codebase for desktop (Tauri, wide window), Android
(Tauri, phone) and browser (PWA, any width). Layout adapts by width, not by platform.

## Directory layout and ownership

```
src/main.tsx                     bootstrap: platform → workspace → <App/> (shell agent)
src/app/
  App.tsx                        top-level layout + view router (shell)
  state/store.ts                 zustand store (shell) — see "Store" below
  state/hooks.ts                 useT(), useIndexVersion(), useNote(path), usePlatform() (shell)
  services/workspace.ts          Workspace service (shell) — see below
  services/commands.ts           command registry used by palette, shortcuts, menus (shell)
  shell/                         Sidebar, TopBar, MobileNav, CommandPalette, QuickSwitcher,
                                 Toasts, Onboarding (vault picker), SettingsView, Dialogs (shell)
  editor/                        CodeMirror editor component + extensions (editor agents)
    NoteEditor.tsx               <NoteEditor path=... /> (editor-core)
    context.ts                   EditorContext type (shell defines; editors consume)
    setup.ts, outliner.ts, autocomplete.ts, slash.ts, paste.ts, keymap.ts (editor-core)
    preview/*.ts                 live-preview decorations & widgets (editor-preview)
    MobileToolbar.tsx            formatting/outliner toolbar above the keyboard (editor-core)
  views/                         full-screen views (views agents)
    JournalsView.tsx, PageView.tsx, CalendarView.tsx, TasksView.tsx, GraphView.tsx,
    SearchView.tsx, AllPagesView.tsx, TagsView.tsx, SyncView.tsx, ImportExportView.tsx,
    HistoryView.tsx, TrashView.tsx
  components/                    shared small components: Icon, Button, Modal, Popover,
                                 DatePicker, ReminderDialog, RenderedMarkdown, Backlinks,
                                 DiffView, EmptyState, Spinner (first agent that needs one
                                 creates it; keep them generic)
  styles/
    theme.css                    CSS variables (light/dark), typography (shell)
    app.css                      layout (shell); editor.css (editor agents); views.css (views)
```

## Routing

No URL router. The store keeps a navigation stack of `Route` objects (back/forward like a
browser, Android back button/gesture pops the stack):

```ts
type Route =
  | { view: 'journals' }                                  // home: today + past days
  | { view: 'page'; path: VaultPath; line?: number; blockId?: string }
  | { view: 'calendar'; date?: LocalDate; mode?: 'month' | 'week' | 'agenda' }
  | { view: 'tasks' } | { view: 'graph'; path?: VaultPath } | { view: 'search'; q?: string }
  | { view: 'pages' } | { view: 'tags'; tag?: string } | { view: 'sync' }
  | { view: 'import-export' } | { view: 'history'; path: VaultPath } | { view: 'trash' }
  | { view: 'settings'; section?: string };
```
`App.tsx` maps `route.view` → lazily-loaded view component (`React.lazy`), each receiving the
route as props. Opening a link = `store.navigate(route)`; Ctrl/⌘-click = open in the right
sidebar ("side panel", desktop only).

## Store (zustand, `src/app/state/store.ts`)

```ts
interface AppState {
  status: 'boot' | 'onboarding' | 'loading' | 'ready' | 'error';
  error?: string;
  platform: Platform | null;
  workspace: Workspace | null;            // the open vault (null before onboarding)
  indexVersion: number;                   // bumps on every index change (subscribe for re-render)
  locale: Locale; theme: 'system' | 'light' | 'dark';
  settings: DeviceSettings;               // persisted per device (LocalStore 'settings')
  route: Route; back: Route[]; forward: Route[];
  sidePanel: Route | null;                // desktop right panel
  sidebarOpen: boolean;                   // left sidebar / mobile drawer
  paletteOpen: boolean; switcherOpen: boolean;
  toasts: Toast[];
  syncStatus: SyncStatus;                 // from SyncScheduler
  // actions
  navigate(r: Route, opts?: { replace?: boolean }): void; goBack(): void; goForward(): void;
  openSidePanel(r: Route | null): void; toast(t: Omit<Toast, 'id'>): void; ...
}
interface DeviceSettings {
  vaults: { location: VaultLocation; lastOpened: number }[];
  currentVault?: VaultLocation;
  locale?: Locale; theme: 'system' | 'light' | 'dark'; fontSize: number;
  editor: { livePreview: boolean; vimMode: boolean; spellcheck: boolean; lineWidth: number; outlinerMode: boolean };
  nextcloud?: { server: string; username: string; password: string; folder: string; enabled: boolean; intervalMin: number };
  syncthing?: { url: string; apiKey: string; folderId?: string; enabled: boolean };
  reminders: { enabled: boolean; sound: boolean; horizonDays: number };
  closeToTray: boolean;
}
```

## Workspace service (`src/app/services/workspace.ts`)

Glues core modules for one open vault. Created by `openWorkspace(platform, location)`.

```ts
class Workspace {
  readonly vault: Vault; readonly index: VaultIndex; readonly history: History;
  readonly store: LocalStore; readonly platform: Platform;
  readonly reminders: ReminderScheduler; readonly sync: SyncScheduler | null;
  readonly config: VaultConfig;
  ready: Promise<void>;                                  // initial indexing done
  progress: { indexed: number; total: number };
  // reads
  read(path): Promise<string>;
  assetUrl(path): string | null;                         // tauri convertFileSrc / blob URL cache
  // page operations (all update files through the vault; index follows via onChange)
  openJournal(date: LocalDate): Promise<VaultPath>;      // creates from template if missing
  createPage(title: string, folder?: string, content?: string): Promise<VaultPath>;
  resolveOrCreate(target: string, from?: VaultPath): Promise<VaultPath>;
  renamePage(path, newTitleOrPath): Promise<VaultPath>;  // + rewrites links in other files
  deletePage(path): Promise<void>; restore(trashPath): Promise<VaultPath>;
  write(path, content): Promise<void>;                   // debounced saves from editors go here
  toggleTask(path, line): Promise<void>; setTaskStatus(path, line, status): Promise<void>;
  ensureBlockId(path, line): Promise<string>;
  addReminder(path, line, at: LocalDateTime, alarm: boolean): Promise<void>;
  quickCapture(text: string, opts?: { reminderAt?: LocalDateTime }): Promise<void>; // → today's journal
  saveAttachment(name: string, data: Uint8Array): Promise<VaultPath>;
  importFiles(importer, files, targetFolder): Promise<ImportReport>;
  exportFiles(exporterId, scope: { paths?: VaultPath[] }): Promise<ExportedFile>;
  configureSync(settings): void; syncNow(): Promise<SyncReport | null>;
  conflicts(): ConflictInfo[];                           // Syncthing + Brulion conflict copies
  resolveConflict(original, copy, mergedContent): Promise<void>;
  close(): Promise<void>;
}
```

The editor never writes files directly: `NoteEditor` calls `workspace.write(path, text)`
debounced (~400 ms) and on blur/unmount. When the file changes externally (sync / Syncthing /
another app) and the editor has no unsaved local edits, the editor replaces its doc with a
minimal diff transaction (keeps cursor); if it has unsaved edits, a 3-way merge
(`merge3` from `@/core/sync`) with the last saved text as base is applied.

## EditorContext (`src/app/editor/context.ts`)

Passed into CodeMirror extensions via a StateField/Facet so widgets can access the vault:

```ts
interface EditorContext {
  path: VaultPath;
  workspace: Workspace;
  t: Translator; locale: Locale; now(): Date;
  navigate(r: Route, opts?: { newPane?: boolean }): void;
  openLink(target: string, opts?: { newPane?: boolean }): void;   // resolves or creates page
  assetUrl(path: VaultPath): string | null;
}
```

## Look & feel

* Calm, typographic, paper-like light theme and a deep blue-grey dark theme (matches logo:
  `#1f2430` ink, `#fbf7ee` paper, `#e2574c` margin red accent, `#f2b33d` amber for reminders,
  `#9fc3e6` grid blue). CSS variables in `theme.css`; `prefers-color-scheme` + manual override.
* Font: system UI stack for UI; editor uses a readable serif/sans option (setting).
* Bullets in outliner mode render as dots with a vertical guide line per nesting level, fold
  arrows on hover, like Logseq. Clicking a bullet zooms into the block.
* Mobile (< 768 px): bottom navigation (Dziennik, Kalendarz, Szukaj, Zadania, Menu), the
  sidebar is a drawer, editor toolbar above the keyboard (indent, outdent, todo, date,
  reminder, link, undo), large touch targets, Android back = `goBack()`.
* Desktop: left sidebar (navigation, favourites, recent, file tree, tags), main pane, optional
  right side panel (backlinks/outline/calendar mini or a second note).
* Keyboard: Ctrl+K palette, Ctrl+O quick switcher, Ctrl+Shift+F search, Alt+←/→ back/forward,
  Ctrl+Enter toggle task, Tab/Shift+Tab indent, Alt+↑/↓ move block, Ctrl+. fold, Ctrl+J today.
* Accessibility: semantic buttons, focus rings, aria labels (localised), reduced motion.

## `<NoteEditor>` contract (`src/app/editor/NoteEditor.tsx`)

```ts
interface NoteEditorProps {
  path: VaultPath;
  /** Scroll to / place cursor on this 0-based line or block id after load. */
  line?: number; blockId?: string;
  /** Outliner zoom: show only the list item at this line and its children. */
  zoomLine?: number;
  /** Journal stream / embeds: no title, auto height (no inner scroll), lighter chrome. */
  compact?: boolean;
  autoFocus?: boolean; readOnly?: boolean;
  /** Placeholder for empty notes (e.g. "Zacznij pisać…"). */
  placeholder?: string;
}
```
The component loads content via `workspace.read(path)`, saves via `workspace.write`, follows
external changes (index/vault change events) as described above, and registers itself so the
command palette can act on the focused editor (`useFocusedEditor()` in `state/hooks.ts`).

## Platform integration details

* Back navigation: every `navigate()` pushes `history.pushState({ i })`; `popstate` → `goBack()`.
  This makes the Android back gesture and browser back button work.
* PWA share target: `?share-target&title=&text=&url=` on startup → `workspace.quickCapture()`.
* Notification tap (`platform.notifications.onActivate`) → `navigate({ view: 'page', path, line })`.
* Tauri: after opening a vault call `invoke('allow_vault_assets', { root })`; `assetUrl()` uses
  `convertFileSrc(root + '/' + path)`. Browser: blob URLs cached per path (revoked on change).
* Desktop close-to-tray setting → `invoke('set_close_to_tray', { enabled })`.
