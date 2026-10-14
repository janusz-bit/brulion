// @vitest-environment jsdom
import { afterEach, describe, expect, it, vi } from 'vitest';
import { emit } from '@tauri-apps/api/event';
import type { FsEvent } from '@/core/types';
import { FsError, isFsError, isNotFoundError } from '@/core/storage';
import { describeAdapterContract } from '@/core/storage/adapterContract.testutil';
import { bytesToBase64 } from './bytes';
import { FS_EVENTS, nativeWatchCount, normalizeRoot, parseStatDto, TauriFs, TauriFsError, toFsError } from './tauri-fs';
import { fakeVaultBackend, installIpc, uninstallIpc, type IpcCall } from './tauri-mock.testutil';

const ROOT = '/home/jan/Notatki Łukasza';
const enc = new TextEncoder();

afterEach(() => {
  uninstallIpc();
  vi.restoreAllMocks();
});

// The whole FileSystemAdapter contract, through the IPC layer, against an emulated backend.
describeAdapterContract('TauriFs (emulated Rust backend)', (clock) => {
  const backend = fakeVaultBackend({ now: clock.now });
  installIpc(backend.handle);
  return new TauriFs(ROOT);
});

function setup(opts: Parameters<typeof fakeVaultBackend>[0] = {}) {
  const backend = fakeVaultBackend(opts);
  const calls = installIpc(backend.handle);
  return { backend, calls };
}

function lastCall(calls: IpcCall[], cmd: string): IpcCall {
  const c = calls.filter((x) => x.cmd === cmd).at(-1);
  if (!c) throw new Error(`no ${cmd} call`);
  return c;
}

describe('TauriFs IPC arguments', () => {
  it('sends root + relative, normalised paths', async () => {
    const { calls } = setup();
    const fs = new TauriFs(ROOT);
    expect(fs.kind).toBe('tauri');
    expect(fs.label).toBe(ROOT);
    await fs.writeText('./Dziennik//2026-10-14.md', '- [ ] Zażółć');
    expect(lastCall(calls, 'fs_write_text').args).toEqual({ root: ROOT, path: 'Dziennik/2026-10-14.md', content: '- [ ] Zażółć' });
    await fs.readText('Dziennik/2026-10-14.md');
    expect(lastCall(calls, 'fs_read_text').args).toEqual({ root: ROOT, path: 'Dziennik/2026-10-14.md' });
    await fs.stat('Dziennik');
    expect(lastCall(calls, 'fs_stat').args).toEqual({ root: ROOT, path: 'Dziennik' });
    await fs.rename('Dziennik/2026-10-14.md', 'Archiwum/Październik.md');
    expect(lastCall(calls, 'fs_rename').args).toEqual({ root: ROOT, from: 'Dziennik/2026-10-14.md', to: 'Archiwum/Październik.md' });
    await fs.mkdir('Nowy/Folder');
    expect(lastCall(calls, 'fs_mkdir').args).toEqual({ root: ROOT, path: 'Nowy/Folder' });
    await fs.remove('Nowy');
    expect(lastCall(calls, 'fs_remove').args).toEqual({ root: ROOT, path: 'Nowy' });
    await fs.list();
    expect(lastCall(calls, 'fs_list').args).toEqual({ root: ROOT });
  });

  it('sends binary writes as a raw body with encoded headers', async () => {
    const { calls, backend } = setup();
    const fs = new TauriFs(ROOT);
    const data = Uint8Array.from([0, 1, 2, 250, 255]);
    const st = await fs.writeBinary('załączniki/zdjęcie ćmy.png', data);
    const call = lastCall(calls, 'fs_write_binary');
    expect(call.args).toBe(data);
    expect(call.options?.headers).toEqual({
      'x-root': encodeURIComponent(ROOT),
      'x-path': encodeURIComponent('załączniki/zdjęcie ćmy.png'),
    });
    expect(st).toMatchObject({ path: 'załączniki/zdjęcie ćmy.png', kind: 'file', size: 5 });
    expect([...(await backend.vault(ROOT).readBinary('załączniki/zdjęcie ćmy.png'))]).toEqual([...data]);
  });

  it('sends large bodies as base64 above the threshold (Android)', async () => {
    const { calls, backend } = setup();
    const fs = new TauriFs(ROOT, { base64Threshold: 4 });
    await fs.writeBinary('mały.bin', Uint8Array.from([1, 2, 3, 4]));
    expect(lastCall(calls, 'fs_write_binary').args).toBeInstanceOf(Uint8Array);
    const big = enc.encode('Zażółć gęślą jaźń');
    await fs.writeBinary('duży.bin', big);
    expect(lastCall(calls, 'fs_write_binary').args).toEqual({ base64: bytesToBase64(big) });
    expect(await backend.vault(ROOT).readText('duży.bin')).toBe('Zażółć gęślą jaźń');
  });

  it('converts ArrayBuffer and number-array binary answers', async () => {
    const calls = installIpc((cmd) => (cmd === 'fs_read_binary' ? [104, 105] : null));
    expect([...(await new TauriFs(ROOT).readBinary('a.bin'))]).toEqual([104, 105]);
    expect(calls).toHaveLength(1);
  });

  it('allows the root for stat and mkdir', async () => {
    const { calls } = setup();
    const fs = new TauriFs(ROOT);
    await fs.mkdir('');
    expect(lastCall(calls, 'fs_mkdir').args).toEqual({ root: ROOT, path: '' });
    expect(await fs.stat('')).toMatchObject({ path: '', kind: 'dir' });
  });

  it('refuses empty entry paths without calling Rust', async () => {
    const { calls } = setup();
    const fs = new TauriFs(ROOT);
    for (const op of [() => fs.remove(''), () => fs.remove('/'), () => fs.readText(''), () => fs.writeBinary('.', new Uint8Array()), () => fs.rename('', 'a.md')]) {
      const e = await op().catch((x: unknown) => x);
      expect(isFsError(e, 'EINVAL')).toBe(true);
    }
    expect(calls).toHaveLength(0);
  });

  it('treats a rename to the same path as a no-op', async () => {
    const { calls } = setup();
    await new TauriFs(ROOT).rename('a.md', './a.md');
    expect(calls).toHaveLength(0);
  });

  it('requires a root', () => {
    expect(() => new TauriFs('')).toThrow(FsError);
  });
});

describe('TauriFs answers and errors', () => {
  it('maps known POSIX codes to FsError with the VaultPath', async () => {
    installIpc(() => {
      throw 'ENOENT: No such file or directory (os error 2): Książki/brak.md';
    });
    const e = await new TauriFs(ROOT).readText('Książki/brak.md').catch((x: unknown) => x);
    expect(e).toBeInstanceOf(FsError);
    expect((e as FsError).code).toBe('ENOENT');
    expect((e as FsError).path).toBe('Książki/brak.md');
    expect((e as FsError).message).toBe('ENOENT: No such file or directory (os error 2): Książki/brak.md');
    expect(isNotFoundError(e)).toBe(true);
  });

  it('keeps other codes as TauriFsError', async () => {
    installIpc(() => {
      throw 'EACCES: Permission denied (os error 13): Prywatne/a.md';
    });
    const e = await new TauriFs(ROOT).writeText('Prywatne/a.md', 'x').catch((x: unknown) => x);
    expect(e).toBeInstanceOf(TauriFsError);
    expect((e as TauriFsError).code).toBe('EACCES');
    expect((e as TauriFsError).path).toBe('Prywatne/a.md');
    expect((e as Error).message).toContain('Permission denied');
    expect(isNotFoundError(e)).toBe(false);
  });

  it('rejects malformed answers', async () => {
    installIpc((cmd) => (cmd === 'fs_list' ? { nope: true } : cmd === 'fs_read_text' ? 42 : cmd === 'fs_read_binary' ? 'x' : { path: 5 }));
    const fs = new TauriFs(ROOT);
    await expect(fs.list()).rejects.toBeInstanceOf(TauriFsError);
    await expect(fs.readText('a.md')).rejects.toBeInstanceOf(TauriFsError);
    await expect(fs.readBinary('a.md')).rejects.toBeInstanceOf(TauriFsError);
    await expect(fs.writeText('a.md', 'x')).rejects.toBeInstanceOf(TauriFsError);
  });

  it('drops malformed and root entries from listings', async () => {
    installIpc(() => [
      { path: 'a.md', kind: 'file', size: 3, mtime: 10 },
      { path: '', kind: 'dir', size: 0, mtime: 0 },
      { kind: 'file' },
      null,
      { path: 'dir', kind: 'dir', size: 'x', mtime: 'y' },
    ]);
    expect(await new TauriFs(ROOT).list()).toEqual([
      { path: 'a.md', kind: 'file', size: 3, mtime: 10 },
      { path: 'dir', kind: 'dir', size: 0, mtime: 0 },
    ]);
  });
});

describe('toFsError', () => {
  it('parses Rust error strings', () => {
    const e = toFsError('EISDIR: Is a directory: notes', 'notes');
    expect(isFsError(e, 'EISDIR')).toBe(true);
    expect(e.message).toBe('EISDIR: Is a directory: notes');
  });

  it('strips the on-disk path spelling', () => {
    const nfd = 'Żółw.md'.normalize('NFD');
    const e = toFsError(`ENOENT: gone: ${nfd}`, 'Żółw.md', nfd);
    expect(e.message).toBe('ENOENT: gone: Żółw.md');
  });

  it('accepts Error objects and passes FsErrors through', () => {
    expect(isFsError(toFsError(new Error('ENOTEMPTY: Directory not empty: d'), 'd'), 'ENOTEMPTY')).toBe(true);
    const fe = new FsError('EEXIST', 'x');
    expect(toFsError(fe, 'y')).toBe(fe);
  });

  it('wraps unknown formats as EIO', () => {
    const e = toFsError('background task failed', 'a.md');
    expect(e).toBeInstanceOf(TauriFsError);
    expect((e as TauriFsError).code).toBe('EIO');
    expect(toFsError({ message: 'weird' }, 'a.md').message).toBe('weird');
  });
});

describe('parseStatDto / normalizeRoot', () => {
  it('validates DTOs', () => {
    expect(parseStatDto({ path: 'a', kind: 'dir', size: 0, mtime: 5 })).toEqual({ path: 'a', kind: 'dir', size: 0, mtime: 5 });
    expect(parseStatDto({ path: 'a', kind: 'weird', size: -1, mtime: Number.NaN })).toEqual({ path: 'a', kind: 'file', size: 0, mtime: 0 });
    expect(parseStatDto(null)).toBeNull();
    expect(parseStatDto({ path: 1 })).toBeNull();
  });

  it('normalises roots', () => {
    expect(normalizeRoot('/a/b/')).toBe('/a/b');
    expect(normalizeRoot('/a/b//')).toBe('/a/b');
    expect(normalizeRoot('/')).toBe('/');
    expect(normalizeRoot('C:\\Notatki\\')).toBe('C:\\Notatki');
  });
});

describe('TauriFs and non-NFC names on disk', () => {
  const nfdFile = 'Żółw.md'.normalize('NFD');
  const nfdDir = 'Książki'.normalize('NFD');

  function nfdBackend() {
    return installIpc((cmd) => {
      switch (cmd) {
        case 'fs_list':
          return [
            { path: nfdFile, kind: 'file', size: 4, mtime: 1 },
            { path: nfdDir, kind: 'dir', size: 0, mtime: 1 },
            { path: `${nfdDir}/Lalka.md`, kind: 'file', size: 2, mtime: 1 },
          ];
        case 'fs_read_text':
          return 'treść';
        case 'fs_write_text':
          return { path: 'x', kind: 'file', size: 1, mtime: 2 };
        default:
          return null;
      }
    });
  }

  it('lists NFC paths and reaches the real NFD names afterwards', async () => {
    const calls = nfdBackend();
    const fs = new TauriFs(ROOT);
    const paths = (await fs.list()).map((s) => s.path);
    expect(paths).toEqual(['Żółw.md', 'Książki', 'Książki/Lalka.md']);
    await fs.readText('Żółw.md');
    expect(lastCall(calls, 'fs_read_text').args).toEqual({ root: ROOT, path: nfdFile });
    await fs.readText('Książki/Lalka.md');
    expect(lastCall(calls, 'fs_read_text').args).toEqual({ root: ROOT, path: `${nfdDir}/Lalka.md` });
    const st = await fs.writeText('Książki/Nowa.md', 'n');
    expect(lastCall(calls, 'fs_write_text').args).toMatchObject({ path: `${nfdDir}/Nowa.md` });
    expect(st.path).toBe('Książki/Nowa.md');
  });

  it('forgets disk names after remove / rename', async () => {
    const calls = nfdBackend();
    const fs = new TauriFs(ROOT);
    await fs.list();
    await fs.rename('Książki', 'Biblioteka');
    expect(lastCall(calls, 'fs_rename').args).toEqual({ root: ROOT, from: nfdDir, to: 'Biblioteka' });
    await fs.readText('Książki/Lalka.md');
    expect(lastCall(calls, 'fs_read_text').args).toMatchObject({ path: 'Książki/Lalka.md' });
    await fs.remove('Żółw.md');
    expect(lastCall(calls, 'fs_remove').args).toMatchObject({ path: nfdFile });
    await fs.readText('Żółw.md');
    expect(lastCall(calls, 'fs_read_text').args).toMatchObject({ path: 'Żółw.md' });
  });

  it('keeps the disk spelling of children of a moved directory', async () => {
    const nfdChild = 'Żółw.md'.normalize('NFD');
    const calls = installIpc((cmd) => {
      if (cmd === 'fs_list') {
        return [
          { path: 'Zoo', kind: 'dir', size: 0, mtime: 1 },
          { path: `Zoo/${nfdChild}`, kind: 'file', size: 1, mtime: 1 },
        ];
      }
      return cmd === 'fs_read_text' ? 'x' : null;
    });
    const fs = new TauriFs(ROOT);
    await fs.list();
    await fs.rename('Zoo', 'Archiwum/Zoo 2026');
    await fs.readText('Archiwum/Zoo 2026/Żółw.md');
    expect(lastCall(calls, 'fs_read_text').args).toMatchObject({ path: `Archiwum/Zoo 2026/${nfdChild}` });
  });

  it('works without any known names', async () => {
    const calls = nfdBackend();
    await new TauriFs(ROOT).readText('Żółw.md');
    expect(lastCall(calls, 'fs_read_text').args).toMatchObject({ path: 'Żółw.md' });
  });
});

describe('TauriFs.watch', () => {
  async function started(calls: IpcCall[], cmd = 'watch_start'): Promise<void> {
    await vi.waitFor(() => expect(calls.some((c) => c.cmd === cmd)).toBe(true));
  }

  it('starts the native watcher and forwards events for its root only', async () => {
    const { calls } = setup();
    const fs = new TauriFs(ROOT);
    const seen: FsEvent[][] = [];
    const stop = fs.watch((ev) => seen.push(ev));
    await started(calls);
    expect(lastCall(calls, 'watch_start').args).toEqual({ root: ROOT });
    expect(nativeWatchCount(ROOT)).toBe(1);

    await emit(FS_EVENTS, { root: '/home/jan/Inny', events: [{ type: 'create', path: 'x.md' }] });
    await emit(FS_EVENTS, {
      root: `${ROOT}/`,
      events: [
        { type: 'create', path: 'Dziennik/2026-10-14.md' },
        { type: 'modify', path: 'Żółw.md'.normalize('NFD') },
        { type: 'rename', path: 'nowa.md', oldPath: 'stara.md' },
        { type: 'rename', path: 'bez-starej.md' },
        { type: 'delete', path: 'usunięta.md' },
        { type: 'chmod', path: 'x.md' },
        { path: 'bez-typu.md' },
      ],
    });
    expect(seen).toEqual([
      [
        { type: 'create', path: 'Dziennik/2026-10-14.md' },
        { type: 'modify', path: 'Żółw.md' },
        { type: 'rename', path: 'nowa.md', oldPath: 'stara.md' },
        { type: 'delete', path: 'usunięta.md' },
      ],
    ]);

    stop();
    await started(calls, 'watch_stop');
    expect(lastCall(calls, 'watch_stop').args).toEqual({ root: ROOT });
    expect(nativeWatchCount(ROOT)).toBe(0);
    vi.spyOn(console, 'warn').mockImplementation(() => {});
    await emit(FS_EVENTS, { root: ROOT, events: [{ type: 'create', path: 'po.md' }] });
    expect(seen).toHaveLength(1);
    stop();
    await new Promise((r) => setTimeout(r, 10));
    expect(calls.filter((c) => c.cmd === 'watch_stop')).toHaveLength(1);
  });

  it('reports rescan as an event for the vault root', async () => {
    const { calls } = setup();
    const seen: FsEvent[][] = [];
    const stop = new TauriFs(ROOT).watch((ev) => seen.push(ev));
    await started(calls);
    await emit(FS_EVENTS, { root: ROOT, events: [], rescan: true });
    await emit(FS_EVENTS, { root: ROOT, events: [] });
    await emit(FS_EVENTS, 'garbage');
    expect(seen).toEqual([[{ type: 'modify', path: '' }]]);
    stop();
  });

  it('reference-counts the native watcher per root', async () => {
    const { calls } = setup();
    const a = new TauriFs(ROOT);
    const b = new TauriFs(`${ROOT}/`);
    const stopA = a.watch(() => {});
    await started(calls);
    const stopB = b.watch(() => {});
    await vi.waitFor(() => expect(nativeWatchCount(ROOT)).toBe(2));
    expect(calls.filter((c) => c.cmd === 'watch_start')).toHaveLength(1);
    stopA();
    await new Promise((r) => setTimeout(r, 10));
    expect(calls.filter((c) => c.cmd === 'watch_stop')).toHaveLength(0);
    stopB();
    await started(calls, 'watch_stop');
    expect(calls.filter((c) => c.cmd === 'watch_stop')).toHaveLength(1);
  });

  it('cleans up when stopped before the listener is ready', async () => {
    const { calls } = setup();
    const stop = new TauriFs(ROOT).watch(() => {});
    stop();
    await new Promise((r) => setTimeout(r, 20));
    expect(calls.some((c) => c.cmd === 'watch_start')).toBe(false);
    expect(nativeWatchCount(ROOT)).toBe(0);
  });

  it('falls back to polling when the native watcher fails', async () => {
    const warn = vi.spyOn(console, 'warn').mockImplementation(() => {});
    const backend = fakeVaultBackend({
      extra: () => null,
    });
    const memfs = backend.vault(ROOT);
    await memfs.writeText('a.md', 'a');
    const calls = installIpc((cmd, args, options) => {
      if (cmd === 'watch_start') throw 'ENOSPC: inotify watch limit reached: (root)';
      return backend.handle(cmd, args, options);
    });
    let mtime = 1000;
    vi.spyOn(Date, 'now').mockImplementation(() => (mtime += 1000));
    const seen: FsEvent[] = [];
    const stop = new TauriFs(ROOT, { pollInterval: 15 }).watch((ev) => seen.push(...ev));
    await vi.waitFor(() => expect(calls.filter((c) => c.cmd === 'fs_list').length).toBeGreaterThan(0), { timeout: 5000 });
    expect(nativeWatchCount(ROOT)).toBe(0);
    await memfs.writeText('b.md', 'nowy plik — źdźbło');
    await vi.waitFor(() => expect(seen).toContainEqual({ type: 'create', path: 'b.md' }), { timeout: 5000 });
    await memfs.remove('a.md');
    await vi.waitFor(() => expect(seen).toContainEqual({ type: 'delete', path: 'a.md' }), { timeout: 5000 });
    stop();
    const listed = calls.filter((c) => c.cmd === 'fs_list').length;
    await new Promise((r) => setTimeout(r, 50));
    expect(calls.filter((c) => c.cmd === 'fs_list').length).toBeLessThanOrEqual(listed + 1);
    expect(warn).toHaveBeenCalled();
  }, 20_000);
});

describe('TauriFs review fixes', () => {
  const nfd = 'Żółw.md'.normalize('NFD');

  it('keeps the new on-disk spelling after an NFC -> NFD rename event', async () => {
    const calls = installIpc((cmd) => (cmd === 'fs_read_text' ? 'treść' : cmd === 'fs_stat' ? { path: nfd, kind: 'file', size: 1, mtime: 1 } : null));
    const fs = new TauriFs(ROOT);
    const seen: FsEvent[][] = [];
    const stop = fs.watch((ev) => seen.push(ev));
    await vi.waitFor(() => expect(calls.some((c) => c.cmd === 'watch_start')).toBe(true));
    await emit(FS_EVENTS, { root: ROOT, events: [{ type: 'rename', path: nfd, oldPath: 'Żółw.md' }] });
    expect(seen).toEqual([[{ type: 'rename', path: 'Żółw.md', oldPath: 'Żółw.md' }]]);
    await fs.readText('Żółw.md');
    expect(lastCall(calls, 'fs_read_text').args).toEqual({ root: ROOT, path: nfd });
    await fs.stat('Żółw.md');
    expect(lastCall(calls, 'fs_stat').args).toEqual({ root: ROOT, path: nfd });
    stop();
  });

  it('a rename event moves the disk names of a directory\'s children', async () => {
    const nfdDir = 'Książki'.normalize('NFD');
    const calls = installIpc((cmd) => {
      if (cmd === 'fs_list') return [{ path: `Stare/${nfd}`, kind: 'file', size: 1, mtime: 1 }];
      return cmd === 'fs_read_text' ? 'x' : null;
    });
    const fs = new TauriFs(ROOT);
    await fs.list();
    const stop = fs.watch(() => {});
    await vi.waitFor(() => expect(calls.some((c) => c.cmd === 'watch_start')).toBe(true));
    await emit(FS_EVENTS, { root: ROOT, events: [{ type: 'rename', path: nfdDir, oldPath: 'Stare' }] });
    await fs.readText('Książki/Żółw.md');
    expect(lastCall(calls, 'fs_read_text').args).toEqual({ root: ROOT, path: `${nfdDir}/${nfd}` });
    stop();
  });

  it('list() forgets on-disk spellings that no longer exist', async () => {
    let listing = [{ path: nfd, kind: 'file', size: 1, mtime: 1 }];
    const calls = installIpc((cmd) => (cmd === 'fs_list' ? listing : cmd === 'fs_read_text' ? 'x' : null));
    const fs = new TauriFs(ROOT);
    await fs.list();
    await fs.readText('Żółw.md');
    expect(lastCall(calls, 'fs_read_text').args).toMatchObject({ path: nfd });
    // Another tool replaced the file by its NFC spelling (no watcher running).
    listing = [{ path: 'Żółw.md', kind: 'file', size: 1, mtime: 2 }];
    expect((await fs.list()).map((s) => s.path)).toEqual(['Żółw.md']);
    await fs.readText('Żółw.md');
    expect(lastCall(calls, 'fs_read_text').args).toMatchObject({ path: 'Żółw.md' });
  });

  it('watchers joining a native start that fails fall back to polling, later ones retry natively', async () => {
    vi.spyOn(console, 'warn').mockImplementation(() => {});
    let rejectStart: (e: unknown) => void = () => {};
    let startCalls = 0;
    const calls = installIpc((cmd) => {
      if (cmd === 'watch_start') {
        startCalls++;
        if (startCalls === 1) return new Promise((_, reject) => (rejectStart = reject));
        return null;
      }
      return cmd === 'fs_list' ? [] : null;
    });
    const stopA = new TauriFs(ROOT, { pollInterval: 60_000 }).watch(() => {});
    await vi.waitFor(() => expect(startCalls).toBe(1));
    const stopB = new TauriFs(ROOT, { pollInterval: 60_000 }).watch(() => {});
    await vi.waitFor(() => expect(nativeWatchCount(ROOT)).toBe(2));
    rejectStart('ENOSPC: inotify watch limit reached: /home/jan');
    // Both subscribers poll (each lists the vault once right away).
    await vi.waitFor(() => expect(calls.filter((c) => c.cmd === 'fs_list')).toHaveLength(2));
    expect(nativeWatchCount(ROOT)).toBe(0);
    stopA();
    stopB();
    const stopC = new TauriFs(ROOT).watch(() => {});
    await vi.waitFor(() => expect(startCalls).toBe(2));
    expect(nativeWatchCount(ROOT)).toBe(1);
    stopC();
    await vi.waitFor(() => expect(calls.filter((c) => c.cmd === 'watch_stop')).toHaveLength(1));
    expect(nativeWatchCount(ROOT)).toBe(0);
  });

  it('a watcher stopped while the native start is running releases it once the start settles', async () => {
    let resolveStart: (v: unknown) => void = () => {};
    const calls = installIpc((cmd) => (cmd === 'watch_start' ? new Promise((r) => (resolveStart = r)) : null));
    const stop = new TauriFs(ROOT).watch(() => {});
    await vi.waitFor(() => expect(calls.some((c) => c.cmd === 'watch_start')).toBe(true));
    stop();
    expect(calls.some((c) => c.cmd === 'watch_stop')).toBe(false);
    resolveStart(null);
    await vi.waitFor(() => expect(calls.filter((c) => c.cmd === 'watch_stop')).toHaveLength(1));
    expect(nativeWatchCount(ROOT)).toBe(0);
  });
});

describe('TauriFs.absolutePath / assetUrl', () => {
  it('joins the root and the on-disk spelling', async () => {
    const nfdDir = 'Załączniki'.normalize('NFD');
    installIpc((cmd) => (cmd === 'fs_list' ? [{ path: nfdDir, kind: 'dir', size: 0, mtime: 1 }] : null));
    const fs = new TauriFs(`${ROOT}/`);
    expect(fs.absolutePath('obrazy/kot.png')).toBe(`${ROOT}/obrazy/kot.png`);
    expect(fs.absolutePath('')).toBe(`${ROOT}/`);
    await fs.list();
    expect(fs.absolutePath('Załączniki/żaba.png')).toBe(`${ROOT}/${nfdDir}/żaba.png`);
    expect(new TauriFs('/').absolutePath('a.md')).toBe('/a.md');
    expect(new TauriFs('C:\\Notatki').absolutePath('a/b.png')).toBe('C:\\Notatki\\a\\b.png');
  });

  it('builds asset-protocol URLs inside Tauri and null elsewhere', async () => {
    const fs = new TauriFs(ROOT);
    expect(fs.assetUrl('kot.png')).toBeNull();
    installIpc(() => null);
    const { mockConvertFileSrc } = await import('@tauri-apps/api/mocks');
    mockConvertFileSrc('linux');
    expect(fs.assetUrl('Załączniki/kot 1.png')).toBe(`asset://localhost/${encodeURIComponent(`${ROOT}/Załączniki/kot 1.png`)}`);
  });
});

describe('native watcher call ordering (review fix)', () => {
  it('does not send watch_start for a root before its previous watch_stop has answered', async () => {
    let finishStop: (v: unknown) => void = () => {};
    const calls = installIpc((cmd) => (cmd === 'watch_stop' ? new Promise((r) => (finishStop = r)) : null));
    const stopA = new TauriFs(ROOT).watch(() => {});
    await vi.waitFor(() => expect(nativeWatchCount(ROOT)).toBe(1));
    await vi.waitFor(() => expect(calls.filter((c) => c.cmd === 'watch_start')).toHaveLength(1));
    stopA();
    await vi.waitFor(() => expect(calls.filter((c) => c.cmd === 'watch_stop')).toHaveLength(1));
    // Remount (StrictMode, vault reopened) while Rust is still stopping the old watcher.
    const stopB = new TauriFs(ROOT).watch(() => {});
    await new Promise((r) => setTimeout(r, 20));
    expect(calls.filter((c) => c.cmd === 'watch_start')).toHaveLength(1);
    finishStop(null);
    await vi.waitFor(() => expect(calls.filter((c) => c.cmd === 'watch_start')).toHaveLength(2));
    expect(calls.map((c) => c.cmd)).toEqual(['watch_start', 'watch_stop', 'watch_start']);
    stopB();
    await vi.waitFor(() => expect(calls.filter((c) => c.cmd === 'watch_stop')).toHaveLength(2));
  });
});
