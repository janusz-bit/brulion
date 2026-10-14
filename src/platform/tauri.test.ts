// @vitest-environment jsdom
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { IdbFs, MemoryFs } from '@/core/storage';
import { bytesToBase64 } from './bytes';
import { isPlatformError } from './errors';
import { AndroidNotificationService, TauriDesktopNotificationService } from './notifications-tauri';
import {
  ANDROID_BASE64_THRESHOLD,
  androidAccept,
  createTauriHttpTransport,
  createTauriPlatform,
  detectTauriOs,
  dialogFilters,
  isAbsolutePath,
  saveFilters,
  splitNativePath,
  TauriPlatform,
} from './tauri';
import { TauriFs } from './tauri-fs';
import { fakeVaultBackend, installIpc, uninstallIpc, type IpcCall } from './tauri-mock.testutil';

const mocks = vi.hoisted(() => ({
  os: { platform: vi.fn((): string => 'linux') },
  dialog: {
    open: vi.fn(async (_o?: unknown): Promise<unknown> => null),
    save: vi.fn(async (_o?: unknown): Promise<string | null> => null),
  },
  opener: { openUrl: vi.fn(async (_u: string) => {}) },
  http: { fetch: vi.fn(async (_i: string, _init?: RequestInit) => new Response('')) },
}));
vi.mock('@tauri-apps/plugin-os', () => mocks.os);
vi.mock('@tauri-apps/plugin-dialog', () => mocks.dialog);
vi.mock('@tauri-apps/plugin-opener', () => mocks.opener);
vi.mock('@tauri-apps/plugin-http', () => mocks.http);
vi.mock('@tauri-apps/plugin-notification', () => ({
  isPermissionGranted: vi.fn(async () => true),
  requestPermission: vi.fn(async () => 'granted'),
  sendNotification: vi.fn(),
  onAction: vi.fn(async () => ({ unregister: async () => {} })),
}));

const enc = new TextEncoder();
const dec = new TextDecoder();

function backendSetup(opts: Parameters<typeof fakeVaultBackend>[0] = {}) {
  const backend = fakeVaultBackend(opts);
  const calls = installIpc(backend.handle);
  return { backend, calls };
}

const callsOf = (calls: IpcCall[], cmd: string) => calls.filter((c) => c.cmd === cmd);

async function rejection(p: Promise<unknown>): Promise<unknown> {
  try {
    await p;
  } catch (e) {
    return e;
  }
  throw new Error('expected a rejection');
}

beforeEach(() => {
  mocks.os.platform.mockReset().mockReturnValue('linux');
  mocks.dialog.open.mockReset().mockResolvedValue(null);
  mocks.dialog.save.mockReset().mockResolvedValue(null);
  mocks.opener.openUrl.mockReset().mockResolvedValue(undefined);
  mocks.http.fetch.mockReset().mockResolvedValue(new Response(''));
  vi.spyOn(console, 'warn').mockImplementation(() => {});
});
afterEach(() => {
  uninstallIpc();
  vi.restoreAllMocks();
});

describe('helpers', () => {
  it('isAbsolutePath', () => {
    expect(isAbsolutePath('/home/jan')).toBe(true);
    expect(isAbsolutePath('/storage/emulated/0/Brulion')).toBe(true);
    expect(isAbsolutePath('C:\\Notatki')).toBe(true);
    expect(isAbsolutePath('\\\\serwer\\udział')).toBe(true);
    expect(isAbsolutePath('Notatki')).toBe(false);
    expect(isAbsolutePath('~/Notatki')).toBe(false);
  });

  it('splitNativePath', () => {
    expect(splitNativePath('/home/jan/Łódź.md')).toEqual({ dir: '/home/jan', base: 'Łódź.md' });
    expect(splitNativePath('/plik.md')).toEqual({ dir: '/', base: 'plik.md' });
    expect(splitNativePath('/home/jan/Notatki/')).toEqual({ dir: '/home/jan', base: 'Notatki' });
    expect(splitNativePath('C:\\Users\\Jan\\a.md')).toEqual({ dir: 'C:\\Users\\Jan', base: 'a.md' });
    expect(splitNativePath('C:\\a.md')).toEqual({ dir: 'C:\\', base: 'a.md' });
    expect(splitNativePath('a.md')).toEqual({ dir: '', base: 'a.md' });
  });

  it('dialogFilters keeps extensions only', () => {
    expect(dialogFilters(['.md', 'TXT', '*.html', 'text/markdown', '.md', ''])).toEqual([{ name: '.md, .txt, .html', extensions: ['md', 'txt', 'html'] }]);
    expect(dialogFilters(['text/plain'])).toBeUndefined();
    expect(dialogFilters(undefined)).toBeUndefined();
  });

  it('saveFilters', () => {
    expect(saveFilters('zip', 'application/zip', false)).toEqual({ filters: [{ name: 'ZIP', extensions: ['zip'] }] });
    expect(saveFilters('', 'application/zip', true)).toEqual({ filters: [{ name: 'application/zip', extensions: ['application/zip'] }] });
    expect(saveFilters('', 'application/zip', false)).toEqual({});
    expect(saveFilters('', '', true)).toEqual({});
  });
});

describe('createTauriPlatform', () => {
  it('detects desktop Linux', async () => {
    const p = await createTauriPlatform();
    expect(p.kind).toBe('tauri-desktop');
    expect(p.name).toBe('Linux');
    expect(p.vaultKinds).toEqual(['tauri']);
    expect(p.storageAccess).toBeUndefined();
    expect(p.notifications).toBeInstanceOf(TauriDesktopNotificationService);
    expect(p.notifications.firesWhenClosed).toBe(false);
  });

  it('detects Android', async () => {
    mocks.os.platform.mockReturnValue('android');
    const p = await createTauriPlatform();
    expect(p.kind).toBe('tauri-android');
    expect(p.name).toBe('Android');
    expect(p.android).toBe(true);
    expect(p.storageAccess).toBeDefined();
    expect(p.notifications).toBeInstanceOf(AndroidNotificationService);
    expect(p.notifications.firesWhenClosed).toBe(true);
  });

  it('honours explicit options without asking plugin-os', async () => {
    expect((await createTauriPlatform({ android: true })).kind).toBe('tauri-android');
    expect((await createTauriPlatform({ android: false, os: 'windows' })).name).toBe('Windows');
    expect(mocks.os.platform).not.toHaveBeenCalled();
    expect(new TauriPlatform(false, 'haiku').name).toBe('Komputer');
  });

  it('falls back to the user agent without plugin-os', async () => {
    mocks.os.platform.mockImplementation(() => {
      throw new TypeError("Cannot read properties of undefined (reading 'platform')");
    });
    expect(await detectTauriOs()).toBe('linux');
    const ua = vi.spyOn(navigator, 'userAgent', 'get').mockReturnValue('Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36');
    expect(await detectTauriOs()).toBe('android');
    ua.mockRestore();
  });
});

describe('vault locations', () => {
  it('picks a folder with the desktop dialog', async () => {
    mocks.dialog.open.mockResolvedValue('/home/jan/Syncthing/Notatki Łukasza');
    const p = new TauriPlatform(false);
    expect(await p.pickVaultFolder()).toEqual({ kind: 'tauri', path: '/home/jan/Syncthing/Notatki Łukasza', name: 'Notatki Łukasza' });
    expect(mocks.dialog.open).toHaveBeenCalledWith({ directory: true, multiple: false });
    mocks.dialog.open.mockResolvedValue(null);
    expect(await p.pickVaultFolder()).toBeNull();
    mocks.dialog.open.mockResolvedValue(['/a/b']);
    expect(await p.pickVaultFolder()).toEqual({ kind: 'tauri', path: '/a/b', name: 'b' });
  });

  it('returns null on Android (the UI asks for a path)', async () => {
    expect(await new TauriPlatform(true).pickVaultFolder()).toBeNull();
    expect(mocks.dialog.open).not.toHaveBeenCalled();
  });

  it('asks Rust for the default vault folder', async () => {
    const { calls } = backendSetup({ defaultVaultDir: '/storage/emulated/0/Brulion' });
    expect(await new TauriPlatform(true).defaultVaultLocation()).toEqual({ kind: 'tauri', path: '/storage/emulated/0/Brulion', name: 'Brulion' });
    expect(callsOf(calls, 'default_vault_dir')).toHaveLength(1);
  });

  it('opens native folders: creates them and allows assets', async () => {
    const { calls, backend } = backendSetup();
    const fs = await new TauriPlatform(false).openFileSystem({ kind: 'tauri', path: '/home/jan/Notatki', name: 'Notatki' });
    expect(fs).toBeInstanceOf(TauriFs);
    expect((fs as TauriFs).root).toBe('/home/jan/Notatki');
    expect(fs.label).toBe('/home/jan/Notatki');
    expect(callsOf(calls, 'fs_mkdir')[0]!.args).toEqual({ root: '/home/jan/Notatki', path: '' });
    await vi.waitFor(() => expect(callsOf(calls, 'allow_vault_assets')[0]?.args).toEqual({ root: '/home/jan/Notatki' }));
    await fs.writeText('Dziennik/2026-10-14.md', '- spotkanie ⏰ 2026-10-14 09:00');
    expect(await backend.vault('/home/jan/Notatki').readText('Dziennik/2026-10-14.md')).toContain('⏰');
  });

  it('still opens the vault when the asset scope cannot be extended', async () => {
    const backend = fakeVaultBackend();
    installIpc((cmd, args, options) => {
      if (cmd === 'allow_vault_assets') throw 'EINVAL: bad root: /x';
      return backend.handle(cmd, args, options);
    });
    await expect(new TauriPlatform(false).openFileSystem({ kind: 'tauri', path: '/x', name: 'x' })).resolves.toBeInstanceOf(TauriFs);
  });

  it('propagates folder creation errors', async () => {
    installIpc(() => {
      throw 'EACCES: Permission denied (os error 13): ';
    });
    const e = await rejection(new TauriPlatform(true).openFileSystem({ kind: 'tauri', path: '/storage/emulated/0/Brulion', name: 'Brulion' }));
    expect((e as { code?: string }).code).toBe('EACCES');
  });

  it('uses base64 bodies for large files on Android', async () => {
    const { calls } = backendSetup();
    const fs = await new TauriPlatform(true).openFileSystem({ kind: 'tauri', path: '/storage/emulated/0/Brulion', name: 'Brulion' });
    const big = new Uint8Array(ANDROID_BASE64_THRESHOLD + 1).fill(7);
    await fs.writeBinary('assets/duży.bin', big);
    expect(callsOf(calls, 'fs_write_binary')[0]!.args).toEqual({ base64: bytesToBase64(big) });
    await fs.writeBinary('assets/mały.bin', Uint8Array.from([1]));
    expect(callsOf(calls, 'fs_write_binary')[1]!.args).toBeInstanceOf(Uint8Array);
  });

  it('rejects relative or empty paths', async () => {
    const p = new TauriPlatform(true);
    expect(isPlatformError(await rejection(p.openFileSystem({ kind: 'tauri', path: 'Notatki', name: 'x' })), 'invalid-location')).toBe(true);
    expect(isPlatformError(await rejection(p.openFileSystem({ kind: 'tauri', path: '  ', name: 'x' })), 'invalid-location')).toBe(true);
  });

  it('opens IndexedDB and memory vaults too, but not browser folders', async () => {
    const p = new TauriPlatform(false);
    expect(await p.openFileSystem({ kind: 'idb', id: 'tauri-idb', name: 'Próba' })).toBeInstanceOf(IdbFs);
    const m = await p.openFileSystem({ kind: 'memory', id: 'm', name: 'M' });
    expect(m).toBeInstanceOf(MemoryFs);
    expect(await p.openFileSystem({ kind: 'memory', id: 'm', name: 'M' })).toBe(m);
    expect(isPlatformError(await rejection(p.openFileSystem({ kind: 'fsaccess', handleId: 'h', name: 'x' })), 'unsupported')).toBe(true);
  });
});

describe('openExternal', () => {
  it('opens safe URLs with the opener plugin', async () => {
    await new TauriPlatform(false).openExternal('https://nextcloud.example.com/apps/files');
    expect(mocks.opener.openUrl).toHaveBeenCalledWith('https://nextcloud.example.com/apps/files');
  });

  it('refuses javascript: URLs', async () => {
    const e = await rejection(new TauriPlatform(false).openExternal('javascript:void(0)'));
    expect(isPlatformError(e, 'invalid-url')).toBe(true);
    expect(mocks.opener.openUrl).not.toHaveBeenCalled();
  });
});

describe('saveFile', () => {
  it('desktop: save dialog + write_export with an encoded target header', async () => {
    const { calls, backend } = backendSetup();
    mocks.dialog.save.mockResolvedValue('/home/jan/Pobrane/Notatki — eksport.zip');
    const data = enc.encode('PK zażółć');
    await new TauriPlatform(false).saveFile('Notatki — eksport.zip', data, 'application/zip');
    expect(mocks.dialog.save).toHaveBeenCalledWith({ defaultPath: 'Notatki — eksport.zip', filters: [{ name: 'ZIP', extensions: ['zip'] }] });
    const call = callsOf(calls, 'write_export')[0]!;
    expect(call.args).toBe(data);
    expect(call.options?.headers).toEqual({ 'x-path': encodeURIComponent('/home/jan/Pobrane/Notatki — eksport.zip') });
    expect(dec.decode(backend.exports.get('/home/jan/Pobrane/Notatki — eksport.zip'))).toBe('PK zażółć');
  });

  it('sanitises the suggested name', async () => {
    backendSetup();
    await new TauriPlatform(false).saveFile('a/b:c?.md', new Uint8Array(), 'text/markdown');
    expect(mocks.dialog.save).toHaveBeenCalledWith(expect.objectContaining({ defaultPath: 'a b c .md' }));
  });

  it('does nothing when cancelled', async () => {
    const { calls } = backendSetup();
    await new TauriPlatform(false).saveFile('x.zip', new Uint8Array([1]), 'application/zip');
    expect(callsOf(calls, 'write_export')).toHaveLength(0);
  });

  it('desktop: dialog failures propagate', async () => {
    backendSetup();
    mocks.dialog.save.mockRejectedValue(new Error('dialog broken'));
    await expect(new TauriPlatform(false).saveFile('x.zip', new Uint8Array(), '')).rejects.toThrow('dialog broken');
  });

  it('Android: writes to the content:// URI, base64 for big files', async () => {
    const { calls, backend } = backendSetup();
    mocks.dialog.save.mockResolvedValue('content://com.android.providers.downloads.documents/document/42');
    const big = new Uint8Array(ANDROID_BASE64_THRESHOLD + 10).fill(1);
    await new TauriPlatform(true).saveFile('kalendarz.ics', big, 'text/calendar');
    expect(callsOf(calls, 'write_export')[0]!.args).toEqual({ base64: bytesToBase64(big) });
    expect(backend.exports.get('content://com.android.providers.downloads.documents/document/42')?.byteLength).toBe(big.byteLength);
  });

  it('Android: falls back to Eksport/ in the default vault without a dialog', async () => {
    const { backend } = backendSetup({ defaultVaultDir: '/storage/emulated/0/Brulion' });
    mocks.dialog.save.mockRejectedValue(new Error('not implemented on this platform'));
    const p = new TauriPlatform(true);
    await p.saveFile('Notatki.zip', enc.encode('pierwszy'), 'application/zip');
    await p.saveFile('Notatki.zip', enc.encode('drugi'), 'application/zip');
    const vault = backend.vault('/storage/emulated/0/Brulion');
    expect(await vault.readText('Eksport/Notatki.zip')).toBe('pierwszy');
    expect(await vault.readText('Eksport/Notatki 2.zip')).toBe('drugi');
    expect(await p.exportToVaultFolder('Notatki.zip', enc.encode('trzeci'))).toBe('/storage/emulated/0/Brulion/Eksport/Notatki 3.zip');
  });
});

describe('pickFiles', () => {
  it('desktop: reads picked files through fs_read_binary', async () => {
    const { calls, backend } = backendSetup();
    await backend.vault('/home/jan').writeText('a.md', '# A');
    await backend.vault('/home/jan/Dokumenty').writeText('Łódź.txt', 'miasto włókniarzy');
    mocks.dialog.open.mockResolvedValue(['/home/jan/a.md', '/home/jan/Dokumenty/Łódź.txt']);
    const files = await new TauriPlatform(false).pickFiles({ accept: ['.md', '.txt'], multiple: true });
    expect(mocks.dialog.open).toHaveBeenCalledWith({ multiple: true, directory: false, filters: [{ name: '.md, .txt', extensions: ['md', 'txt'] }] });
    expect(files.map((f) => f.path)).toEqual(['a.md', 'Łódź.txt']);
    expect(dec.decode(files[1]!.data)).toBe('miasto włókniarzy');
    expect(callsOf(calls, 'fs_read_binary').map((c) => c.args)).toEqual([
      { root: '/home/jan', path: 'a.md' },
      { root: '/home/jan/Dokumenty', path: 'Łódź.txt' },
    ]);
  });

  it('desktop: a single file and cancel', async () => {
    const { backend } = backendSetup();
    await backend.vault('/tmp').writeText('x.html', '<p>x</p>');
    mocks.dialog.open.mockResolvedValue('/tmp/x.html');
    expect((await new TauriPlatform(false).pickFiles({})).map((f) => f.path)).toEqual(['x.html']);
    mocks.dialog.open.mockResolvedValue(null);
    expect(await new TauriPlatform(false).pickFiles({ multiple: true })).toEqual([]);
  });

  it('desktop: reads a directory recursively with relative paths', async () => {
    const { backend } = backendSetup();
    const vault = backend.vault('/home/jan/Obsidian');
    await vault.writeText('Strona główna.md', '[[Żółw]]');
    await vault.writeText('Zwierzęta/Żółw.md', '# Żółw');
    await vault.writeText('.obsidian/app.json', '{}');
    mocks.dialog.open.mockResolvedValue('/home/jan/Obsidian');
    const files = await new TauriPlatform(false).pickFiles({ directory: true, accept: ['.md'] });
    expect(mocks.dialog.open).toHaveBeenCalledWith({ multiple: false, directory: true });
    expect(files.map((f) => f.path).sort()).toEqual(['.obsidian/app.json', 'Strona główna.md', 'Zwierzęta/Żółw.md']);
    expect(dec.decode(files.find((f) => f.path === 'Zwierzęta/Żółw.md')!.data)).toBe('# Żółw');
  });

  it('desktop: several directories are prefixed with their names', async () => {
    const { backend } = backendSetup();
    await backend.vault('/a/Logseq').writeText('pages/x.md', 'x');
    await backend.vault('/a/Joplin').writeText('y.md', 'y');
    mocks.dialog.open.mockResolvedValue(['/a/Logseq', '/a/Joplin']);
    const files = await new TauriPlatform(false).pickFiles({ directory: true, multiple: true });
    expect(files.map((f) => f.path)).toEqual(['Logseq/pages/x.md', 'Joplin/y.md']);
  });

  it('desktop: read errors become FsErrors', async () => {
    backendSetup();
    mocks.dialog.open.mockResolvedValue('/nie/ma/pliku.md');
    await expect(new TauriPlatform(false).pickFiles({})).rejects.toMatchObject({ code: 'ENOENT' });
  });

  it('Android: uses the WebView file input', async () => {
    const click = vi.spyOn(HTMLInputElement.prototype, 'click').mockImplementation(() => {});
    const picked = new TauriPlatform(true).pickFiles({ accept: ['.md'] });
    expect(mocks.dialog.open).not.toHaveBeenCalled();
    const el = click.mock.contexts[0] as HTMLInputElement;
    // Extension filters are not handed to the Android file chooser (see androidAccept).
    expect(el.accept).toBe('');
    el.dispatchEvent(new Event('cancel'));
    expect(await picked).toEqual([]);
  });

  it('Android: keeps MIME-only filters, drops filters that contain extensions', async () => {
    expect(androidAccept(['image/*', 'application/pdf'])).toEqual(['image/*', 'application/pdf']);
    expect(androidAccept(['.enex'])).toBeUndefined();
    expect(androidAccept(['.md', 'text/markdown'])).toBeUndefined();
    expect(androidAccept(['md'])).toBeUndefined();
    expect(androidAccept(undefined)).toBeUndefined();
    expect(androidAccept([' ', ''])).toBeUndefined();
    const click = vi.spyOn(HTMLInputElement.prototype, 'click').mockImplementation(() => {});
    const picked = new TauriPlatform(true).pickFiles({ accept: ['image/*'], multiple: true });
    const el = click.mock.contexts[0] as HTMLInputElement;
    expect(el.accept).toBe('image/*');
    expect(el.multiple).toBe(true);
    el.dispatchEvent(new Event('cancel'));
    expect(await picked).toEqual([]);
  });
});

describe('storageAccess (Android)', () => {
  it('checks and requests all-files access through the plugin', async () => {
    let granted = false;
    const calls = installIpc((cmd) => (cmd.endsWith('has_all_files_access') ? { granted } : null));
    const p = new TauriPlatform(true);
    expect(await p.storageAccess!.granted()).toBe(false);
    await p.storageAccess!.request();
    granted = true;
    expect(await p.storageAccess!.granted()).toBe(true);
    expect(calls.map((c) => c.cmd)).toEqual([
      'plugin:brulion-android|has_all_files_access',
      'plugin:brulion-android|request_all_files_access',
      'plugin:brulion-android|has_all_files_access',
    ]);
  });
});

describe('createTauriHttpTransport', () => {
  it('uses plugin-http fetch', async () => {
    mocks.http.fetch.mockResolvedValue(new Response('<d:multistatus/>', { status: 207, headers: { ETag: '"1"' } }));
    const res = await createTauriHttpTransport()({ method: 'propfind', url: 'https://cloud.example.com/dav', headers: { Depth: '1' }, body: '<x/>' });
    expect(mocks.http.fetch).toHaveBeenCalledWith('https://cloud.example.com/dav', expect.objectContaining({ method: 'PROPFIND', body: '<x/>', headers: { Depth: '1' } }));
    expect(res.status).toBe(207);
    expect(res.headers.etag).toBe('"1"');
    expect(dec.decode(res.body)).toBe('<d:multistatus/>');
  });

  it('reports failures as NetworkError without a CORS hint', async () => {
    mocks.http.fetch.mockRejectedValue('error sending request for url (https://cloud.example.com/dav)');
    const e = (await rejection(new TauriPlatform(false).http({ method: 'GET', url: 'https://cloud.example.com/dav' }))) as Error;
    expect(e.name).toBe('NetworkError');
    expect(e.message).not.toMatch(/CORS/);
  });
});

describe('pickFiles review fixes', () => {
  it('desktop: names of picked files are NFC like those of picked directories', async () => {
    const { calls, backend } = backendSetup();
    await backend.vault('/home/jan').writeText('Żółw.md', '# Żółw');
    const nfd = '/home/jan/Żółw.md'.normalize('NFD');
    mocks.dialog.open.mockResolvedValue([nfd]);
    const files = await new TauriPlatform(false).pickFiles({ multiple: true });
    expect(files.map((f) => f.path)).toEqual(['Żółw.md']);
    expect(files[0]!.path).toBe('Żółw.md'.normalize('NFC'));
    // The file is read under its real (on-disk) name.
    expect(callsOf(calls, 'fs_read_binary')[0]!.args).toEqual({ root: '/home/jan', path: 'Żółw.md'.normalize('NFD') });
  });
});
