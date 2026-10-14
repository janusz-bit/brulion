/**
 * Device settings (docs/UI.md, "Store"): persisted per device, never inside the vault.
 *
 * Two copies are kept:
 * - `localStorage['brulion.settings']` — read synchronously at startup so the first paint
 *   already has the right theme, language and font size (secrets are left out of it);
 * - LocalStore namespace `settings`, key `device` — the authoritative copy (IndexedDB).
 *
 * `normalizeSettings` accepts anything (old versions, hand-edited storage) and returns a
 * complete, valid DeviceSettings.
 */
import type { LocalStore } from '@/core/storage';
import type { Locale, VaultLocation, VaultPath } from '@/core/types';

export type ThemeSetting = 'system' | 'light' | 'dark';
export type EditorFont = 'serif' | 'sans';

export interface NextcloudSettings {
  server: string;
  username: string;
  password: string;
  folder: string;
  enabled: boolean;
  intervalMin: number;
}

export interface SyncthingSettings {
  url: string;
  apiKey: string;
  folderId?: string;
  enabled: boolean;
}

export interface EditorDeviceSettings {
  livePreview: boolean;
  vimMode: boolean;
  spellcheck: boolean;
  /** Max line width in rem; 0 = full width. */
  lineWidth: number;
  outlinerMode: boolean;
}

export interface ReminderSettings {
  enabled: boolean;
  /** Play a sound for alarms fired while the app is open. */
  sound: boolean;
  /** How far ahead reminders are scheduled. */
  horizonDays: number;
}

export interface RecentVault {
  location: VaultLocation;
  lastOpened: number;
}

export interface DeviceSettings {
  vaults: RecentVault[];
  currentVault?: VaultLocation;
  /** UI language; undefined = detect from the browser (Polish unless English). */
  locale?: Locale;
  theme: ThemeSetting;
  /** Editor font size in px. */
  fontSize: number;
  editorFont: EditorFont;
  editor: EditorDeviceSettings;
  nextcloud?: NextcloudSettings;
  syncthing?: SyncthingSettings;
  reminders: ReminderSettings;
  /** Desktop: closing the window hides Brulion to the tray. */
  closeToTray: boolean;
  /** Name of this device (conflict copies, sync). */
  deviceName?: string;
  /** Favourite pages per vault (key: {@link vaultKey}). */
  favourites: Record<string, VaultPath[]>;
  /** Recently opened pages per vault, newest first. */
  recent: Record<string, VaultPath[]>;
  /** Desktop sidebar visible. */
  sidebarVisible: boolean;
  /** Last tab of the default right side panel. */
  sidePanelTab: 'backlinks' | 'outline' | 'calendar';
}

export const DEFAULT_SETTINGS: DeviceSettings = {
  vaults: [],
  theme: 'system',
  fontSize: 17,
  editorFont: 'serif',
  editor: { livePreview: true, vimMode: false, spellcheck: true, lineWidth: 46, outlinerMode: true },
  reminders: { enabled: true, sound: true, horizonDays: 30 },
  closeToTray: true,
  favourites: {},
  recent: {},
  sidebarVisible: true,
  sidePanelTab: 'backlinks',
};

export const SETTINGS_STORAGE_KEY = 'brulion.settings';
export const SETTINGS_NAMESPACE = 'settings';
export const SETTINGS_KEY = 'device';
export const MAX_RECENT_PAGES = 15;
export const MAX_RECENT_VAULTS = 8;

/** Stable identifier of a vault location (LocalStore namespaces, favourites, recent). */
export function vaultKey(location: VaultLocation): string {
  switch (location.kind) {
    case 'tauri':
      return `tauri:${location.path}`;
    case 'fsaccess':
      return `fsaccess:${location.handleId}`;
    case 'idb':
      return `idb:${location.id}`;
    case 'memory':
      return `memory:${location.id}`;
  }
}

export function sameLocation(a: VaultLocation | undefined, b: VaultLocation | undefined): boolean {
  return !!a && !!b && vaultKey(a) === vaultKey(b);
}

function isObject(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

function str(v: unknown, fallback: string): string {
  return typeof v === 'string' ? v : fallback;
}

function bool(v: unknown, fallback: boolean): boolean {
  return typeof v === 'boolean' ? v : fallback;
}

function num(v: unknown, fallback: number, min: number, max: number): number {
  return typeof v === 'number' && Number.isFinite(v) ? Math.min(max, Math.max(min, v)) : fallback;
}

/** Validate a stored VaultLocation. */
export function parseLocation(v: unknown): VaultLocation | undefined {
  if (!isObject(v)) return undefined;
  const name = str(v.name, '');
  switch (v.kind) {
    case 'tauri':
      return typeof v.path === 'string' && v.path ? { kind: 'tauri', path: v.path, name: name || v.path } : undefined;
    case 'fsaccess':
      return typeof v.handleId === 'string' && v.handleId ? { kind: 'fsaccess', handleId: v.handleId, name } : undefined;
    case 'idb':
      return typeof v.id === 'string' && v.id ? { kind: 'idb', id: v.id, name: name || v.id } : undefined;
    case 'memory':
      return typeof v.id === 'string' && v.id ? { kind: 'memory', id: v.id, name: name || v.id } : undefined;
    default:
      return undefined;
  }
}

function pathLists(v: unknown, max: number): Record<string, VaultPath[]> {
  const out: Record<string, VaultPath[]> = {};
  if (!isObject(v)) return out;
  for (const [k, list] of Object.entries(v)) {
    if (k === '__proto__' || !Array.isArray(list)) continue;
    out[k] = list.filter((p): p is string => typeof p === 'string' && p !== '').slice(0, max);
  }
  return out;
}

/** A complete, valid DeviceSettings from anything (missing keys fall back to defaults). */
export function normalizeSettings(raw: unknown): DeviceSettings {
  const r = isObject(raw) ? raw : {};
  const d = DEFAULT_SETTINGS;
  const editor = isObject(r.editor) ? r.editor : {};
  const reminders = isObject(r.reminders) ? r.reminders : {};
  const vaults: RecentVault[] = [];
  if (Array.isArray(r.vaults)) {
    for (const v of r.vaults) {
      if (!isObject(v)) continue;
      const location = parseLocation(v.location);
      if (!location || vaults.some((x) => sameLocation(x.location, location))) continue;
      vaults.push({ location, lastOpened: num(v.lastOpened, 0, 0, Number.MAX_SAFE_INTEGER) });
    }
  }
  const out: DeviceSettings = {
    vaults: vaults.slice(0, MAX_RECENT_VAULTS),
    theme: r.theme === 'light' || r.theme === 'dark' || r.theme === 'system' ? r.theme : d.theme,
    fontSize: num(r.fontSize, d.fontSize, 12, 28),
    editorFont: r.editorFont === 'sans' || r.editorFont === 'serif' ? r.editorFont : d.editorFont,
    editor: {
      livePreview: bool(editor.livePreview, d.editor.livePreview),
      vimMode: bool(editor.vimMode, d.editor.vimMode),
      spellcheck: bool(editor.spellcheck, d.editor.spellcheck),
      lineWidth: num(editor.lineWidth, d.editor.lineWidth, 0, 200),
      outlinerMode: bool(editor.outlinerMode, d.editor.outlinerMode),
    },
    reminders: {
      enabled: bool(reminders.enabled, d.reminders.enabled),
      sound: bool(reminders.sound, d.reminders.sound),
      horizonDays: num(reminders.horizonDays, d.reminders.horizonDays, 1, 366),
    },
    closeToTray: bool(r.closeToTray, d.closeToTray),
    favourites: pathLists(r.favourites, 200),
    recent: pathLists(r.recent, MAX_RECENT_PAGES),
    sidebarVisible: bool(r.sidebarVisible, d.sidebarVisible),
    sidePanelTab: r.sidePanelTab === 'outline' || r.sidePanelTab === 'calendar' ? r.sidePanelTab : 'backlinks',
  };
  const current = parseLocation(r.currentVault);
  if (current) out.currentVault = current;
  if (r.locale === 'pl' || r.locale === 'en') out.locale = r.locale;
  if (typeof r.deviceName === 'string' && r.deviceName.trim()) out.deviceName = r.deviceName.trim().slice(0, 60);
  if (isObject(r.nextcloud)) {
    const n = r.nextcloud;
    out.nextcloud = {
      server: str(n.server, ''),
      username: str(n.username, ''),
      password: str(n.password, ''),
      folder: str(n.folder, 'Brulion'),
      enabled: bool(n.enabled, false),
      intervalMin: num(n.intervalMin, 5, 1, 24 * 60),
    };
  }
  if (isObject(r.syncthing)) {
    const s = r.syncthing;
    out.syncthing = { url: str(s.url, 'http://127.0.0.1:8384'), apiKey: str(s.apiKey, ''), enabled: bool(s.enabled, false) };
    if (typeof s.folderId === 'string' && s.folderId) out.syncthing.folderId = s.folderId;
  }
  return out;
}

/** The localStorage copy: everything except secrets (passwords, API keys). */
export function publicSettings(s: DeviceSettings): DeviceSettings {
  const out: DeviceSettings = { ...s };
  if (s.nextcloud) out.nextcloud = { ...s.nextcloud, password: '' };
  if (s.syncthing) out.syncthing = { ...s.syncthing, apiKey: '' };
  return out;
}

/** Synchronous bootstrap copy from localStorage (defaults when missing or unreadable). */
export function readBootSettings(storage: Pick<Storage, 'getItem'> | undefined = safeLocalStorage()): DeviceSettings {
  try {
    const raw = storage?.getItem(SETTINGS_STORAGE_KEY);
    return normalizeSettings(raw ? JSON.parse(raw) : {});
  } catch {
    return normalizeSettings({});
  }
}

export function safeLocalStorage(): Storage | undefined {
  try {
    return typeof localStorage !== 'undefined' ? localStorage : undefined;
  } catch {
    return undefined;
  }
}

/**
 * Merge the authoritative copy (LocalStore) over the bootstrap copy. Secrets only exist in
 * the LocalStore copy; everything else is taken from it when present.
 */
export function mergeStoredSettings(boot: DeviceSettings, stored: unknown): DeviceSettings {
  if (!isObject(stored)) return boot;
  return normalizeSettings(stored);
}

/** Persists settings to both places; writes to IndexedDB are coalesced. */
export class SettingsPersistence {
  private timer: ReturnType<typeof setTimeout> | null = null;
  private pending: DeviceSettings | null = null;
  private writing: Promise<void> = Promise.resolve();

  constructor(
    private readonly store: LocalStore | null,
    private readonly storage: Pick<Storage, 'setItem'> | undefined = safeLocalStorage(),
    private readonly delayMs = 150,
  ) {}

  static async load(
    openStore: () => Promise<LocalStore>,
    storage: Pick<Storage, 'getItem' | 'setItem'> | undefined = safeLocalStorage(),
  ): Promise<{ settings: DeviceSettings; persistence: SettingsPersistence }> {
    const boot = readBootSettings(storage);
    let store: LocalStore | null = null;
    let settings = boot;
    try {
      store = await openStore();
      settings = mergeStoredSettings(boot, await store.get<unknown>(SETTINGS_KEY));
    } catch (e) {
      console.warn('[settings] device settings store unavailable; using localStorage only', e);
    }
    return { settings, persistence: new SettingsPersistence(store, storage) };
  }

  /** Save (localStorage at once, IndexedDB shortly after). */
  save(settings: DeviceSettings): void {
    try {
      this.storage?.setItem(SETTINGS_STORAGE_KEY, JSON.stringify(publicSettings(settings)));
    } catch {
      // Private mode / quota: the IndexedDB copy still works.
    }
    if (!this.store) return;
    this.pending = settings;
    if (this.timer) return;
    this.timer = setTimeout(() => {
      this.timer = null;
      void this.flush();
    }, this.delayMs);
  }

  /** Write a pending save now. */
  flush(): Promise<void> {
    if (this.timer) {
      clearTimeout(this.timer);
      this.timer = null;
    }
    const value = this.pending;
    const store = this.store;
    this.pending = null;
    if (!value || !store) return this.writing;
    this.writing = this.writing.then(() =>
      store.set(SETTINGS_KEY, value).catch((e: unknown) => console.warn('[settings] could not save device settings', e)),
    );
    return this.writing;
  }
}

/** Add/refresh a vault in the recent list (most recent first). */
export function rememberVault(settings: DeviceSettings, location: VaultLocation, now = Date.now()): DeviceSettings {
  const vaults = [{ location, lastOpened: now }, ...settings.vaults.filter((v) => !sameLocation(v.location, location))].slice(
    0,
    MAX_RECENT_VAULTS,
  );
  return { ...settings, vaults, currentVault: location };
}

/** Forget a vault (recent list and, if it is the current one, the current vault). */
export function forgetVault(settings: DeviceSettings, location: VaultLocation): DeviceSettings {
  const out: DeviceSettings = { ...settings, vaults: settings.vaults.filter((v) => !sameLocation(v.location, location)) };
  if (sameLocation(out.currentVault, location)) delete out.currentVault;
  return out;
}

/** Add `path` at the front of the recent pages of a vault. */
export function pushRecent(settings: DeviceSettings, key: string, path: VaultPath): DeviceSettings {
  const list = settings.recent[key] ?? [];
  if (list[0] === path) return settings;
  return { ...settings, recent: { ...settings.recent, [key]: [path, ...list.filter((p) => p !== path)].slice(0, MAX_RECENT_PAGES) } };
}

/** Replace a path in favourites/recent after a rename (or drop it when `to` is null). */
export function renamePathInLists(settings: DeviceSettings, key: string, from: VaultPath, to: VaultPath | null): DeviceSettings {
  const fix = (list: VaultPath[] | undefined): VaultPath[] | undefined => {
    if (!list || !list.includes(from)) return list;
    const out = to === null ? list.filter((p) => p !== from) : list.map((p) => (p === from ? to : p));
    return out.filter((p, i) => out.indexOf(p) === i);
  };
  const fav = fix(settings.favourites[key]);
  const rec = fix(settings.recent[key]);
  if (fav === settings.favourites[key] && rec === settings.recent[key]) return settings;
  return {
    ...settings,
    favourites: fav ? { ...settings.favourites, [key]: fav } : settings.favourites,
    recent: rec ? { ...settings.recent, [key]: rec } : settings.recent,
  };
}

/** Toggle `path` in the favourites of a vault. */
export function toggleFavourite(settings: DeviceSettings, key: string, path: VaultPath): DeviceSettings {
  const list = settings.favourites[key] ?? [];
  const next = list.includes(path) ? list.filter((p) => p !== path) : [...list, path];
  return { ...settings, favourites: { ...settings.favourites, [key]: next } };
}
