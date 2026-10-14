/**
 * App controller: startup (platform → settings → vault → UI), opening / switching vaults and
 * the glue between the store, the Workspace and the platform:
 *
 * - settings side effects (theme, language, font, close-to-tray, sync, reminders),
 * - browser history (Android back gesture), recent pages, route restore after reload,
 * - notifications (in-app reminders, taps → note), PWA updates, share target,
 * - Tauri desktop quit handshake (flush pending writes, then quit).
 */
import { create } from 'zustand';
import { translatorFor } from '@/app/components';
import { LocalStore } from '@/core/storage';
import type { Platform, VaultLocation } from '@/core/types';
import { desktopShell, getPlatform, isPlatformError } from '@/platform';
import {
  forgetVault,
  pushRecent,
  readBootSettings,
  rememberVault,
  renamePathInLists,
  sameLocation,
  SETTINGS_NAMESPACE,
  SettingsPersistence,
} from '../state/settings';
import { bindBrowserHistory, HOME_ROUTE, parseRoute, routeKey, useAppStore, type Route } from '../state/store';
import { registerBuiltinCommands } from './commands';
import { DEMO_VAULT_ID, seedDemoVault } from './demo-vault';
import { flushPendingActivation, wireNotifications } from './notifications';
import { registerServiceWorker } from './pwa';
import { applyDocumentSettings } from './theme';
import { composeSharedText, parseShareTarget, stripShareParams, type SharedContent } from './share-target';
import { openWorkspace, type Workspace } from './workspace';

export { applyDocumentSettings, resolveTheme } from './theme';

// ---------------------------------------------------------------------------------------------
// Boot state shown by the onboarding screen
// ---------------------------------------------------------------------------------------------

export type BootIssue =
  /** A folder vault (File System Access) needs permission again: needs a click. */
  | { kind: 'permission'; location: VaultLocation }
  /** Android: the vault is on shared storage and "All files access" is missing. */
  | { kind: 'storage-access'; location: VaultLocation }
  /** Opening failed. */
  | { kind: 'error'; location: VaultLocation; message: string };

interface BootState {
  issue: BootIssue | null;
  /** The onboarding screen was opened on purpose ("Zmień skarbiec"), not because nothing is open. */
  switching: boolean;
}

export const useBootStore = create<BootState>()(() => ({ issue: null, switching: false }));

// ---------------------------------------------------------------------------------------------
// Startup
// ---------------------------------------------------------------------------------------------

let pendingShare: SharedContent | null = null;
let workspaceOffs: (() => void)[] = [];
let booted = false;

const ROUTE_KEY = 'brulion.route';

function saveRoute(key: string, route: Route): void {
  try {
    sessionStorage.setItem(ROUTE_KEY, JSON.stringify({ key, route }));
  } catch {
    // ignore
  }
}

function restoredRoute(key: string): Route | null {
  try {
    const raw = sessionStorage.getItem(ROUTE_KEY);
    if (!raw) return null;
    const v = JSON.parse(raw) as { key?: unknown; route?: unknown };
    return v.key === key ? parseRoute(v.route) : null;
  } catch {
    return null;
  }
}

/** Device name for sync conflict copies ("Linux-a3f9"). */
function ensureDeviceName(platform: Platform): void {
  const s = useAppStore.getState();
  if (s.settings.deviceName) return;
  const suffix = Math.random().toString(36).slice(2, 6);
  s.updateSettings({ deviceName: `${platform.name}-${suffix}`.replace(/\s+/g, '-') });
}

/** Mobile browsers: let the layout viewport shrink with the on-screen keyboard. */
function preferResizingKeyboard(): void {
  const meta = document.querySelector('meta[name="viewport"]');
  const content = meta?.getAttribute('content') ?? '';
  if (meta && !content.includes('interactive-widget')) meta.setAttribute('content', `${content}, interactive-widget=resizes-content`);
}

/** Track the visual viewport (on-screen keyboard) in CSS: `--vvh`, `data-keyboard="open"`. */
function trackVisualViewport(): () => void {
  const vv = window.visualViewport;
  if (!vv) return () => {};
  const root = document.documentElement;
  const update = () => {
    root.style.setProperty('--vvh', `${Math.round(vv.height)}px`);
    const inset = Math.max(0, window.innerHeight - vv.height - vv.offsetTop);
    root.style.setProperty('--keyboard-inset', `${Math.round(inset)}px`);
    // A keyboard takes well over 120 px; small differences are browser chrome.
    const open = inset > 120 || (window.innerHeight < screen.height * 0.6 && document.activeElement?.matches('input, textarea, [contenteditable="true"]') === true);
    if (open) root.dataset.keyboard = 'open';
    else delete root.dataset.keyboard;
  };
  update();
  vv.addEventListener('resize', update);
  vv.addEventListener('scroll', update);
  window.addEventListener('resize', update);
  window.addEventListener('focusin', update);
  window.addEventListener('focusout', update);
  return () => {
    window.removeEventListener('resize', update);
    vv.removeEventListener('resize', update);
    vv.removeEventListener('scroll', update);
    window.removeEventListener('focusin', update);
    window.removeEventListener('focusout', update);
  };
}

/**
 * Settings side effects: document theme/font, close-to-tray, and — for the open workspace —
 * sync and reminders. Also re-applies the theme when the OS scheme changes.
 */
function watchSettings(platform: Platform): void {
  let prev = useAppStore.getState().settings;
  applyDocumentSettings(prev);
  void desktopShell(platform)
    ?.setCloseToTray(prev.closeToTray)
    .catch((e: unknown) => console.warn('[boot] set_close_to_tray failed', e));
  useAppStore.subscribe((state) => {
    const s = state.settings;
    if (s === prev) return;
    const old = prev;
    prev = s;
    if (s.theme !== old.theme || s.locale !== old.locale || s.fontSize !== old.fontSize || s.editorFont !== old.editorFont) {
      applyDocumentSettings(s);
    }
    if (s.closeToTray !== old.closeToTray) {
      void desktopShell(platform)?.setCloseToTray(s.closeToTray).catch((e: unknown) => console.warn('[boot] set_close_to_tray failed', e));
    }
    const ws = state.workspace;
    if (!ws) return;
    if (s.locale !== old.locale) ws.setLocale(state.locale);
    if (JSON.stringify(s.nextcloud) !== JSON.stringify(old.nextcloud)) ws.configureSync(s.nextcloud);
    if (JSON.stringify(s.reminders) !== JSON.stringify(old.reminders)) ws.applyReminderSettings(s.reminders);
  });
  if (typeof matchMedia === 'function') {
    matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => applyDocumentSettings(useAppStore.getState().settings));
  }
}

/** Remember recent pages and the route (restored after a reload). */
function watchRoute(): void {
  let prev = useAppStore.getState().route;
  useAppStore.subscribe((state) => {
    if (state.route === prev) return;
    prev = state.route;
    const ws = state.workspace;
    if (!ws || state.status !== 'ready') return;
    saveRoute(ws.key, state.route);
    if (state.route.view === 'page') {
      const path = state.route.path;
      state.updateSettings((s) => pushRecent(s, ws.key, path));
    }
  });
}

/** Everything that flushes pending edits (editor sessions listen to `pagehide`). */
export async function flushEverything(): Promise<void> {
  try {
    window.dispatchEvent(new Event('pagehide'));
  } catch {
    // ignore
  }
  await new Promise((r) => setTimeout(r, 0));
  await useAppStore.getState().workspace?.flush();
}

/** Start the app: platform, settings, vault. Safe to call once. */
export async function bootApp(): Promise<void> {
  if (booted) return;
  booted = true;
  const store = useAppStore.getState();
  // Theme and language before the first paint.
  const bootSettings = readBootSettings();
  store.initSettings(bootSettings, null);
  applyDocumentSettings(bootSettings);
  preferResizingKeyboard();
  trackVisualViewport();
  registerBuiltinCommands();

  // A share-target launch: remember the content, clean the address bar.
  pendingShare = parseShareTarget(window.location.href);
  if (pendingShare) {
    try {
      window.history.replaceState(window.history.state, '', stripShareParams(window.location.href));
    } catch {
      // ignore
    }
  }

  let platform: Platform;
  try {
    platform = await getPlatform();
  } catch (e) {
    console.error('[boot] platform failed', e);
    store.setStatus('error', e instanceof Error ? e.message : String(e));
    return;
  }
  store.setPlatform(platform);

  const { settings, persistence } = await SettingsPersistence.load(() => LocalStore.open(SETTINGS_NAMESPACE));
  store.initSettings(settings, persistence);
  ensureDeviceName(platform);
  watchSettings(platform);
  watchRoute();
  bindBrowserHistory();
  wireNotifications(platform);
  desktopShell(platform)?.onQuitRequested(async () => {
    await flushEverything();
    await persistence.flush();
  });
  window.addEventListener('pagehide', () => void persistence.flush());

  void registerServiceWorker({
    onUpdate: (apply) => {
      const t = translatorFor(useAppStore.getState().locale);
      useAppStore.getState().toast({
        id: 'pwa-update',
        kind: 'info',
        title: t('shell.update.title'),
        message: t('shell.update.body'),
        action: { label: t('shell.update.reload'), onClick: apply },
        timeout: 0,
      });
    },
    onOfflineReady: () => {
      const t = translatorFor(useAppStore.getState().locale);
      useAppStore.getState().toast({ kind: 'success', message: t('shell.update.offline') });
    },
  });

  const current = useAppStore.getState().settings.currentVault;
  if (current) await openVault(current);
  else useAppStore.getState().setStatus('onboarding');
}

/** True for Android shared-storage paths (need "All files access"). */
export function isSharedStoragePath(path: string): boolean {
  return /^\/(storage\/emulated\/|sdcard(\/|$)|storage\/[0-9A-F]{4}-[0-9A-F]{4}\/)/i.test(path);
}

export interface OpenVaultOptions {
  /** Seed the demo content ("Przykładowy brulion") when the vault has no notes. */
  demo?: boolean;
}

function unwireWorkspace(): void {
  for (const off of workspaceOffs.splice(0)) off();
}

function wireWorkspace(ws: Workspace): void {
  workspaceOffs.push(ws.subscribe(() => useAppStore.getState().bumpIndex()));
  workspaceOffs.push(
    ws.onProgress((p) => useAppStore.getState().setIndexProgress(p.indexed < p.total ? { indexed: p.indexed, total: p.total } : null)),
  );
  workspaceOffs.push(ws.onSyncStatus((s) => useAppStore.getState().setSyncStatus(s)));
  // Keep favourites, recent pages and the open page in step with renames and deletions.
  workspaceOffs.push(
    ws.onFileChanged((changes) => {
      for (const c of changes) {
        if (c.type === 'rename' && c.oldPath) {
          const from = c.oldPath;
          const to = c.path;
          useAppStore.getState().updateSettings((s) => renamePathInLists(s, ws.key, from, to));
          const route = useAppStore.getState().route;
          if (route.view === 'page' && route.path === from) useAppStore.getState().navigate({ view: 'page', path: to }, { replace: true });
        } else if (c.type === 'delete') {
          const path = c.path;
          useAppStore.getState().updateSettings((s) => renamePathInLists(s, ws.key, path, null));
        }
      }
    }),
  );
}

/** Open (or switch to) a vault. Returns false when it could not be opened (see `useBootStore`). */
export async function openVault(location: VaultLocation, opts: OpenVaultOptions = {}): Promise<boolean> {
  const store = useAppStore.getState();
  const platform = store.platform;
  if (!platform) throw new Error('Platform not ready');
  const t = translatorFor(store.locale);

  if (location.kind === 'tauri' && platform.storageAccess && isSharedStoragePath(location.path)) {
    let granted = false;
    try {
      granted = await platform.storageAccess.granted();
    } catch (e) {
      console.warn('[boot] storage access check failed', e);
    }
    if (!granted) {
      useBootStore.setState({ issue: { kind: 'storage-access', location } });
      store.setStatus('onboarding');
      return false;
    }
  }

  const previous = store.workspace;
  store.setStatus('loading');
  if (previous) {
    unwireWorkspace();
    try {
      await flushEverything();
      await previous.close();
    } catch (e) {
      console.warn('[boot] closing the previous vault failed', e);
    }
    store.setWorkspace(null);
    store.openSidePanel(null);
  }

  let ws: Workspace;
  try {
    const s = useAppStore.getState().settings;
    ws = await openWorkspace(platform, location, {
      device: s.deviceName,
      locale: useAppStore.getState().locale,
      reminders: s.reminders,
      nextcloud: s.nextcloud,
    });
  } catch (e) {
    console.warn('[boot] opening the vault failed', e);
    useBootStore.setState({
      issue: isPlatformError(e, 'permission-denied')
        ? { kind: 'permission', location }
        : { kind: 'error', location, message: e instanceof Error ? e.message : String(e) },
    });
    store.setStatus('onboarding');
    return false;
  }

  if (opts.demo && ws.vault.markdownFiles().length === 0) {
    try {
      await seedDemoVault(ws);
    } catch (e) {
      console.warn('[boot] seeding the demo vault failed', e);
    }
  }

  wireWorkspace(ws);
  useBootStore.setState({ issue: null, switching: false });
  const st = useAppStore.getState();
  st.updateSettings((s) => rememberVault(s, location));
  st.setWorkspace(ws);
  st.setSyncStatus(ws.syncStatus);
  const initial = restoredRoute(ws.key) ?? HOME_ROUTE;
  const routeOk = initial.view !== 'page' || ws.exists(initial.path);
  useAppStore.setState({ back: [], forward: [] });
  st.navigate(routeOk ? initial : HOME_ROUTE, { replace: true });
  st.setStatus('ready');
  saveRoute(ws.key, useAppStore.getState().route);
  flushPendingActivation();
  void handlePendingShare(ws, t);
  void ws.ready.then(() => reportMissedReminders(ws));
  return true;
}

async function handlePendingShare(ws: Workspace, t: ReturnType<typeof translatorFor>): Promise<void> {
  const shared = pendingShare;
  pendingShare = null;
  if (!shared) return;
  const text = composeSharedText(shared);
  if (!text) return;
  try {
    const at = await ws.quickCapture(text, { task: false, parseDates: false });
    useAppStore.getState().toast({
      kind: 'success',
      message: t('shell.capture.saved'),
      action: { label: t('shell.capture.open'), onClick: () => useAppStore.getState().navigate({ view: 'page', path: at.path, line: at.line }) },
    });
  } catch (e) {
    useAppStore.getState().toast({ kind: 'error', message: t('shell.capture.failed', { error: e instanceof Error ? e.message : String(e) }) });
  }
}

function reportMissedReminders(ws: Workspace): void {
  if (ws.isClosed) return;
  let missed: { path: string; reminder: { line: number; text: string } }[] = [];
  try {
    const fn = (ws.reminders as { missed?: () => typeof missed }).missed;
    missed = typeof fn === 'function' ? fn.call(ws.reminders) : [];
  } catch (e) {
    console.warn('[boot] missed reminders unavailable', e);
  }
  if (missed.length === 0) return;
  const t = translatorFor(useAppStore.getState().locale);
  const first = missed[0]!;
  useAppStore.getState().toast({
    id: 'missed-reminders',
    kind: 'reminder',
    title: t('shell.reminder.missedTitle'),
    message: t('shell.reminder.missed', { count: missed.length }),
    action: {
      label: t('shell.reminder.open'),
      onClick: () => useAppStore.getState().navigate({ view: 'page', path: first.path, line: first.reminder.line }),
    },
  });
}

/** The demo vault location ("Przykładowy brulion" in browser storage, on every platform). */
export function demoLocation(name: string): VaultLocation {
  return { kind: 'idb', id: DEMO_VAULT_ID, name };
}

/** Show the vault picker (keeps the current vault open until another one is chosen). */
export function showVaultPicker(): void {
  useBootStore.setState({ switching: true });
  useAppStore.getState().closeDialog();
}

/** Back to the open vault from the picker. */
export function cancelVaultPicker(): void {
  useBootStore.setState({ switching: false, issue: null });
}

/** Remove a vault from the recent list (files are not touched). */
export function forgetRecentVault(location: VaultLocation): void {
  useAppStore.getState().updateSettings((s) => forgetVault(s, location));
}

export function isCurrentVault(location: VaultLocation): boolean {
  return sameLocation(useAppStore.getState().workspace?.location, location);
}

/** Route key helper re-exported for the main pane (scroll memory). */
export { routeKey };
