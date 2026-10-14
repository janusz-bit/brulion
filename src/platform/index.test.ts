// @vitest-environment jsdom
import { afterEach, describe, expect, it, vi } from 'vitest';
import { mockIPC } from '@tauri-apps/api/mocks';
import { uninstallIpc } from './tauri-mock.testutil';

// Record which Tauri modules get evaluated; the web path must not touch any of them.
const loaded = vi.hoisted(() => new Set<string>());
const os = vi.hoisted(() => ({ name: 'android' }));
vi.mock('./tauri', async (importOriginal) => {
  loaded.add('./tauri');
  return importOriginal();
});
vi.mock('@tauri-apps/api/core', async (importOriginal) => {
  loaded.add('@tauri-apps/api/core');
  return importOriginal();
});
vi.mock('@tauri-apps/api/event', async (importOriginal) => {
  loaded.add('@tauri-apps/api/event');
  return importOriginal();
});
vi.mock('@tauri-apps/plugin-os', () => {
  loaded.add('@tauri-apps/plugin-os');
  return { platform: () => os.name };
});
vi.mock('@tauri-apps/plugin-notification', () => {
  loaded.add('@tauri-apps/plugin-notification');
  return {};
});
vi.mock('@tauri-apps/plugin-dialog', () => {
  loaded.add('@tauri-apps/plugin-dialog');
  return {};
});
vi.mock('@tauri-apps/plugin-http', () => {
  loaded.add('@tauri-apps/plugin-http');
  return {};
});
vi.mock('@tauri-apps/plugin-opener', () => {
  loaded.add('@tauri-apps/plugin-opener');
  return {};
});

afterEach(() => {
  uninstallIpc();
});

describe('getPlatform', () => {
  it('returns the web platform in a plain browser without loading any Tauri module', async () => {
    const mod = await import('./index');
    mod.resetPlatformCache();
    expect(mod.isTauriRuntime()).toBe(false);
    const p = await mod.getPlatform();
    expect(p.kind).toBe('web');
    expect(p.name).toBe('Przeglądarka');
    expect(p.vaultKinds).toContain('idb');
    expect(await p.defaultVaultLocation()).toEqual({ kind: 'idb', id: 'default', name: 'Brulion' });
    expect(p.notifications.firesWhenClosed).toBe(false);
    expect([...loaded]).toEqual([]);
  });

  it('caches the platform', async () => {
    const mod = await import('./index');
    mod.resetPlatformCache();
    const a = mod.getPlatform();
    expect(mod.getPlatform()).toBe(a);
    expect(await mod.getPlatform()).toBe(await a);
    mod.resetPlatformCache();
    expect(await mod.getPlatform()).not.toBe(await a);
  });

  it('loads the Tauri implementation when __TAURI_INTERNALS__ exists (Android)', async () => {
    mockIPC(() => null);
    const mod = await import('./index');
    mod.resetPlatformCache();
    expect(mod.isTauriRuntime()).toBe(true);
    os.name = 'android';
    const p = await mod.getPlatform();
    expect(p.kind).toBe('tauri-android');
    expect(p.vaultKinds).toEqual(['tauri']);
    expect(p.storageAccess).toBeDefined();
    expect(p.notifications.firesWhenClosed).toBe(true);
    expect(loaded.has('./tauri')).toBe(true);
    expect(loaded.has('@tauri-apps/plugin-os')).toBe(true);
  });

  it('detects desktop Tauri', async () => {
    mockIPC(() => null);
    const mod = await import('./index');
    mod.resetPlatformCache();
    os.name = 'linux';
    const p = await mod.getPlatform();
    expect(p.kind).toBe('tauri-desktop');
    expect(p.name).toBe('Linux');
    expect(p.storageAccess).toBeUndefined();
    mod.resetPlatformCache();
  });

  it('detects __TAURI_INTERNALS__ on globalThis', async () => {
    const mod = await import('./index');
    const g = globalThis as Record<string, unknown>;
    g.__TAURI_INTERNALS__ = {};
    try {
      expect(mod.isTauriRuntime()).toBe(true);
    } finally {
      delete g.__TAURI_INTERNALS__;
    }
    expect(mod.isTauriRuntime()).toBe(false);
  });
});

describe('public API', () => {
  it('re-exports the implementations and helpers', async () => {
    const mod = await import('./index');
    for (const name of [
      'getPlatform',
      'createWebPlatform',
      'WebPlatform',
      'WebNotificationService',
      'onInAppNotification',
      'createFetchTransport',
      'createBrowserHttpTransport',
      'pickFilesWithInput',
      'downloadFile',
      'TauriFs',
      'PlatformError',
      'isPlatformError',
      'checkExternalUrl',
      'NOTIFICATION_CLICK_MESSAGE',
    ]) {
      expect(mod, name).toHaveProperty(name);
    }
  });
});

describe('platform extras without duck typing (review addition)', () => {
  it('desktopShell / androidNotifications narrow the platform', async () => {
    const mod = await import('./index');
    mod.resetPlatformCache();
    const web = await mod.getPlatform();
    expect(mod.desktopShell(web)).toBeUndefined();
    expect(mod.androidNotifications(web)).toBeUndefined();

    mockIPC(() => null);
    mod.resetPlatformCache();
    os.name = 'linux';
    const desktop = await mod.getPlatform();
    expect(mod.desktopShell(desktop)).toBeInstanceOf(mod.TauriDesktopShell);
    expect(mod.androidNotifications(desktop)).toBeUndefined();

    mod.resetPlatformCache();
    os.name = 'android';
    const android = await mod.getPlatform();
    expect(mod.desktopShell(android)).toBeUndefined();
    expect(mod.androidNotifications(android)).toBeInstanceOf(mod.AndroidNotificationService);
    mod.resetPlatformCache();
  });

  it('exports the review additions', async () => {
    const mod = await import('./index');
    for (const name of ['isAppClient', 'QUIT_REQUESTED_EVENT', 'TauriDesktopShell', 'AndroidNotificationService', 'TauriDesktopNotificationService']) {
      expect(mod, name).toHaveProperty(name);
    }
  });
});
