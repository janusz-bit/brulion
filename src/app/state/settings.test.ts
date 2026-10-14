import { LocalStore } from '@/core/storage';
import {
  DEFAULT_SETTINGS,
  forgetVault,
  MAX_RECENT_PAGES,
  normalizeSettings,
  publicSettings,
  pushRecent,
  readBootSettings,
  rememberVault,
  renamePathInLists,
  sameLocation,
  SETTINGS_KEY,
  SETTINGS_STORAGE_KEY,
  SettingsPersistence,
  toggleFavourite,
  vaultKey,
} from './settings';

function memoryStorage(): Storage {
  const map = new Map<string, string>();
  return {
    get length() {
      return map.size;
    },
    clear: () => map.clear(),
    getItem: (k) => map.get(k) ?? null,
    key: (i) => [...map.keys()][i] ?? null,
    removeItem: (k) => void map.delete(k),
    setItem: (k, v) => void map.set(k, String(v)),
  };
}

describe('normalizeSettings', () => {
  it('fills defaults for anything', () => {
    expect(normalizeSettings(undefined)).toEqual(DEFAULT_SETTINGS);
    expect(normalizeSettings('garbage')).toEqual(DEFAULT_SETTINGS);
    expect(normalizeSettings([])).toEqual(DEFAULT_SETTINGS);
  });

  it('validates and clamps values', () => {
    const s = normalizeSettings({
      theme: 'neon',
      fontSize: 400,
      locale: 'de',
      editor: { lineWidth: -5, spellcheck: 'yes', outlinerMode: false },
      reminders: { horizonDays: 0 },
      editorFont: 'sans',
      deviceName: '  Mój laptop  ',
      favourites: { 'idb:x': ['a.md', 3, '', 'b.md'], __proto__: ['x'] },
    });
    expect(s.theme).toBe('system');
    expect(s.fontSize).toBe(28);
    expect(s.locale).toBeUndefined();
    expect(s.editor.lineWidth).toBe(0);
    expect(s.editor.spellcheck).toBe(true);
    expect(s.editor.outlinerMode).toBe(false);
    expect(s.reminders.horizonDays).toBe(1);
    expect(s.editorFont).toBe('sans');
    expect(s.deviceName).toBe('Mój laptop');
    expect(s.favourites).toEqual({ 'idb:x': ['a.md', 'b.md'] });
  });

  it('keeps valid vault locations and drops broken / duplicate ones', () => {
    const s = normalizeSettings({
      vaults: [
        { location: { kind: 'tauri', path: '/home/jan/Brulion', name: 'Brulion' }, lastOpened: 5 },
        { location: { kind: 'tauri', path: '/home/jan/Brulion', name: 'dup' }, lastOpened: 4 },
        { location: { kind: 'idb' }, lastOpened: 3 },
        { location: { kind: 'ftp', id: 'x' } },
        { location: { kind: 'fsaccess', handleId: 'fsa-1', name: 'Notatki' } },
      ],
      currentVault: { kind: 'idb', id: 'default', name: 'Brulion' },
      nextcloud: { server: 'cloud.example.pl', username: 'jan', password: 'tajne', enabled: true },
    });
    expect(s.vaults.map((v) => vaultKey(v.location))).toEqual(['tauri:/home/jan/Brulion', 'fsaccess:fsa-1']);
    expect(s.currentVault).toEqual({ kind: 'idb', id: 'default', name: 'Brulion' });
    expect(s.nextcloud).toEqual({ server: 'cloud.example.pl', username: 'jan', password: 'tajne', folder: 'Brulion', enabled: true, intervalMin: 5 });
  });
});

describe('helpers', () => {
  const loc = { kind: 'idb', id: 'default', name: 'Brulion' } as const;
  const other = { kind: 'tauri', path: '/x', name: 'X' } as const;

  it('identifies vaults', () => {
    expect(vaultKey(loc)).toBe('idb:default');
    expect(vaultKey({ kind: 'memory', id: 'm', name: '' })).toBe('memory:m');
    expect(sameLocation(loc, { ...loc, name: 'inna nazwa' })).toBe(true);
    expect(sameLocation(loc, other)).toBe(false);
    expect(sameLocation(undefined, loc)).toBe(false);
  });

  it('remembers and forgets vaults', () => {
    let s = rememberVault(DEFAULT_SETTINGS, loc, 1);
    s = rememberVault(s, other, 2);
    s = rememberVault(s, loc, 3);
    expect(s.vaults.map((v) => v.location.kind)).toEqual(['idb', 'tauri']);
    expect(s.vaults[0]!.lastOpened).toBe(3);
    expect(s.currentVault).toEqual(loc);
    s = forgetVault(s, loc);
    expect(s.currentVault).toBeUndefined();
    expect(s.vaults).toHaveLength(1);
  });

  it('keeps a bounded recent list per vault', () => {
    let s = DEFAULT_SETTINGS;
    for (let i = 0; i < MAX_RECENT_PAGES + 5; i++) s = pushRecent(s, 'k', `p${i}.md`);
    s = pushRecent(s, 'k', 'p3.md');
    expect(s.recent.k![0]).toBe('p3.md');
    expect(s.recent.k).toHaveLength(MAX_RECENT_PAGES);
    expect(new Set(s.recent.k).size).toBe(MAX_RECENT_PAGES);
    expect(pushRecent(s, 'k', 'p3.md')).toBe(s);
  });

  it('follows renames and deletions in favourites and recent pages', () => {
    let s = toggleFavourite(DEFAULT_SETTINGS, 'k', 'Stara.md');
    s = pushRecent(s, 'k', 'Stara.md');
    s = renamePathInLists(s, 'k', 'Stara.md', 'Nowa.md');
    expect(s.favourites.k).toEqual(['Nowa.md']);
    expect(s.recent.k).toEqual(['Nowa.md']);
    s = renamePathInLists(s, 'k', 'Nowa.md', null);
    expect(s.favourites.k).toEqual([]);
    expect(renamePathInLists(s, 'k', 'brak.md', 'x.md')).toBe(s);
    expect(toggleFavourite(toggleFavourite(s, 'k', 'a.md'), 'k', 'a.md').favourites.k).toEqual([]);
  });

  it('keeps secrets out of the localStorage copy', () => {
    const s = normalizeSettings({ nextcloud: { password: 'tajne' }, syncthing: { apiKey: 'klucz' } });
    const pub = publicSettings(s);
    expect(pub.nextcloud!.password).toBe('');
    expect(pub.syncthing!.apiKey).toBe('');
    expect(s.nextcloud!.password).toBe('tajne');
  });
});

describe('SettingsPersistence', () => {
  it('reads the bootstrap copy synchronously', () => {
    const storage = memoryStorage();
    storage.setItem(SETTINGS_STORAGE_KEY, JSON.stringify({ theme: 'dark', locale: 'en' }));
    const s = readBootSettings(storage);
    expect(s.theme).toBe('dark');
    expect(s.locale).toBe('en');
    storage.setItem(SETTINGS_STORAGE_KEY, '{broken');
    expect(readBootSettings(storage)).toEqual(DEFAULT_SETTINGS);
  });

  it('saves to localStorage (without secrets) and to the LocalStore', async () => {
    const storage = memoryStorage();
    const store = LocalStore.memory('settings-test');
    const { settings, persistence } = await SettingsPersistence.load(async () => store, storage);
    expect(settings).toEqual(DEFAULT_SETTINGS);
    const next = normalizeSettings({ theme: 'light', nextcloud: { server: 's', username: 'u', password: 'hasło' } });
    persistence.save(next);
    expect(JSON.parse(storage.getItem(SETTINGS_STORAGE_KEY)!).nextcloud.password).toBe('');
    await persistence.flush();
    expect(((await store.get(SETTINGS_KEY)) as { nextcloud: { password: string } }).nextcloud.password).toBe('hasło');
    // The authoritative copy wins on the next start.
    const again = await SettingsPersistence.load(async () => store, storage);
    expect(again.settings.nextcloud!.password).toBe('hasło');
    expect(again.settings.theme).toBe('light');
  });

  it('works without IndexedDB', async () => {
    const storage = memoryStorage();
    const warn = vi.spyOn(console, 'warn').mockImplementation(() => {});
    const { persistence } = await SettingsPersistence.load(async () => {
      throw new Error('no idb');
    }, storage);
    persistence.save(normalizeSettings({ theme: 'dark' }));
    await persistence.flush();
    expect(readBootSettings(storage).theme).toBe('dark');
    warn.mockRestore();
  });
});
