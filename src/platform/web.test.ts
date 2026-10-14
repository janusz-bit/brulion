// @vitest-environment jsdom
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { NotificationService, Platform, VaultLocation } from '@/core/types';
import { FsAccessFs, IdbFs, MemoryFs } from '@/core/storage';
import { createFakeRoot, fakeWrite } from '@/core/storage/fakeFsAccess.testutil';
import { isPlatformError } from './errors';
import { WebNotificationService } from './notifications-web';
import { createWebPlatform, DEFAULT_WEB_VAULT, newHandleId, openBrowserVault, WebPlatform, type ShowDirectoryPicker, type WebPlatformDeps } from './web';

function handleStore() {
  const map = new Map<string, FileSystemDirectoryHandle>();
  return {
    map,
    saveDirectoryHandle: vi.fn(async (id: string, h: FileSystemDirectoryHandle) => {
      map.set(id, h);
    }),
    loadDirectoryHandle: vi.fn(async (id: string) => map.get(id) ?? null),
  };
}

const stubNotifications = { firesWhenClosed: false } as unknown as NotificationService;

function platform(deps: Partial<WebPlatformDeps> = {}): WebPlatform {
  return createWebPlatform({ notifications: stubNotifications, http: vi.fn(), ...deps });
}

async function rejection(p: Promise<unknown>): Promise<unknown> {
  try {
    await p;
  } catch (e) {
    return e;
  }
  throw new Error('expected a rejection');
}

describe('WebPlatform basics', () => {
  it('describes itself', () => {
    const p = platform({ showDirectoryPicker: undefined });
    expect(p.kind).toBe('web');
    expect(p.name).toBe('Przeglądarka');
    expect(p.vaultKinds).toEqual(['idb']);
    expect((p as Platform).storageAccess).toBeUndefined();
  });

  it('offers folders first when the File System Access API exists', () => {
    const p = platform({ showDirectoryPicker: vi.fn() });
    expect(p.vaultKinds).toEqual(['fsaccess', 'idb']);
  });

  it('uses real browser defaults (jsdom has no showDirectoryPicker)', () => {
    const p = new WebPlatform();
    expect(p.vaultKinds).toEqual(['idb']);
    expect(p.notifications).toBeInstanceOf(WebNotificationService);
    expect(typeof p.http).toBe('function');
  });

  it('detects window.showDirectoryPicker', () => {
    const w = window as Window & { showDirectoryPicker?: unknown };
    w.showDirectoryPicker = vi.fn();
    try {
      expect(new WebPlatform({ notifications: stubNotifications }).vaultKinds).toEqual(['fsaccess', 'idb']);
    } finally {
      delete w.showDirectoryPicker;
    }
  });

  it('suggests an IndexedDB vault for the first run', async () => {
    const loc = await platform().defaultVaultLocation();
    expect(loc).toEqual({ kind: 'idb', id: 'default', name: 'Brulion' });
    loc.name = 'zmienione';
    expect(DEFAULT_WEB_VAULT.name).toBe('Brulion');
  });

  it('generates unique handle ids', () => {
    const a = newHandleId();
    expect(a).toMatch(/^fsa-/);
    expect(newHandleId()).not.toBe(a);
  });
});

describe('pickVaultFolder', () => {
  it('persists the picked handle and returns its location', async () => {
    const store = handleStore();
    const root = createFakeRoot('Notatki Łukasza');
    const pick = vi.fn<ShowDirectoryPicker>(async () => root as unknown as FileSystemDirectoryHandle);
    const p = platform({ ...store, showDirectoryPicker: pick, newId: () => 'fsa-1' });
    const loc = await p.pickVaultFolder();
    expect(loc).toEqual({ kind: 'fsaccess', handleId: 'fsa-1', name: 'Notatki Łukasza' });
    expect(pick).toHaveBeenCalledWith(expect.objectContaining({ mode: 'readwrite' }));
    expect(store.saveDirectoryHandle).toHaveBeenCalledWith('fsa-1', root);
  });

  it('returns null when the user cancels', async () => {
    const store = handleStore();
    const p = platform({
      ...store,
      showDirectoryPicker: async () => {
        throw new DOMException('The user aborted a request.', 'AbortError');
      },
    });
    expect(await p.pickVaultFolder()).toBeNull();
    expect(store.saveDirectoryHandle).not.toHaveBeenCalled();
  });

  it('rethrows other errors (e.g. missing user gesture)', async () => {
    const p = platform({
      showDirectoryPicker: async () => {
        throw new DOMException('Must be handling a user gesture', 'SecurityError');
      },
    });
    await expect(p.pickVaultFolder()).rejects.toMatchObject({ name: 'SecurityError' });
  });

  it('is unsupported without the API', async () => {
    expect(isPlatformError(await rejection(platform({ showDirectoryPicker: undefined }).pickVaultFolder()), 'unsupported')).toBe(true);
  });
});

describe('openFileSystem', () => {
  it('opens IndexedDB vaults', async () => {
    const fs = await platform().openFileSystem({ kind: 'idb', id: 'web-test-vault', name: 'Mój notatnik' });
    expect(fs).toBeInstanceOf(IdbFs);
    expect(fs.kind).toBe('idb');
    expect(fs.label).toContain('Mój notatnik');
    await fs.writeText('Notatki/Żółw.md', '# Żółw');
    const again = await platform().openFileSystem({ kind: 'idb', id: 'web-test-vault', name: 'x' });
    expect(await again.readText('Notatki/Żółw.md')).toBe('# Żółw');
  });

  it('rejects IndexedDB locations without an id', async () => {
    expect(isPlatformError(await rejection(platform().openFileSystem({ kind: 'idb', id: '', name: 'x' })), 'invalid-location')).toBe(true);
  });

  it('opens folders after checking permission (requesting it)', async () => {
    const store = handleStore();
    const root = createFakeRoot('Notatki', { permission: 'prompt', permissionAfterRequest: 'granted' });
    await fakeWrite(root, 'Dziennik/2026-10-14.md', '- [ ] zadanie');
    store.map.set('fsa-7', root as unknown as FileSystemDirectoryHandle);
    const verify = vi.fn(async () => true);
    const fs = await platform({ ...store, verifyPermission: verify }).openFileSystem({ kind: 'fsaccess', handleId: 'fsa-7', name: 'Notatki' });
    expect(verify).toHaveBeenCalledWith(root, 'readwrite', true);
    expect(fs).toBeInstanceOf(FsAccessFs);
    expect(fs.label).toBe('Notatki');
    expect(await fs.readText('Dziennik/2026-10-14.md')).toBe('- [ ] zadanie');
  });

  it('uses the real verifyPermission by default', async () => {
    const store = handleStore();
    const root = createFakeRoot('Notatki', { permission: 'prompt', permissionAfterRequest: 'granted' });
    store.map.set('fsa-8', root as unknown as FileSystemDirectoryHandle);
    const fs = await platform(store).openFileSystem({ kind: 'fsaccess', handleId: 'fsa-8', name: '' });
    expect(fs.label).toBe('Notatki');
  });

  it('reports a missing handle', async () => {
    const e = await rejection(platform(handleStore()).openFileSystem({ kind: 'fsaccess', handleId: 'gone', name: 'Stary folder' }));
    expect(isPlatformError(e, 'handle-missing')).toBe(true);
    expect((e as Error).message).toContain('Stary folder');
  });

  it('reports denied permission (retry from a user gesture)', async () => {
    const store = handleStore();
    const root = createFakeRoot('N', { permission: 'prompt', permissionAfterRequest: 'denied' });
    store.map.set('fsa-9', root as unknown as FileSystemDirectoryHandle);
    const e = await rejection(platform(store).openFileSystem({ kind: 'fsaccess', handleId: 'fsa-9', name: 'N' }));
    expect(isPlatformError(e, 'permission-denied')).toBe(true);
  });

  it('rejects folder locations without a handle id', async () => {
    const e = await rejection(platform(handleStore()).openFileSystem({ kind: 'fsaccess', handleId: '', name: 'x' }));
    expect(isPlatformError(e, 'invalid-location')).toBe(true);
  });

  it('keeps memory vaults per id', async () => {
    const p = platform();
    const a = await p.openFileSystem({ kind: 'memory', id: 'demo', name: 'Demo' });
    expect(a).toBeInstanceOf(MemoryFs);
    await a.writeText('a.md', 'ą');
    const b = await p.openFileSystem({ kind: 'memory', id: 'demo', name: 'Demo' });
    expect(b).toBe(a);
    const c = await p.openFileSystem({ kind: 'memory', id: 'inny', name: 'Inny' });
    expect(c).not.toBe(a);
  });

  it('refuses native folders', async () => {
    const e = await rejection(platform().openFileSystem({ kind: 'tauri', path: '/home/jan/Notatki', name: 'Notatki' }));
    expect(isPlatformError(e, 'unsupported')).toBe(true);
  });

  it('refuses unknown kinds', async () => {
    const e = await rejection(platform().openFileSystem({ kind: 'nfs', id: 'x' } as unknown as VaultLocation));
    expect(isPlatformError(e, 'unsupported')).toBe(true);
  });
});

describe('openBrowserVault', () => {
  it('returns null for non-browser kinds', () => {
    expect(openBrowserVault({ kind: 'tauri', path: '/x', name: 'x' }, new Map())).toBeNull();
    expect(openBrowserVault({ kind: 'fsaccess', handleId: 'h', name: 'x' }, new Map())).toBeNull();
  });
});

describe('openExternal, saveFile, pickFiles', () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('opens safe URLs in a new tab with noopener', async () => {
    const open = vi.spyOn(window, 'open').mockImplementation(() => null);
    await new WebPlatform({ notifications: stubNotifications }).openExternal('https://pl.wikipedia.org/wiki/Gdańsk');
    expect(open).toHaveBeenCalledWith('https://pl.wikipedia.org/wiki/Gda%C5%84sk', '_blank', 'noopener');
  });

  it('refuses javascript: URLs', async () => {
    const openWindow = vi.fn();
    const e = await rejection(platform({ openWindow }).openExternal('javascript:alert(document.cookie)'));
    expect(isPlatformError(e, 'invalid-url')).toBe(true);
    expect(openWindow).not.toHaveBeenCalled();
  });

  it('saves files through the injected downloader', async () => {
    const saveFile = vi.fn();
    const data = new Uint8Array([1, 2]);
    await platform({ saveFile }).saveFile('eksport.zip', data, 'application/zip');
    expect(saveFile).toHaveBeenCalledWith('eksport.zip', data, 'application/zip');
  });

  it('downloads by default', async () => {
    Object.assign(URL, { createObjectURL: vi.fn(() => 'blob:x'), revokeObjectURL: vi.fn() });
    const click = vi.spyOn(HTMLAnchorElement.prototype, 'click').mockImplementation(() => {});
    await new WebPlatform({ notifications: stubNotifications }).saveFile('Notatki.html', new Uint8Array([60]), 'text/html');
    expect(click).toHaveBeenCalledTimes(1);
  });

  it('delegates file picking', async () => {
    const pickFiles = vi.fn(async () => [{ path: 'a.md', data: new Uint8Array([97]) }]);
    const out = await platform({ pickFiles }).pickFiles({ accept: ['.md'], multiple: true });
    expect(pickFiles).toHaveBeenCalledWith({ accept: ['.md'], multiple: true });
    expect(out).toHaveLength(1);
  });
});
