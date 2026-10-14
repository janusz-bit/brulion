import { afterEach, describe, expect, it, vi } from 'vitest';
import { DEFAULT_VAULT_CONFIG, type FsEvent } from '../types';
import { makeClock } from './adapterContract.testutil';
import { CONFIG_PATH } from './config';
import { isFsError } from './errors';
import { createFakeRoot, fakeWrite } from './fakeFsAccess.testutil';
import { FsAccessFs } from './fsAccessFs';
import { History } from './history';
import { IdbFs } from './idbFs';
import { LocalStore } from './localStore';
import { MemoryFs, type MemoryFsOptions } from './memoryFs';
import { Vault, trashStamp, type VaultChange } from './vault';

/** 2026-10-14 10:11:12 local time. */
const START = new Date(2026, 9, 14, 10, 11, 12).getTime();

function setup(opts: { files?: MemoryFsOptions['files']; reportOwnWrites?: boolean; history?: boolean } = {}) {
  const clock = makeClock(START);
  const fs = new MemoryFs({ now: clock.now, files: opts.files, reportOwnWrites: opts.reportOwnWrites });
  const store = LocalStore.memory('vault');
  const history = opts.history === false ? undefined : new History(LocalStore.memory('history'), { now: clock.now });
  const vault = new Vault(fs, { id: 'v1', name: 'Mój Brulion', store, history, now: clock.now });
  const batches: VaultChange[][] = [];
  vault.onChange((c) => batches.push(c));
  const all = (): VaultChange[] => batches.flat();
  return { clock, fs, store, history, vault, batches, all };
}

const short = (changes: VaultChange[]): string[] =>
  changes.map((c) => `${c.type}:${c.oldPath ? `${c.oldPath}->` : ''}${c.path}@${c.source}`);

afterEach(() => {
  vi.restoreAllMocks();
});

describe('Vault.open / queries', () => {
  it('lists non-ignored files, keeps conflict copies, exposes folders', async () => {
    const { vault } = setup({
      files: {
        'Strona.md': 'a',
        'journals/2026-10-14.md': 'b',
        'Strona.sync-conflict-20261014-101010-ABCDEFG.md': 'c',
        'assets/zdjęcie.png': new Uint8Array([1, 2, 3]),
        '.obsidian/app.json': '{}',
        '.trash/old.md': 'x',
        '.git/HEAD': 'ref',
        '~syncthing~Strona.md.tmp': 'tmp',
        [CONFIG_PATH]: '{}',
        'logseq/config.edn': '{}',
      },
    });
    await vault.open();
    expect(vault.files().map((f) => f.path)).toEqual([
      'Strona.md',
      'Strona.sync-conflict-20261014-101010-ABCDEFG.md',
      'assets/zdjęcie.png',
      'journals/2026-10-14.md',
    ]);
    expect(vault.markdownFiles().map((f) => f.path)).toEqual(['Strona.md', 'Strona.sync-conflict-20261014-101010-ABCDEFG.md', 'journals/2026-10-14.md']);
    expect(vault.folders().map((f) => f.path)).toEqual(['assets', 'journals']);
    expect(vault.exists('journals')).toBe(true);
    expect(vault.exists('./journals/2026-10-14.md')).toBe(true);
    expect(vault.exists('.obsidian/app.json')).toBe(false);
    expect(vault.stat('assets/zdjęcie.png')).toMatchObject({ kind: 'file', size: 3 });
    expect(vault.stat('nope')).toBeUndefined();
    expect(vault.id).toBe('v1');
    expect(vault.name).toBe('Mój Brulion');
  });

  it('normalises Polish paths given in NFD form to NFC', async () => {
    const { vault } = setup({ files: { 'Zażółć gęślą jaźń.md': 'treść' } });
    await vault.open();
    const nfd = 'Zażółć gęślą jaźń.md'.normalize('NFD');
    expect(nfd).not.toBe('Zażółć gęślą jaźń.md');
    expect(vault.exists(nfd)).toBe(true);
    expect(await vault.read(nfd)).toBe('treść');
    await vault.write(nfd, 'nowa treść');
    expect(vault.files().map((f) => f.path)).toEqual(['Zażółć gęślą jaźń.md']);
  });

  it('files() returns a copy that callers may mutate', async () => {
    const { vault } = setup({ files: { 'a.md': 'a' } });
    await vault.open();
    vault.files().pop();
    expect(vault.files()).toHaveLength(1);
  });

  it('uses default config when the file is missing', async () => {
    const { vault } = setup();
    await vault.open();
    expect(vault.config).toEqual(DEFAULT_VAULT_CONFIG);
  });

  it('merges config.json over defaults', async () => {
    const { vault } = setup({ files: { [CONFIG_PATH]: '{"journalsFolder":"Dziennik","weekStartsOn":0,"future":true}' } });
    await vault.open();
    expect(vault.config).toEqual({ ...DEFAULT_VAULT_CONFIG, journalsFolder: 'Dziennik', weekStartsOn: 0 });
    expect(await vault.read(CONFIG_PATH)).toContain('Dziennik');
  });

  it('invalid config JSON -> defaults and a warning', async () => {
    const warn = vi.spyOn(console, 'warn').mockImplementation(() => {});
    const { vault } = setup({ files: { [CONFIG_PATH]: '{ to nie jest JSON' } });
    await vault.open();
    expect(vault.config).toEqual(DEFAULT_VAULT_CONFIG);
    expect(warn).toHaveBeenCalled();
  });

  it('open() can be called again to reload', async () => {
    const { vault, fs } = setup({ files: { 'a.md': 'a' } });
    await vault.open();
    fs.externalWrite('b.md', 'b', { silent: true });
    await vault.open();
    expect(vault.files().map((f) => f.path)).toEqual(['a.md', 'b.md']);
  });
});

describe('Vault.saveConfig', () => {
  it('writes config.json, keeps unknown keys and updates config', async () => {
    const { vault, fs, all } = setup({ files: { [CONFIG_PATH]: '{"pluginX":{"on":true}}' } });
    await vault.open();
    await vault.saveConfig({ ...vault.config, journalsFolder: 'Dziennik', defaultReminderTime: '08:30' });
    await vault.idle();
    const saved = JSON.parse((fs.dump()[CONFIG_PATH] as string)) as Record<string, unknown>;
    expect(saved).toMatchObject({ journalsFolder: 'Dziennik', defaultReminderTime: '08:30', pluginX: { on: true } });
    expect(vault.config.journalsFolder).toBe('Dziennik');
    expect(short(all())).toEqual([`modify:${CONFIG_PATH}@user`]);
    expect(vault.files().some((f) => f.path === CONFIG_PATH)).toBe(false);
  });

  it('creates the config file when missing', async () => {
    const { vault, fs } = setup();
    await vault.open();
    await vault.saveConfig({ ...DEFAULT_VAULT_CONFIG, weekStartsOn: 0 });
    expect(JSON.parse(fs.dump()[CONFIG_PATH] as string)).toEqual({ ...DEFAULT_VAULT_CONFIG, weekStartsOn: 0 });
  });
});

describe('Vault.read', () => {
  it('caches text content', async () => {
    const { vault, fs } = setup({ files: { 'a.md': 'Zażółć' } });
    await vault.open();
    const spy = vi.spyOn(fs, 'readText');
    expect(await vault.read('a.md')).toBe('Zażółć');
    expect(await vault.read('a.md')).toBe('Zażółć');
    expect(spy).toHaveBeenCalledTimes(1);
  });

  it('waits for a pending write of the same file', async () => {
    const { vault } = setup({ files: { 'a.md': 'old' } });
    await vault.open();
    const w = vault.write('a.md', 'new');
    expect(await vault.read('a.md')).toBe('new');
    await w;
  });

  it('rejects for missing files and empty paths', async () => {
    const { vault } = setup();
    await vault.open();
    await expect(vault.read('missing.md')).rejects.toBeTruthy();
    await expect(vault.read('')).rejects.toSatisfy((e: unknown) => isFsError(e, 'EINVAL'));
  });

  it('reads ignored files without caching them', async () => {
    const { vault, fs } = setup({ files: { '.obsidian/app.json': '{"a":1}' } });
    await vault.open();
    const spy = vi.spyOn(fs, 'readText');
    expect(await vault.read('.obsidian/app.json')).toBe('{"a":1}');
    await vault.read('.obsidian/app.json');
    expect(spy).toHaveBeenCalledTimes(2);
  });

  it('readBinary returns bytes', async () => {
    const { vault } = setup({ files: { 'img.png': new Uint8Array([9, 8]) } });
    await vault.open();
    expect([...(await vault.readBinary('img.png'))]).toEqual([9, 8]);
  });
});

describe('Vault.write', () => {
  it('emits create then modify with the given source', async () => {
    const { vault, all } = setup();
    await vault.open();
    await vault.write('Notatki/Żółw.md', 'pierwsza');
    await vault.write('Notatki/Żółw.md', 'druga', 'sync');
    await vault.idle();
    expect(short(all())).toEqual(['create:Notatki/Żółw.md@user', 'modify:Notatki/Żółw.md@sync']);
    expect(vault.exists('Notatki')).toBe(true);
    expect(vault.stat('Notatki/Żółw.md')).toMatchObject({ size: 'druga'.length });
    expect(await vault.read('Notatki/Żółw.md')).toBe('druga');
  });

  it('is a no-op when the content is unchanged (no write, no event)', async () => {
    const { vault, fs, all } = setup({ files: { 'a.md': 'same' } });
    await vault.open();
    const spy = vi.spyOn(fs, 'writeText');
    await vault.write('a.md', 'same'); // never read before: compared against disk
    await vault.write('a.md', 'same');
    await vault.write('a.md', 'changed');
    await vault.write('a.md', 'changed');
    await vault.idle();
    expect(spy).toHaveBeenCalledTimes(1);
    expect(short(all())).toEqual(['modify:a.md@user']);
  });

  it('snapshots the previous content into history with the change source', async () => {
    const { vault, history, clock } = setup({ files: { 'a.md': 'v0' } });
    await vault.open();
    await vault.write('a.md', 'v1');
    clock.advance(10 * 60_000);
    await vault.write('a.md', 'v2', 'sync');
    const list = await history!.list('a.md');
    expect(list.map((e) => e.reason)).toEqual(['sync', 'user']);
    expect(await history!.get(list[0]!.id)).toBe('v1');
    expect(await history!.get(list[1]!.id)).toBe('v0');
  });

  it('does not snapshot new files or binary files', async () => {
    const { vault, history } = setup();
    await vault.open();
    await vault.write('new.md', 'x');
    await vault.writeBinary('img.png', new Uint8Array([1]));
    await vault.writeBinary('img.png', new Uint8Array([2]));
    expect(await history!.paths()).toEqual([]);
  });

  it('works without history', async () => {
    const { vault } = setup({ history: false, files: { 'a.md': 'a' } });
    await vault.open();
    await vault.write('a.md', 'b');
    expect(await vault.read('a.md')).toBe('b');
  });

  it('a failing history does not block the write', async () => {
    const warn = vi.spyOn(console, 'warn').mockImplementation(() => {});
    const { vault, history, fs } = setup({ files: { 'a.md': 'a' } });
    await vault.open();
    vi.spyOn(history!, 'snapshot').mockRejectedValue(new Error('quota'));
    await vault.write('a.md', 'b');
    expect(fs.dump()['a.md']).toBe('b');
    expect(warn).toHaveBeenCalled();
  });

  it('serialises rapid writes to the same file (never interleaved, last wins)', async () => {
    const { vault, fs, all } = setup({ files: { 'a.md': '' } });
    await vault.open();
    const original = fs.writeText.bind(fs);
    let active = 0;
    let maxActive = 0;
    const order: string[] = [];
    vi.spyOn(fs, 'writeText').mockImplementation(async (p, c) => {
      active++;
      maxActive = Math.max(maxActive, active);
      await new Promise((r) => setTimeout(r, 1));
      order.push(c);
      const st = await original(p, c);
      active--;
      return st;
    });
    const writes = Array.from({ length: 30 }, (_, i) => vault.write('a.md', `wersja ${i}`));
    await Promise.all(writes);
    await vault.idle();
    expect(maxActive).toBe(1);
    expect(order).toEqual(Array.from({ length: 30 }, (_, i) => `wersja ${i}`));
    expect(fs.dump()['a.md']).toBe('wersja 29');
    expect(all()).toHaveLength(30);
  });

  it('writes to different folders run concurrently', async () => {
    const { vault, fs } = setup();
    await vault.open();
    const original = fs.writeText.bind(fs);
    let active = 0;
    let maxActive = 0;
    vi.spyOn(fs, 'writeText').mockImplementation(async (p, c) => {
      active++;
      maxActive = Math.max(maxActive, active);
      await new Promise((r) => setTimeout(r, 2));
      const st = await original(p, c);
      active--;
      return st;
    });
    await Promise.all([vault.write('a/x.md', '1'), vault.write('b/y.md', '2'), vault.write('z.md', '3')]);
    expect(maxActive).toBeGreaterThan(1);
  });

  it('rejects writing onto a folder or an empty path', async () => {
    const { vault } = setup({ files: { 'folder/a.md': 'a' } });
    await vault.open();
    await expect(vault.write('folder', 'x')).rejects.toSatisfy((e: unknown) => isFsError(e, 'EISDIR'));
    await expect(vault.write('', 'x')).rejects.toSatisfy((e: unknown) => isFsError(e, 'EINVAL'));
  });

  it('writes to ignored paths without tracking or events', async () => {
    const { vault, fs, all } = setup();
    await vault.open();
    await vault.write('.obsidian/workspace.json', '{}');
    await vault.idle();
    expect(fs.dump()['.obsidian/workspace.json']).toBe('{}');
    expect(all()).toEqual([]);
    expect(vault.files()).toEqual([]);
  });

  it('snapshots content of a file that exists on disk but was unknown', async () => {
    const { vault, fs, history, all } = setup();
    await vault.open();
    fs.externalWrite('surprise.md', 'from Syncthing', { silent: true });
    await vault.write('surprise.md', 'mine');
    await vault.idle();
    expect(short(all())).toEqual(['modify:surprise.md@user']);
    const list = await history!.list('surprise.md');
    expect(await history!.get(list[0]!.id)).toBe('from Syncthing');
  });

  it('when the backend write fails, the error propagates and caches are not poisoned', async () => {
    const { vault, fs, all } = setup({ files: { 'a.md': 'disk' } });
    await vault.open();
    vi.spyOn(fs, 'writeText').mockRejectedValueOnce(new Error('EACCES'));
    await expect(vault.write('a.md', 'lost')).rejects.toThrow('EACCES');
    expect(await vault.read('a.md')).toBe('disk');
    await vault.write('a.md', 'disk'); // same as disk: no-op
    await vault.idle();
    expect(all()).toEqual([]);
  });

  it('writeBinary emits events and skips identical bytes', async () => {
    const { vault, fs, all } = setup();
    await vault.open();
    const spy = vi.spyOn(fs, 'writeBinary');
    await vault.writeBinary('assets/a.bin', new Uint8Array([1, 2]));
    await vault.writeBinary('assets/a.bin', new Uint8Array([1, 2]));
    await vault.writeBinary('assets/a.bin', new Uint8Array([3]), 'import');
    await vault.idle();
    expect(spy).toHaveBeenCalledTimes(2);
    expect(short(all())).toEqual(['create:assets/a.bin@user', 'modify:assets/a.bin@import']);
  });
});

describe('Vault.create', () => {
  it('returns the path, or a unique variant when taken (case-insensitive)', async () => {
    const { vault } = setup({ files: { 'Nowa notatka.md': 'x', 'folder/strona.md': 'y' } });
    await vault.open();
    expect(await vault.create('Inna.md', 'a')).toBe('Inna.md');
    expect(await vault.create('Nowa notatka.md', 'b')).toBe('Nowa notatka 2.md');
    expect(await vault.create('Nowa notatka.md', 'c')).toBe('Nowa notatka 3.md');
    expect(await vault.create('folder/Strona.md', 'd')).toBe('folder/Strona 2.md');
    expect(await vault.read('Nowa notatka.md')).toBe('x');
  });

  it('concurrent creates of the same name get distinct paths', async () => {
    const { vault } = setup();
    await vault.open();
    const paths = await Promise.all(Array.from({ length: 5 }, (_, i) => vault.create('Dziennik/Spotkanie.md', `${i}`)));
    expect(new Set(paths).size).toBe(5);
    expect(paths.sort()).toEqual([
      'Dziennik/Spotkanie 2.md',
      'Dziennik/Spotkanie 3.md',
      'Dziennik/Spotkanie 4.md',
      'Dziennik/Spotkanie 5.md',
      'Dziennik/Spotkanie.md',
    ]);
  });

  it('never overwrites a file that exists on disk but is unknown to the vault', async () => {
    const { vault, fs } = setup();
    await vault.open();
    fs.externalWrite('Pomysł.md', 'cudzy', { silent: true });
    expect(await vault.create('Pomysł.md', 'mój')).toBe('Pomysł 2.md');
    expect(fs.dump()['Pomysł.md']).toBe('cudzy');
  });

  it('does not collide with folders', async () => {
    const { vault } = setup({ files: { 'Projekt/a.md': 'a' } });
    await vault.open();
    expect(await vault.create('Projekt', 'x')).toBe('Projekt 2');
  });

  it('creates binary files', async () => {
    const { vault } = setup();
    await vault.open();
    expect(await vault.create('assets/img.png', new Uint8Array([1]))).toBe('assets/img.png');
    expect(await vault.create('assets/img.png', new Uint8Array([2]))).toBe('assets/img 2.png');
  });
});

describe('Vault.delete / trash / restore', () => {
  it('moves a file to .trash and emits delete', async () => {
    const { vault, fs, all } = setup({ files: { 'Projekty/Dom.md': 'dom' } });
    await vault.open();
    await vault.read('Projekty/Dom.md');
    await vault.delete('Projekty/Dom.md');
    await vault.idle();
    expect(short(all())).toEqual(['delete:Projekty/Dom.md@user']);
    expect(vault.exists('Projekty/Dom.md')).toBe(false);
    expect(fs.dump()['.trash/Projekty/Dom.md']).toBe('dom');
    expect((await vault.trash()).map((t) => t.path)).toEqual(['.trash/Projekty/Dom.md']);
    await expect(vault.read('Projekty/Dom.md')).rejects.toBeTruthy();
  });

  it('adds a local timestamp when the trash path is taken', async () => {
    const { vault, fs, clock } = setup({ files: { 'a.md': 'first' } });
    await vault.open();
    await vault.delete('a.md');
    await vault.write('a.md', 'second');
    await vault.delete('a.md');
    await vault.write('a.md', 'third');
    await vault.delete('a.md');
    clock.advance(1000);
    await vault.write('a.md', 'fourth');
    await vault.delete('a.md');
    const stamp = trashStamp(START);
    expect(stamp).toBe('20261014-101112');
    expect(fs.dump()).toMatchObject({
      '.trash/a.md': 'first',
      [`.trash/a (${stamp}).md`]: 'second',
      [`.trash/a (${stamp}) 2.md`]: 'third',
      '.trash/a (20261014-101113).md': 'fourth',
    });
  });

  it('deletes folders recursively with a delete per file', async () => {
    const { vault, fs, all } = setup({ files: { 'P/a.md': 'a', 'P/sub/b.md': 'b', 'P2/c.md': 'c' } });
    await vault.open();
    await vault.delete('P');
    await vault.idle();
    expect(short(all()).sort()).toEqual(['delete:P/a.md@user', 'delete:P/sub/b.md@user']);
    expect(vault.folders().map((f) => f.path)).toEqual(['P2']);
    expect(fs.dump()).toMatchObject({ '.trash/P/a.md': 'a', '.trash/P/sub/b.md': 'b', 'P2/c.md': 'c' });
  });

  it('deleting a missing path is a no-op; a stale cache entry is just forgotten', async () => {
    const { vault, fs, all } = setup({ files: { 'a.md': 'a' } });
    await vault.open();
    await vault.delete('missing.md');
    fs.externalRemove('a.md', { silent: true });
    vi.spyOn(fs, 'stat').mockResolvedValue(null);
    await vault.delete('a.md');
    await vault.idle();
    expect(short(all())).toEqual(['delete:a.md@user']);
    expect(vault.exists('a.md')).toBe(false);
  });

  it('restores to the original path', async () => {
    const { vault, all } = setup({ files: { 'Książki/Lalka.md': 'Prus' } });
    await vault.open();
    await vault.delete('Książki/Lalka.md');
    expect(await vault.trashInfo('.trash/Książki/Lalka.md')).toEqual({ original: 'Książki/Lalka.md', deletedAt: START });
    const restored = await vault.restore('.trash/Książki/Lalka.md');
    await vault.idle();
    expect(restored).toBe('Książki/Lalka.md');
    expect(await vault.read(restored)).toBe('Prus');
    expect(short(all())).toEqual(['delete:Książki/Lalka.md@user', 'create:Książki/Lalka.md@user']);
    expect(await vault.trash()).toEqual([]);
  });

  it('restores stamped copies and picks a unique path when the original is taken', async () => {
    const { vault } = setup({ files: { 'a.md': 'one' } });
    await vault.open();
    await vault.delete('a.md');
    await vault.write('a.md', 'two');
    await vault.delete('a.md');
    await vault.write('a.md', 'three');
    const stamped = `.trash/a (${trashStamp(START)}).md`;
    expect((await vault.trashInfo(stamped)).original).toBe('a.md');
    expect(await vault.restore(stamped)).toBe('a 2.md');
    expect(await vault.read('a 2.md')).toBe('two');
    expect(await vault.restore('.trash/a.md')).toBe('a 3.md');
  });

  it('restores whole folders and single files from trashed folders', async () => {
    const { vault, all } = setup({ files: { 'F/a.md': 'a', 'F/b.md': 'b', 'G/c.md': 'c' } });
    await vault.open();
    await vault.delete('F');
    await vault.delete('G');
    await vault.idle();
    const before = all().length;
    expect(await vault.restore('.trash/G/c.md')).toBe('G/c.md');
    expect(await vault.restore('.trash/F')).toBe('F');
    await vault.idle();
    expect(short(all().slice(before)).sort()).toEqual(['create:F/a.md@user', 'create:F/b.md@user', 'create:G/c.md@user']);
    expect(vault.files().map((f) => f.path)).toEqual(['F/a.md', 'F/b.md', 'G/c.md']);
  });

  it('falls back to parsing the trash path when no metadata exists (deleted on another device)', async () => {
    const { vault, fs } = setup({ files: { '.trash/Notatki (20261001-080000)/x (20261002-090000) 3.md': 'x' } });
    await vault.open();
    expect(await vault.trashInfo('.trash/Notatki (20261001-080000)/x (20261002-090000) 3.md')).toEqual({ original: 'Notatki/x.md' });
    expect(await vault.restore('.trash/Notatki (20261001-080000)/x (20261002-090000) 3.md')).toBe('Notatki/x.md');
    expect(fs.dump()['Notatki/x.md']).toBe('x');
  });

  it('rejects trash operations on paths outside the trash', async () => {
    const { vault } = setup({ files: { 'a.md': 'a' } });
    await vault.open();
    await expect(vault.restore('a.md')).rejects.toSatisfy((e: unknown) => isFsError(e, 'EINVAL'));
    await expect(vault.restore('.trash/missing.md')).rejects.toSatisfy((e: unknown) => isFsError(e, 'ENOENT'));
  });

  it('deleting inside the trash is permanent; emptyTrash clears everything', async () => {
    const { vault, fs, store } = setup({ files: { 'a.md': 'a', 'b.md': 'b' } });
    await vault.open();
    await vault.delete('a.md');
    await vault.delete('b.md');
    await vault.delete('.trash/a.md');
    expect(fs.dump()['.trash/a.md']).toBeUndefined();
    expect(fs.dump()['.trash/b.md']).toBe('b');
    await vault.emptyTrash();
    expect(await vault.trash()).toEqual([]);
    expect(await store.keys()).toEqual([]);
  });

  it('deletePermanently removes tracked files with an event', async () => {
    const { vault, fs, all } = setup({ files: { 'a.md': 'a' } });
    await vault.open();
    await vault.deletePermanently('a.md');
    await vault.idle();
    expect(fs.dump()).toEqual({});
    expect(short(all())).toEqual(['delete:a.md@user']);
  });
});

describe('Vault.rename', () => {
  it('renames a file, keeping cached content and history', async () => {
    const { vault, fs, history, all, clock } = setup({ files: { 'stara.md': 'v0' } });
    await vault.open();
    await vault.write('stara.md', 'v1');
    clock.advance(1000);
    await vault.rename('stara.md', 'Archiwum/nowa.md');
    await vault.idle();
    expect(short(all())).toEqual(['modify:stara.md@user', 'rename:stara.md->Archiwum/nowa.md@user']);
    const spy = vi.spyOn(fs, 'readText');
    expect(await vault.read('Archiwum/nowa.md')).toBe('v1');
    expect(spy).not.toHaveBeenCalled();
    expect(await history!.list('Archiwum/nowa.md')).toHaveLength(1);
    expect(await history!.list('stara.md')).toEqual([]);
    expect(vault.exists('stara.md')).toBe(false);
    expect(vault.exists('Archiwum')).toBe(true);
  });

  it('renames a folder with one rename event per file', async () => {
    const { vault, all } = setup({ files: { 'Projekty/a.md': 'a', 'Projekty/Dom/b.md': 'b', 'Projekty/img.png': new Uint8Array([1]), 'Inne/c.md': 'c' } });
    await vault.open();
    await vault.rename('Projekty', 'Archiwum/Projekty 2025');
    await vault.idle();
    expect(short(all()).sort()).toEqual([
      'rename:Projekty/Dom/b.md->Archiwum/Projekty 2025/Dom/b.md@user',
      'rename:Projekty/a.md->Archiwum/Projekty 2025/a.md@user',
      'rename:Projekty/img.png->Archiwum/Projekty 2025/img.png@user',
    ]);
    expect(vault.folders().map((f) => f.path)).toEqual(['Archiwum', 'Archiwum/Projekty 2025', 'Archiwum/Projekty 2025/Dom', 'Inne']);
    expect(await vault.read('Archiwum/Projekty 2025/Dom/b.md')).toBe('b');
  });

  it('refuses existing targets, missing sources and moving a folder into itself', async () => {
    const { vault } = setup({ files: { 'a.md': 'a', 'b.md': 'b', 'F/x.md': 'x' } });
    await vault.open();
    await expect(vault.rename('a.md', 'b.md')).rejects.toSatisfy((e: unknown) => isFsError(e, 'EEXIST'));
    await expect(vault.rename('a.md', 'F')).rejects.toSatisfy((e: unknown) => isFsError(e, 'EEXIST'));
    await expect(vault.rename('zz.md', 'y.md')).rejects.toSatisfy((e: unknown) => isFsError(e, 'ENOENT'));
    await expect(vault.rename('F', 'F/G')).rejects.toSatisfy((e: unknown) => isFsError(e, 'EINVAL'));
    await vault.rename('a.md', 'a.md');
    expect(await vault.read('a.md')).toBe('a');
  });

  it('allows case-only renames', async () => {
    const { vault, all } = setup({ files: { 'notatka.md': 'n' } });
    await vault.open();
    await vault.rename('notatka.md', 'Notatka.md');
    await vault.idle();
    expect(short(all())).toEqual(['rename:notatka.md->Notatka.md@user']);
  });

  it('moving into an ignored folder reports a delete', async () => {
    const { vault, all } = setup({ files: { 'a.md': 'a' } });
    await vault.open();
    await vault.rename('a.md', '.archive-ignored/../.obsidian/a.md');
    await vault.idle();
    expect(short(all())).toEqual(['delete:a.md@user']);
  });

  it('waits for pending writes inside a folder before moving it', async () => {
    const { vault, fs } = setup({ files: { 'F/a.md': 'old' } });
    await vault.open();
    const w = vault.write('F/a.md', 'new');
    const r = vault.rename('F', 'G');
    await Promise.all([w, r]);
    expect(fs.dump()).toEqual({ 'G/a.md': 'new' });
  });
});

describe('Vault.onChange batching', () => {
  it('delivers changes of one tick as one batch; unsubscribe works', async () => {
    const { vault, batches } = setup({ files: { 'F/a.md': 'a', 'F/b.md': 'b' } });
    await vault.open();
    await vault.rename('F', 'G');
    await vault.idle();
    expect(batches).toHaveLength(1);
    expect(batches[0]).toHaveLength(2);
    const extra = vi.fn();
    const off = vault.onChange(extra);
    off();
    await vault.write('x.md', 'x');
    await vault.idle();
    expect(extra).not.toHaveBeenCalled();
  });

  it('a throwing listener does not affect others', async () => {
    const warn = vi.spyOn(console, 'warn').mockImplementation(() => {});
    const { vault, all } = setup();
    vault.onChange(() => {
      throw new Error('bad listener');
    });
    await vault.open();
    await vault.write('a.md', 'a');
    await vault.idle();
    expect(all()).toHaveLength(1);
    expect(warn).toHaveBeenCalled();
  });
});

describe('Vault watching (external changes)', () => {
  it('reports external create / modify / delete with source external', async () => {
    const { vault, fs, all } = setup({ files: { 'a.md': 'a', 'b.md': 'b' } });
    await vault.open();
    await vault.read('a.md');
    vault.startWatching();
    expect(vault.watching).toBe(true);
    fs.externalWrite('new.md', 'nowa');
    fs.externalWrite('a.md', 'a zmieniona');
    fs.externalRemove('b.md');
    await vault.idle();
    expect(short(all())).toEqual(['create:new.md@external', 'modify:a.md@external', 'delete:b.md@external']);
    expect(await vault.read('a.md')).toBe('a zmieniona');
    expect(vault.exists('b.md')).toBe(false);
    vault.stopWatching();
    fs.externalWrite('c.md', 'c');
    await vault.idle();
    expect(all()).toHaveLength(3);
  });

  it('suppresses the echo of its own writes, renames and deletes', async () => {
    const { vault, all } = setup({ reportOwnWrites: true, files: { 'a.md': 'a', 'F/x.md': 'x' } });
    await vault.open();
    vault.startWatching();
    await vault.write('a.md', 'moja zmiana');
    await vault.write('b.md', 'nowy');
    await vault.writeBinary('img.png', new Uint8Array([1]));
    await vault.rename('b.md', 'c.md');
    await vault.rename('F', 'G');
    await vault.delete('c.md');
    await vault.saveConfig({ ...DEFAULT_VAULT_CONFIG, weekStartsOn: 0 });
    await vault.idle();
    expect(all().filter((c) => c.source === 'external')).toEqual([]);
    expect(all().length).toBe(7);
  });

  it('ignores touches that do not change content', async () => {
    const { vault, fs, all, clock } = setup({ files: { 'a.md': 'same' } });
    await vault.open();
    await vault.read('a.md');
    vault.startWatching();
    clock.advance(5000);
    fs.externalWrite('a.md', 'same');
    await vault.idle();
    expect(all()).toEqual([]);
    expect(vault.stat('a.md')?.mtime).toBe(START + 5000);
  });

  it('snapshots content replaced or deleted by external programs', async () => {
    const { vault, fs, history } = setup({ files: { 'a.md': 'moja wersja', 'b.md': 'do usunięcia' } });
    await vault.open();
    await vault.read('a.md');
    await vault.read('b.md');
    vault.startWatching();
    fs.externalWrite('a.md', 'wersja z Syncthinga');
    fs.externalRemove('b.md');
    await vault.idle();
    const a = await history!.list('a.md');
    expect(a.map((e) => e.reason)).toEqual(['external']);
    expect(await history!.get(a[0]!.id)).toBe('moja wersja');
    const b = await history!.list('b.md');
    expect(await history!.get(b[0]!.id)).toBe('do usunięcia');
  });

  it('detects external renames of files (content unchanged) and keeps history', async () => {
    const { vault, fs, all, history, clock } = setup({ files: { 'a.md': 'v0' } });
    await vault.open();
    await vault.write('a.md', 'v1');
    clock.advance(1000);
    vault.startWatching();
    fs.externalRename('a.md', 'Przeniesione/ą.md');
    await vault.idle();
    expect(short(all()).slice(1)).toEqual(['rename:a.md->Przeniesione/ą.md@external']);
    expect(await history!.list('Przeniesione/ą.md')).toHaveLength(1);
    expect(vault.exists('Przeniesione')).toBe(true);
  });

  it('treats a rename with changed content as delete + create', async () => {
    const { vault, fs, all } = setup({ files: { 'a.md': 'v0' } });
    await vault.open();
    await vault.read('a.md');
    vault.startWatching();
    fs.externalRename('a.md', 'b.md', { silent: true });
    fs.externalWrite('b.md', 'changed', { silent: true });
    // A single rename event arrives after both changes happened.
    fs.externalEvents([{ type: 'rename', path: 'b.md', oldPath: 'a.md' }]);
    await vault.idle();
    expect(short(all())).toEqual(['delete:a.md@external', 'create:b.md@external']);
  });

  it('detects external folder renames', async () => {
    const { vault, fs, all } = setup({ files: { 'F/a.md': 'a', 'F/sub/b.md': 'b' } });
    await vault.open();
    vault.startWatching();
    fs.externalRename('F', 'G');
    await vault.idle();
    expect(short(all()).sort()).toEqual(['rename:F/a.md->G/a.md@external', 'rename:F/sub/b.md->G/sub/b.md@external']);
    expect(vault.folders().map((f) => f.path)).toEqual(['G', 'G/sub']);
  });

  it('a folder moved in from an ignored location appears with create events', async () => {
    const { vault, fs, all } = setup({ files: { '.stversions/Old/a.md': 'a', '.stversions/Old/b/c.md': 'c' } });
    await vault.open();
    vault.startWatching();
    fs.externalRename('.stversions/Old', 'Odzyskane');
    await vault.idle();
    expect(short(all()).sort()).toEqual(['create:Odzyskane/a.md@external', 'create:Odzyskane/b/c.md@external']);
  });

  it('a new folder event scans its contents', async () => {
    const { vault, fs, all } = setup();
    await vault.open();
    vault.startWatching();
    fs.externalWrite('Nowy/a.md', 'a', { silent: true });
    fs.externalWrite('Nowy/b.md', 'b', { silent: true });
    fs.externalEvents([{ type: 'create', path: 'Nowy' }]);
    await vault.idle();
    expect(short(all()).sort()).toEqual(['create:Nowy/a.md@external', 'create:Nowy/b.md@external']);
  });

  it('ignores events for ignored paths and reloads config on external change', async () => {
    const { vault, fs, all } = setup();
    await vault.open();
    vault.startWatching();
    fs.externalWrite('.obsidian/workspace.json', '{}');
    fs.externalWrite('~syncthing~x.md.tmp', 'tmp');
    fs.externalWrite(CONFIG_PATH, '{"journalsFolder":"Dni"}');
    await vault.idle();
    expect(short(all())).toEqual([`create:${CONFIG_PATH}@external`]);
    expect(vault.config.journalsFolder).toBe('Dni');
    fs.externalRemove(CONFIG_PATH);
    await vault.idle();
    expect(vault.config).toEqual(DEFAULT_VAULT_CONFIG);
  });

  it('binary files are compared by size/mtime', async () => {
    const { vault, fs, all, clock } = setup({ files: { 'img.png': new Uint8Array([1]) } });
    await vault.open();
    vault.startWatching();
    clock.advance(1000);
    fs.externalWrite('img.png', new Uint8Array([1, 2]));
    await vault.idle();
    expect(short(all())).toEqual(['modify:img.png@external']);
  });

  it('startWatching is idempotent and a no-op without fs.watch', async () => {
    const { vault, fs } = setup();
    await vault.open();
    const spy = vi.spyOn(fs, 'watch');
    vault.startWatching();
    vault.startWatching();
    expect(spy).toHaveBeenCalledTimes(1);
    await vault.close();
    expect(vault.watching).toBe(false);

    const noWatch = new MemoryFs();
    (noWatch as unknown as { watch: undefined }).watch = undefined;
    const v2 = new Vault(noWatch, { id: 'x', name: 'x', store: LocalStore.memory('x') });
    v2.startWatching();
    expect(v2.watching).toBe(false);
  });

  it('events racing with a user write never produce a false external change', async () => {
    const { vault, fs, all } = setup({ reportOwnWrites: true, files: { 'a.md': 'a' } });
    await vault.open();
    vault.startWatching();
    const writes = Array.from({ length: 10 }, (_, i) => vault.write('a.md', `typing ${i}`));
    await Promise.all(writes);
    await vault.idle();
    expect(all().every((c) => c.source === 'user')).toBe(true);
    expect(fs.dump()['a.md']).toBe('typing 9');
  });
});

describe('Vault.rescan', () => {
  it('detects changes made while not watching', async () => {
    const { vault, fs, clock, batches } = setup({ files: { 'a.md': 'a', 'b.md': 'b', 'c.md': 'c', 'img.png': new Uint8Array([1]) } });
    await vault.open();
    await vault.read('a.md');
    await vault.read('c.md');
    clock.advance(60_000);
    fs.externalWrite('a.md', 'a2', { silent: true });
    fs.externalWrite('c.md', 'c', { silent: true }); // touched, same content
    fs.externalRemove('b.md', { silent: true });
    fs.externalWrite('d/nowy.md', 'd', { silent: true });
    fs.externalWrite('img.png', new Uint8Array([1, 2]), { silent: true });
    fs.externalMkdir('pusty', { silent: true });
    const changes = await vault.rescan();
    await vault.idle();
    expect(short(changes).sort()).toEqual(['create:d/nowy.md@external', 'delete:b.md@external', 'modify:a.md@external', 'modify:img.png@external']);
    expect(batches).toEqual([changes]);
    expect(vault.folders().map((f) => f.path)).toEqual(['d', 'pusty']);
    expect(await vault.read('a.md')).toBe('a2');
    expect(vault.stat('c.md')?.mtime).toBe(START + 60_000);
    expect(await vault.rescan()).toEqual([]);
  });

  it('drops folders that disappeared', async () => {
    const { vault, fs } = setup({ files: { 'F/a.md': 'a' } });
    await vault.open();
    await fs.mkdir('E');
    await vault.rescan();
    expect(vault.folders().map((f) => f.path)).toEqual(['E', 'F']);
    fs.externalRemove('F', { silent: true });
    fs.externalRemove('E', { silent: true });
    await vault.rescan();
    expect(vault.folders()).toEqual([]);
  });

  it('modified files without a known hash are reported without reading them', async () => {
    const { vault, fs, clock } = setup({ files: { 'a.md': 'a' } });
    await vault.open();
    clock.advance(1);
    fs.externalWrite('a.md', 'b', { silent: true });
    expect(short(await vault.rescan())).toEqual(['modify:a.md@external']);
  });
});

describe('Vault with other backends', () => {
  it('works end-to-end on IdbFs, including multi-tab change propagation', async () => {
    const id = `vault-idb-${Date.now()}`;
    const fsA = new IdbFs(id);
    const fsB = new IdbFs(id);
    const tabA = new Vault(fsA, { id, name: 'A', store: LocalStore.memory('a') });
    const tabB = new Vault(fsB, { id, name: 'B', store: LocalStore.memory('b') });
    await tabA.open();
    await tabB.open();
    const seenB: VaultChange[] = [];
    tabB.onChange((c) => seenB.push(...c));
    tabB.startWatching();
    try {
      await tabA.write('Dziennik/2026-10-14.md', '- spotkanie z Łucją');
      for (let i = 0; i < 200 && seenB.length === 0; i++) await new Promise((r) => setTimeout(r, 5));
      await tabB.idle();
      expect(short(seenB)).toEqual(['create:Dziennik/2026-10-14.md@external']);
      expect(await tabB.read('Dziennik/2026-10-14.md')).toBe('- spotkanie z Łucją');
      await tabA.delete('Dziennik/2026-10-14.md');
      expect((await tabA.trash()).map((t) => t.path)).toEqual(['.trash/Dziennik/2026-10-14.md']);
      expect(await tabA.restore('.trash/Dziennik/2026-10-14.md')).toBe('Dziennik/2026-10-14.md');
    } finally {
      await tabB.close();
      fsA.close();
      fsB.close();
      await IdbFs.deleteVault(id);
    }
  });

  it('works on FsAccessFs with polling', async () => {
    const clock = makeClock(START);
    const root = createFakeRoot('Notatki', { now: clock.now, move: 'files' });
    const fs = new FsAccessFs(root, { pollInterval: 60_000 });
    const vault = new Vault(fs, { id: 'fsa', name: 'Notatki', store: LocalStore.memory('fsa') });
    await fakeWrite(root, 'a.md', 'a');
    await vault.open();
    const seen: VaultChange[] = [];
    vault.onChange((c) => seen.push(...c));
    vault.startWatching();
    await fs.checkNow();
    await vault.write('a.md', 'moja');
    await vault.rename('a.md', 'F/b.md');
    await fs.checkNow();
    await vault.idle();
    expect(seen.every((c) => c.source === 'user')).toBe(true);
    clock.advance(10);
    await fakeWrite(root, 'F/b.md', 'z zewnątrz');
    await fs.checkNow();
    await vault.idle();
    expect(short(seen.filter((c) => c.source === 'external'))).toEqual(['modify:F/b.md@external']);
    await vault.close();
  });
});

describe('Vault review fixes', () => {
  it('a case-only rename never overwrites a distinct file on a case-sensitive backend', async () => {
    const { vault, fs } = setup({ files: { 'Notatka.md': 'duża', 'notatka.md': 'mała' } });
    await vault.open();
    await expect(vault.rename('Notatka.md', 'notatka.md')).rejects.toSatisfy((e: unknown) => isFsError(e, 'EEXIST'));
    expect(fs.dump()).toEqual({ 'Notatka.md': 'duża', 'notatka.md': 'mała' });
    expect(vault.files().map((f) => f.path)).toEqual(['Notatka.md', 'notatka.md']);
  });

  it('a case-only rename onto a distinct file unknown to the vault is refused too', async () => {
    const { vault, fs, clock } = setup({ files: { 'Żaba.md': 'moja żaba' } });
    await vault.open();
    clock.advance(1000);
    fs.externalWrite('żaba.md', 'cudza żaba, dłuższa', { silent: true });
    await expect(vault.rename('Żaba.md', 'żaba.md')).rejects.toSatisfy((e: unknown) => isFsError(e, 'EEXIST'));
    expect(fs.dump()).toEqual({ 'Żaba.md': 'moja żaba', 'żaba.md': 'cudza żaba, dłuższa' });
  });

  it('case-only renames work on case-insensitive folders (FsAccessFs) without losing the note', async () => {
    const clock = makeClock(START);
    const root = createFakeRoot('Notatki', { now: clock.now, caseInsensitive: true });
    const fs = new FsAccessFs(root, { pollInterval: 60_000 });
    await fakeWrite(root, 'Zakupy/Lista zakupów.md', '- mleko\n- chleb');
    const vault = new Vault(fs, { id: 'ci', name: 'Notatki', store: LocalStore.memory('ci') });
    await vault.open();
    const seen: VaultChange[] = [];
    vault.onChange((c) => seen.push(...c));
    await vault.rename('Zakupy/Lista zakupów.md', 'Zakupy/lista Zakupów.md');
    await vault.rename('Zakupy', 'zakupy');
    await vault.idle();
    expect(vault.files().map((f) => f.path)).toEqual(['zakupy/lista Zakupów.md']);
    expect(await fs.readText('zakupy/lista Zakupów.md')).toBe('- mleko\n- chleb');
    expect(short(seen)).toEqual([
      'rename:Zakupy/Lista zakupów.md->Zakupy/lista Zakupów.md@user',
      'rename:Zakupy/lista Zakupów.md->zakupy/lista Zakupów.md@user',
    ]);
  });

  it('a write never silently overwrites an external change the watcher has not reported yet', async () => {
    const { vault, fs, clock, history, all } = setup({ files: { 'a.md': 'v0' } });
    await vault.open();
    await vault.write('a.md', 'v1');
    clock.advance(2000);
    // Syncthing replaces the file; the watcher event is still on its way (or not watching at all).
    fs.externalWrite('a.md', 'z Syncthinga', { silent: true });
    clock.advance(1000);
    await vault.write('a.md', 'moje zmiany');
    await vault.idle();
    expect(fs.dump()['a.md']).toBe('moje zmiany');
    expect(short(all())).toEqual(['modify:a.md@user', 'modify:a.md@external', 'modify:a.md@user']);
    const list = await history!.list('a.md');
    const contents = await Promise.all(list.map((e) => history!.get(e.id)));
    expect(contents).toEqual(['z Syncthinga', 'v1', 'v0']);
    expect(list.map((e) => e.reason)).toEqual(['user', 'external', 'user']);
  });

  it('a write after an unseen external delete reports the delete and then a create', async () => {
    const { vault, fs, history, all } = setup({ files: { 'a.md': 'stara treść' } });
    await vault.open();
    await vault.read('a.md');
    fs.externalRemove('a.md', { silent: true });
    await vault.write('a.md', 'nowa treść');
    await vault.idle();
    expect(short(all())).toEqual(['delete:a.md@external', 'create:a.md@user']);
    const list = await history!.list('a.md');
    expect(await history!.get(list[0]!.id)).toBe('stara treść');
  });

  it('an unseen external change identical to the new content makes the write a no-op', async () => {
    const { vault, fs, clock, all } = setup({ files: { 'a.md': 'v0' } });
    await vault.open();
    await vault.read('a.md');
    clock.advance(1000);
    fs.externalWrite('a.md', 'to samo', { silent: true });
    const spy = vi.spyOn(fs, 'writeText');
    await vault.write('a.md', 'to samo');
    await vault.idle();
    expect(spy).not.toHaveBeenCalled();
    expect(short(all())).toEqual(['modify:a.md@external']);
  });

  it('open() again with an empty (or ignored-only) listing clears files()', async () => {
    const { vault, fs } = setup({ files: { 'a.md': 'a', '.obsidian/app.json': '{}' } });
    await vault.open();
    expect(vault.files()).toHaveLength(1);
    fs.externalRemove('a.md', { silent: true });
    await vault.open();
    expect(vault.files()).toEqual([]);
    expect(vault.markdownFiles()).toEqual([]);
  });

  it('create() keeps the extension case of the requested name', async () => {
    const { vault } = setup({ files: { 'Raport.PDF': new Uint8Array([1]) } });
    await vault.open();
    expect(await vault.create('Raport.PDF', new Uint8Array([2]))).toBe('Raport 2.PDF');
    expect(await vault.create('Szkic.Md', 'a')).toBe('Szkic.Md');
    expect(await vault.create('szkic.md', 'b')).toBe('szkic 2.md');
  });

  it('create() sees names freed or taken by renames, deletes and rescans (case-insensitive)', async () => {
    const { vault, fs } = setup({ files: { 'Ala.md': 'a', 'F/x.md': 'x' } });
    await vault.open();
    await vault.rename('Ala.md', 'Ola.md');
    expect(await vault.create('ala.md', '1')).toBe('ala.md');
    expect(await vault.create('OLA.md', '2')).toBe('OLA 2.md');
    await vault.delete('Ola.md');
    expect(await vault.create('ola.md', '3')).toBe('ola.md');
    await vault.rename('F', 'G');
    expect(await vault.create('f', '4')).toBe('f');
    expect(await vault.create('g', '5')).toBe('g 2');
    fs.externalRemove('ala.md', { silent: true });
    await vault.rescan();
    expect(await vault.create('Ala.md', '6')).toBe('Ala.md');
  });

  it('cached stats handed out are read-only (callers cannot corrupt the cache)', async () => {
    const { vault } = setup({ files: { 'a.md': 'a', 'F/b.md': 'b' } });
    await vault.open();
    expect(Object.isFrozen(vault.files()[0])).toBe(true);
    expect(Object.isFrozen(vault.stat('a.md'))).toBe(true);
    expect(Object.isFrozen(vault.folders()[0])).toBe(true);
  });

  it('ignores unusable trash metadata (empty or trash-internal original path)', async () => {
    const { vault, store } = setup({ files: { '.trash/x.md': 'x', '.trash/y.md': 'y' } });
    await vault.open();
    await store.set('vault/v1/trash/.trash/x.md', { original: '', deletedAt: 1 });
    await store.set('vault/v1/trash/.trash/y.md', { original: '.trash/z.md', deletedAt: 1 });
    expect(await vault.trashInfo('.trash/x.md')).toEqual({ original: 'x.md' });
    expect(await vault.trashInfo('.trash/y.md')).toEqual({ original: 'y.md' });
    expect(await vault.restore('.trash/x.md')).toBe('x.md');
  });
});

describe('Vault: external case-only renames on case-insensitive backends', () => {
  async function ciVault(files: Record<string, string>) {
    const clock = makeClock(START);
    const root = createFakeRoot('Notatki', { now: clock.now, caseInsensitive: true, move: 'all' });
    for (const [p, c] of Object.entries(files)) await fakeWrite(root, p, c);
    const fs = new FsAccessFs(root, { pollInterval: 60_000 });
    const history = new History(LocalStore.memory('h'), { now: clock.now });
    const vault = new Vault(fs, { id: 'ci', name: 'Notatki', store: LocalStore.memory('ci'), history, now: clock.now });
    await vault.open();
    const seen: VaultChange[] = [];
    vault.onChange((c) => seen.push(...c));
    return { root, fs, vault, seen, history, clock };
  }

  it('watcher: a file renamed by case only replaces the old entry (no duplicate)', async () => {
    const { root, fs, vault, seen, history, clock } = await ciVault({ 'Notatka o źródłach.md': 'v0' });
    await vault.write('Notatka o źródłach.md', 'v1');
    clock.advance(1000);
    vault.startWatching();
    await fs.checkNow();
    const fh = await root.getFileHandle('Notatka o źródłach.md');
    await fh.move!(root, 'notatka o Źródłach.md');
    await fs.checkNow();
    await vault.idle();
    expect(vault.files().map((f) => f.path)).toEqual(['notatka o Źródłach.md']);
    expect(short(seen.filter((c) => c.source === 'external'))).toEqual(['rename:Notatka o źródłach.md->notatka o Źródłach.md@external']);
    expect(await history.list('notatka o Źródłach.md')).toHaveLength(1);
    await vault.close();
  });

  it('watcher: a folder renamed by case only moves every file', async () => {
    const { root, fs, vault, seen } = await ciVault({ 'Projekt/a.md': 'a', 'Projekt/Dom/b.md': 'b' });
    vault.startWatching();
    await fs.checkNow();
    const dir = await root.getDirectoryHandle('Projekt');
    await dir.move!(root, 'projekt');
    await fs.checkNow();
    await vault.idle();
    expect(vault.files().map((f) => f.path)).toEqual(['projekt/Dom/b.md', 'projekt/a.md']);
    expect(vault.folders().map((f) => f.path)).toEqual(['projekt', 'projekt/Dom']);
    expect(short(seen).sort()).toEqual(['rename:Projekt/Dom/b.md->projekt/Dom/b.md@external', 'rename:Projekt/a.md->projekt/a.md@external']);
    await vault.close();
  });

  it('rescan: a case-only rename made while not watching is picked up', async () => {
    const { root, vault, seen } = await ciVault({ 'Lista.md': 'lista' });
    const fh = await root.getFileHandle('Lista.md');
    await fh.move!(root, 'LISTA.md');
    const changes = await vault.rescan();
    await vault.idle();
    expect(short(changes)).toEqual(['rename:Lista.md->LISTA.md@external']);
    expect(short(seen)).toEqual(['rename:Lista.md->LISTA.md@external']);
    expect(vault.files().map((f) => f.path)).toEqual(['LISTA.md']);
    expect(await vault.rescan()).toEqual([]);
  });

  it('case-sensitive backends still see delete + create for distinct names', async () => {
    const { vault, fs, all } = setup({ files: { 'A.md': 'duże' } });
    await vault.open();
    vault.startWatching();
    fs.externalRemove('A.md');
    fs.externalWrite('a.md', 'małe');
    await vault.idle();
    expect(short(all())).toEqual(['delete:A.md@external', 'create:a.md@external']);
    fs.externalRemove('a.md', { silent: true });
    fs.externalWrite('A.md', 'znowu', { silent: true });
    expect(short(await vault.rescan()).sort()).toEqual(['create:A.md@external', 'delete:a.md@external']);
  });
});

describe('Vault locking granularity', () => {
  it('writes to different files in one folder overlap, and reads do not wait for other files', async () => {
    const { vault, fs } = setup({ files: { 'Import/gotowa.md': 'gotowa', 'Import/a.md': '' } });
    await vault.open();
    const original = fs.writeText.bind(fs);
    let active = 0;
    let maxActive = 0;
    let release!: () => void;
    const gate = new Promise<void>((r) => (release = r));
    vi.spyOn(fs, 'writeText').mockImplementation(async (p, c) => {
      active++;
      maxActive = Math.max(maxActive, active);
      if (p !== 'Import/a.md') await gate;
      const st = await original(p, c);
      active--;
      return st;
    });
    const slow = [vault.write('Import/b.md', 'b'), vault.write('Import/c.md', 'c')];
    await vault.write('Import/a.md', 'a'); // not blocked by the stuck writes of b and c
    expect(await vault.read('Import/gotowa.md')).toBe('gotowa');
    expect(maxActive).toBe(3);
    release();
    await Promise.all(slow);
    expect(fs.dump()).toMatchObject({ 'Import/a.md': 'a', 'Import/b.md': 'b', 'Import/c.md': 'c' });
  });

  it('a folder rename still waits for writes inside it, and later writes wait for the rename', async () => {
    const { vault, fs } = setup({ files: { 'F/a.md': 'old', 'F/b.md': 'old' } });
    await vault.open();
    const w1 = vault.write('F/a.md', 'new a');
    const r = vault.rename('F', 'G');
    const w2 = vault.write('F/b.md', 'after rename');
    await Promise.all([w1, r, w2]);
    expect(fs.dump()).toEqual({ 'G/a.md': 'new a', 'G/b.md': 'old', 'F/b.md': 'after rename' });
  });
});

describe('Vault: watcher overflow (events lost)', () => {
  it('an event for the vault root triggers a full rescan', async () => {
    const { vault, fs, all, clock } = setup({ files: { 'a.md': 'a', 'b.md': 'b' } });
    await vault.open();
    await vault.read('a.md');
    vault.startWatching();
    clock.advance(1000);
    fs.externalWrite('a.md', 'a2', { silent: true });
    fs.externalRemove('b.md', { silent: true });
    fs.externalWrite('Nowy folder/ć.md', 'ć', { silent: true });
    // e.g. platform/tauri.ts forwarding `rescan: true` from the native watcher
    fs.externalEvents([{ type: 'modify', path: '' }]);
    await vault.idle();
    expect(short(all()).sort()).toEqual(['create:Nowy folder/ć.md@external', 'delete:b.md@external', 'modify:a.md@external']);
  });
});

describe('Vault: malformed watcher events', () => {
  it('skips malformed events and still processes the valid ones of the batch', async () => {
    const { vault, fs, all } = setup();
    await vault.open();
    vault.startWatching();
    fs.externalWrite('ok.md', 'ok', { silent: true });
    const junk = [null, { type: 'modify' }, { type: 'explode', path: 'x.md' }, { type: 'rename', path: 'y.md', oldPath: 5 }] as unknown as FsEvent[];
    fs.externalEvents([...junk, { type: 'create', path: 'ok.md' }]);
    await vault.idle();
    expect(short(all())).toEqual(['create:ok.md@external']);
  });
});

describe('Vault.writeBinary on text files', () => {
  it('snapshots the previous text of a note overwritten with bytes (e.g. a sync download)', async () => {
    const { vault, history, all } = setup({ files: { 'Notatka.md': 'lokalna wersja — zażółć' } });
    await vault.open();
    await vault.writeBinary('Notatka.md', new TextEncoder().encode('zdalna wersja'), 'sync');
    await vault.idle();
    const list = await history!.list('Notatka.md');
    expect(list.map((e) => e.reason)).toEqual(['sync']);
    expect(await history!.get(list[0]!.id)).toBe('lokalna wersja — zażółć');
    expect(await vault.read('Notatka.md')).toBe('zdalna wersja');
    expect(short(all())).toEqual(['modify:Notatka.md@sync']);
  });

  it('bytes equal to the current text are a no-op', async () => {
    const { vault, fs } = setup({ files: { 'a.md': 'ćma' } });
    await vault.open();
    const spy = vi.spyOn(fs, 'writeBinary');
    await vault.writeBinary('a.md', new TextEncoder().encode('ćma'), 'sync');
    expect(spy).not.toHaveBeenCalled();
  });
});
