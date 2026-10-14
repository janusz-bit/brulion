/**
 * App state (zustand), see docs/UI.md "Store".
 *
 * Navigation is a stack of {@link Route}s with back/forward like a browser. When bound to the
 * browser history ({@link bindBrowserHistory}), every `navigate()` pushes a history entry, so
 * the Android back gesture and the browser's back/forward buttons move through the stack
 * (`popstate`); `goBack()` from the UI goes through `history.back()` for the same reason.
 *
 * Use the hooks in ./hooks.ts in components; non-React code calls `useAppStore.getState()`.
 */
import { create } from 'zustand';
import { detectLocale } from '@/core/i18n';
import type { SyncStatus } from '@/core/sync';
import type { LocalDate, Locale, Platform, VaultPath } from '@/core/types';
import type { ToastData } from '@/app/components';
import type { Workspace } from '../services/workspace';
import { DEFAULT_SETTINGS, normalizeSettings, type DeviceSettings, type SettingsPersistence, type ThemeSetting } from './settings';

export type { DeviceSettings, ThemeSetting } from './settings';

// ---------------------------------------------------------------------------------------------
// Routes
// ---------------------------------------------------------------------------------------------

export type Route =
  | { view: 'journals' }
  | { view: 'page'; path: VaultPath; line?: number; blockId?: string }
  | { view: 'calendar'; date?: LocalDate; mode?: 'month' | 'week' | 'agenda' }
  | { view: 'tasks' }
  | { view: 'graph'; path?: VaultPath }
  | { view: 'search'; q?: string }
  | { view: 'pages' }
  | { view: 'tags'; tag?: string }
  | { view: 'sync' }
  | { view: 'import-export' }
  | { view: 'history'; path: VaultPath }
  | { view: 'trash' }
  | { view: 'settings'; section?: string };

export type ViewName = Route['view'];
export type RouteOf<V extends ViewName> = Extract<Route, { view: V }>;

/** Props every view component receives (App.tsx renders `<View route={route} pane="main" />`). */
export interface ViewProps<V extends ViewName = ViewName> {
  route: RouteOf<V>;
  /** 'side' when rendered in the desktop right side panel (narrower, no autofocus). */
  pane?: 'main' | 'side';
}

export const VIEW_NAMES: readonly ViewName[] = [
  'journals',
  'page',
  'calendar',
  'tasks',
  'graph',
  'search',
  'pages',
  'tags',
  'sync',
  'import-export',
  'history',
  'trash',
  'settings',
];

export const HOME_ROUTE: Route = { view: 'journals' };

/** Stable string for a route (scroll positions, equality). Line/block targets are ignored. */
export function routeKey(r: Route): string {
  switch (r.view) {
    case 'page':
    case 'history':
      return `${r.view}:${r.path}`;
    case 'calendar':
      return `calendar:${r.date ?? ''}:${r.mode ?? ''}`;
    case 'graph':
      return `graph:${r.path ?? ''}`;
    case 'search':
      return `search:${r.q ?? ''}`;
    case 'tags':
      return `tags:${r.tag ?? ''}`;
    case 'settings':
      return 'settings';
    default:
      return r.view;
  }
}

/** Same destination including line/block targets. */
export function sameRoute(a: Route, b: Route): boolean {
  if (routeKey(a) !== routeKey(b)) return false;
  if (a.view === 'page' && b.view === 'page') return a.line === b.line && a.blockId === b.blockId;
  if (a.view === 'settings' && b.view === 'settings') return a.section === b.section;
  return true;
}

/** Validate a route read from storage / a URL. */
export function parseRoute(v: unknown): Route | null {
  if (typeof v !== 'object' || v === null) return null;
  const r = v as Record<string, unknown>;
  const s = (k: string): string | undefined => (typeof r[k] === 'string' && r[k] !== '' ? (r[k] as string) : undefined);
  switch (r.view) {
    case 'journals':
    case 'tasks':
    case 'pages':
    case 'sync':
    case 'import-export':
    case 'trash':
      return { view: r.view };
    case 'page': {
      const path = s('path');
      if (!path) return null;
      const out: Route = { view: 'page', path };
      if (typeof r.line === 'number' && Number.isInteger(r.line) && r.line >= 0) out.line = r.line;
      const blockId = s('blockId');
      if (blockId) out.blockId = blockId;
      return out;
    }
    case 'history': {
      const path = s('path');
      return path ? { view: 'history', path } : null;
    }
    case 'calendar': {
      const out: Route = { view: 'calendar' };
      const date = s('date');
      if (date && /^\d{4}-\d{2}-\d{2}$/.test(date)) out.date = date;
      if (r.mode === 'month' || r.mode === 'week' || r.mode === 'agenda') out.mode = r.mode;
      return out;
    }
    case 'graph': {
      const path = s('path');
      return path ? { view: 'graph', path } : { view: 'graph' };
    }
    case 'search': {
      const q = s('q');
      return q ? { view: 'search', q } : { view: 'search' };
    }
    case 'tags': {
      const tag = s('tag');
      return tag ? { view: 'tags', tag } : { view: 'tags' };
    }
    case 'settings': {
      const section = s('section');
      return section ? { view: 'settings', section } : { view: 'settings' };
    }
    default:
      return null;
  }
}

// ---------------------------------------------------------------------------------------------
// Store
// ---------------------------------------------------------------------------------------------

export type AppStatus = 'boot' | 'onboarding' | 'loading' | 'ready' | 'error';

export interface Toast extends ToastData {
  id: string;
  /** Auto-dismiss after ms; 0 = stays until closed. */
  timeout?: number;
}

export type ToastInput = Omit<Toast, 'id'> & { id?: string };

/** Dialogs owned by the shell (src/app/shell/Dialogs.tsx). */
export type ShellDialog =
  | { kind: 'new-page'; folder?: string; title?: string; newPane?: boolean }
  | { kind: 'rename'; path: VaultPath }
  | { kind: 'delete'; path: VaultPath }
  | { kind: 'switch-vault' };

export interface IndexProgress {
  indexed: number;
  total: number;
}

export const MAX_HISTORY = 100;

export interface AppState {
  status: AppStatus;
  error?: string;
  platform: Platform | null;
  /** The open vault (null before onboarding / while switching). */
  workspace: Workspace | null;
  /** Bumps on every index / file change (subscribe for re-render). */
  indexVersion: number;
  /** Initial indexing progress; null when done. */
  indexProgress: IndexProgress | null;
  locale: Locale;
  theme: ThemeSetting;
  settings: DeviceSettings;
  route: Route;
  back: Route[];
  forward: Route[];
  /** Route shown in the desktop right side panel; null = the default panel (backlinks/outline/calendar). */
  sidePanel: Route | null;
  sidePanelOpen: boolean;
  /** Desktop: left sidebar visible. */
  sidebarOpen: boolean;
  /** Mobile: the sidebar drawer is open. */
  drawerOpen: boolean;
  paletteOpen: boolean;
  switcherOpen: boolean;
  /** Quick capture dialog. */
  captureOpen: boolean;
  dialog: ShellDialog | null;
  toasts: Toast[];
  syncStatus: SyncStatus;

  navigate(r: Route, opts?: { replace?: boolean; newPane?: boolean }): void;
  goBack(): void;
  goForward(): void;
  /** Show `r` in the side panel (desktop); null closes the panel. */
  openSidePanel(r: Route | null): void;
  /** Open/close the default side panel. */
  toggleSidePanel(): void;
  setSidebarOpen(open: boolean): void;
  toggleSidebar(): void;
  setDrawerOpen(open: boolean): void;
  setPaletteOpen(open: boolean): void;
  setSwitcherOpen(open: boolean): void;
  setCaptureOpen(open: boolean): void;
  openDialog(d: ShellDialog): void;
  closeDialog(): void;
  /** Show a toast; returns its id (pass `id` to replace an existing toast). */
  toast(t: ToastInput): string;
  dismissToast(id: string): void;
  updateSettings(patch: Partial<DeviceSettings> | ((s: DeviceSettings) => DeviceSettings)): void;
  setTheme(theme: ThemeSetting): void;
  setLocale(locale: Locale): void;

  // Shell internals (boot / workspace wiring).
  setStatus(status: AppStatus, error?: string): void;
  setPlatform(p: Platform | null): void;
  setWorkspace(ws: Workspace | null): void;
  bumpIndex(): void;
  setIndexProgress(p: IndexProgress | null): void;
  setSyncStatus(s: SyncStatus): void;
  /** Settings loaded at boot (replaces without persisting) + where to persist changes. */
  initSettings(settings: DeviceSettings, persistence: SettingsPersistence | null): void;
  /** Close palette, switcher, capture dialog, shell dialog and drawer; true if something was open. */
  closeOverlays(): boolean;
}

/** Hooks into the browser history (set by bindBrowserHistory). */
interface HistoryHook {
  push(depth: number): void;
  replace(depth: number): void;
  back(): void;
}

let historyHook: HistoryHook | null = null;
let persistence: SettingsPersistence | null = null;
let toastSeq = 0;
const toastTimers = new Map<string, ReturnType<typeof setTimeout>>();

function defaultTimeout(t: ToastInput): number {
  if (t.timeout !== undefined) return t.timeout;
  if (t.kind === 'reminder') return 0;
  if (t.kind === 'error') return 9000;
  return t.action ? 8000 : 5000;
}

/** State before any boot step (also used by tests to reset). */
export function initialState(): Pick<
  AppState,
  | 'status'
  | 'platform'
  | 'workspace'
  | 'indexVersion'
  | 'indexProgress'
  | 'locale'
  | 'theme'
  | 'settings'
  | 'route'
  | 'back'
  | 'forward'
  | 'sidePanel'
  | 'sidePanelOpen'
  | 'sidebarOpen'
  | 'drawerOpen'
  | 'paletteOpen'
  | 'switcherOpen'
  | 'captureOpen'
  | 'dialog'
  | 'toasts'
  | 'syncStatus'
> {
  return {
    status: 'boot',
    platform: null,
    workspace: null,
    indexVersion: 0,
    indexProgress: null,
    locale: detectLocale(),
    theme: DEFAULT_SETTINGS.theme,
    settings: normalizeSettings({}),
    route: HOME_ROUTE,
    back: [],
    forward: [],
    sidePanel: null,
    sidePanelOpen: false,
    sidebarOpen: true,
    drawerOpen: false,
    paletteOpen: false,
    switcherOpen: false,
    captureOpen: false,
    dialog: null,
    toasts: [],
    syncStatus: { state: 'disabled' },
  };
}

export const useAppStore = create<AppState>()((set, get) => ({
  ...initialState(),

  navigate(r, opts = {}) {
    const s = get();
    if (opts.newPane) {
      s.openSidePanel(r);
      return;
    }
    const patch: Partial<AppState> = { drawerOpen: false, paletteOpen: false, switcherOpen: false };
    if (opts.replace) {
      set({ ...patch, route: r });
      historyHook?.replace(s.back.length);
      return;
    }
    if (sameRoute(s.route, r)) {
      set(patch);
      return;
    }
    const back = [...s.back, s.route].slice(-MAX_HISTORY);
    set({ ...patch, route: r, back, forward: [] });
    historyHook?.push(back.length);
  },

  goBack() {
    if (get().back.length === 0) return;
    if (historyHook) historyHook.back();
    else popBack();
  },

  goForward() {
    if (get().forward.length === 0) return;
    popForward();
    historyHook?.push(get().back.length);
  },

  openSidePanel(r) {
    set({ sidePanel: r, sidePanelOpen: r !== null });
  },

  toggleSidePanel() {
    const s = get();
    set(s.sidePanelOpen ? { sidePanelOpen: false, sidePanel: null } : { sidePanelOpen: true, sidePanel: null });
  },

  setSidebarOpen(open) {
    set({ sidebarOpen: open });
    get().updateSettings({ sidebarVisible: open });
  },
  toggleSidebar() {
    get().setSidebarOpen(!get().sidebarOpen);
  },
  setDrawerOpen(open) {
    set({ drawerOpen: open });
  },
  setPaletteOpen(open) {
    set(open ? { paletteOpen: true, switcherOpen: false } : { paletteOpen: false });
  },
  setSwitcherOpen(open) {
    set(open ? { switcherOpen: true, paletteOpen: false } : { switcherOpen: false });
  },
  setCaptureOpen(open) {
    set(open ? { captureOpen: true, paletteOpen: false, switcherOpen: false } : { captureOpen: false });
  },

  openDialog(d) {
    set({ dialog: d, paletteOpen: false, switcherOpen: false, captureOpen: false, drawerOpen: false });
  },
  closeDialog() {
    set({ dialog: null });
  },

  toast(t) {
    const id = t.id ?? `t${++toastSeq}`;
    const toast: Toast = { ...t, id, timeout: defaultTimeout(t) };
    set((s) => {
      const i = s.toasts.findIndex((x) => x.id === id);
      const toasts = i === -1 ? [...s.toasts, toast].slice(-5) : s.toasts.map((x) => (x.id === id ? toast : x));
      return { toasts };
    });
    const old = toastTimers.get(id);
    if (old) clearTimeout(old);
    toastTimers.delete(id);
    if (toast.timeout && toast.timeout > 0) {
      toastTimers.set(
        id,
        setTimeout(() => get().dismissToast(id), toast.timeout),
      );
    }
    return id;
  },

  dismissToast(id) {
    const timer = toastTimers.get(id);
    if (timer) clearTimeout(timer);
    toastTimers.delete(id);
    set((s) => ({ toasts: s.toasts.filter((x) => x.id !== id) }));
  },

  updateSettings(patch) {
    const prev = get().settings;
    const next = typeof patch === 'function' ? patch(prev) : { ...prev, ...patch };
    if (next === prev) return;
    set({ settings: next, theme: next.theme, locale: next.locale ?? get().locale });
    persistence?.save(next);
  },

  setTheme(theme) {
    get().updateSettings({ theme });
  },

  setLocale(locale) {
    get().updateSettings({ locale });
  },

  setStatus(status, error) {
    set({ status, error });
  },
  setPlatform(p) {
    set({ platform: p });
  },
  setWorkspace(ws) {
    set({ workspace: ws, indexVersion: get().indexVersion + 1 });
  },
  bumpIndex() {
    set((s) => ({ indexVersion: s.indexVersion + 1 }));
  },
  setIndexProgress(p) {
    set({ indexProgress: p });
  },
  setSyncStatus(syncStatus) {
    set({ syncStatus });
  },

  initSettings(settings, p) {
    persistence = p;
    set({
      settings,
      theme: settings.theme,
      locale: settings.locale ?? detectLocale(),
      sidebarOpen: settings.sidebarVisible,
    });
  },

  closeOverlays() {
    const s = get();
    const open = s.paletteOpen || s.switcherOpen || s.captureOpen || s.drawerOpen || s.dialog !== null;
    if (open) set({ paletteOpen: false, switcherOpen: false, captureOpen: false, drawerOpen: false, dialog: null });
    return open;
  },
}));

/** Pop one route from the back stack (no history interaction). */
function popBack(): void {
  const s = useAppStore.getState();
  const prev = s.back[s.back.length - 1];
  if (!prev) return;
  useAppStore.setState({
    route: prev,
    back: s.back.slice(0, -1),
    forward: [s.route, ...s.forward].slice(0, MAX_HISTORY),
    drawerOpen: false,
  });
}

/** Move one route forward (no history interaction). */
function popForward(): void {
  const s = useAppStore.getState();
  const next = s.forward[0];
  if (!next) return;
  useAppStore.setState({ route: next, back: [...s.back, s.route].slice(-MAX_HISTORY), forward: s.forward.slice(1), drawerOpen: false });
}

/** The parts of `window` the history binding uses. */
export interface HistoryWindow {
  history: Pick<History, 'pushState' | 'replaceState' | 'back' | 'state'>;
  addEventListener(type: 'popstate', cb: (e: PopStateEvent) => void): void;
  removeEventListener(type: 'popstate', cb: (e: PopStateEvent) => void): void;
}

const STATE_KEY = 'brulion';

function depthOf(state: unknown): number | null {
  if (typeof state !== 'object' || state === null) return null;
  const d = (state as Record<string, unknown>)[STATE_KEY];
  return typeof d === 'number' && Number.isInteger(d) && d >= 0 ? d : null;
}

/**
 * Keep the navigation stack in sync with the browser history (Android back gesture, browser
 * back/forward). A back step while an overlay (palette, drawer, ...) is open only closes it.
 * Returns an unbind function.
 */
export function bindBrowserHistory(win: HistoryWindow = window): () => void {
  const h = win.history;
  const push = (depth: number) => {
    try {
      h.pushState({ [STATE_KEY]: depth }, '');
    } catch (e) {
      console.warn('[nav] pushState failed', e);
    }
  };
  const replace = (depth: number) => {
    try {
      h.replaceState({ [STATE_KEY]: depth }, '');
    } catch (e) {
      console.warn('[nav] replaceState failed', e);
    }
  };
  historyHook = { push, replace, back: () => h.back() };
  replace(useAppStore.getState().back.length);

  const onPop = (e: PopStateEvent) => {
    const store = useAppStore.getState();
    const current = store.back.length;
    const target = depthOf(e.state);
    if (target === null) {
      // An entry from before the app started (or another page state): stay where we are.
      return;
    }
    if (target < current && store.closeOverlays()) {
      // Back closes the open overlay; restore the entry we just left.
      push(current);
      return;
    }
    if (target < current) for (let i = current - target; i > 0 && useAppStore.getState().back.length > 0; i--) popBack();
    else if (target > current) for (let i = target - current; i > 0 && useAppStore.getState().forward.length > 0; i--) popForward();
  };
  win.addEventListener('popstate', onPop);
  return () => {
    win.removeEventListener('popstate', onPop);
    historyHook = null;
  };
}

/** Reset the store (tests). */
export function resetAppStore(): void {
  for (const t of toastTimers.values()) clearTimeout(t);
  toastTimers.clear();
  historyHook = null;
  persistence = null;
  useAppStore.setState(initialState());
}
