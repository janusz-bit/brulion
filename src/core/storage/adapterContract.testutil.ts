/**
 * Behavioural contract shared by every FileSystemAdapter implementation (tests only).
 * Each adapter's test file calls `describeAdapterContract` with a factory.
 */
import { beforeEach, describe, expect, it } from 'vitest';
import type { FileSystemAdapter } from '../types';
import { isFsError, isNotFoundError } from './errors';

export interface ContractClock {
  now: () => number;
  advance(ms: number): void;
}

export function makeClock(start = Date.UTC(2026, 9, 14, 8, 0, 0)): ContractClock {
  let t = start;
  return {
    now: () => t,
    advance(ms: number) {
      t += ms;
    },
  };
}

async function rejection(p: Promise<unknown>): Promise<unknown> {
  try {
    await p;
  } catch (e) {
    return e;
  }
  throw new Error('expected promise to reject');
}

const enc = new TextEncoder();

export function describeAdapterContract(
  name: string,
  make: (clock: ContractClock) => Promise<FileSystemAdapter> | FileSystemAdapter,
): void {
  describe(`${name}: FileSystemAdapter contract`, () => {
    let clock: ContractClock;
    let fs: FileSystemAdapter;

    beforeEach(async () => {
      clock = makeClock();
      fs = await make(clock);
    });

    it('writes and reads text with Polish diacritics; size counts UTF-8 bytes', async () => {
      const text = 'Zażółć gęślą jaźń — ĄĆĘŁŃÓŚŹŻ\n';
      const st = await fs.writeText('Notatki/Zażółć.md', text);
      expect(st).toMatchObject({ path: 'Notatki/Zażółć.md', kind: 'file', size: enc.encode(text).byteLength });
      expect(await fs.readText('Notatki/Zażółć.md')).toBe(text);
      expect(await fs.stat('Notatki/Zażółć.md')).toMatchObject({ kind: 'file', size: st.size, mtime: st.mtime });
    });

    it('normalises paths (slashes, dots, NFC)', async () => {
      await fs.writeText('./a//b\\c.md', 'x');
      expect(await fs.readText('a/b/c.md')).toBe('x');
      await fs.writeText('Żâ.md'.normalize('NFD'), 'nfd');
      expect(await fs.readText('Żâ.md'.normalize('NFC'))).toBe('nfd');
    });

    it('lists files and parent directories recursively', async () => {
      await fs.writeText('a.md', '1');
      await fs.writeText('dir/sub/b.md', '22');
      await fs.mkdir('empty/inner');
      const list = await fs.list();
      const byPath = new Map(list.map((s) => [s.path, s]));
      expect(byPath.get('a.md')).toMatchObject({ kind: 'file', size: 1 });
      expect(byPath.get('dir/sub/b.md')).toMatchObject({ kind: 'file', size: 2 });
      expect(byPath.get('dir')?.kind).toBe('dir');
      expect(byPath.get('dir/sub')?.kind).toBe('dir');
      expect(byPath.get('empty')?.kind).toBe('dir');
      expect(byPath.get('empty/inner')).toMatchObject({ kind: 'dir', size: 0 });
      expect(list.every((s) => !s.path.startsWith('/') && s.path !== '')).toBe(true);
    });

    it('stat: file, directory, missing and root', async () => {
      await fs.writeText('x/y.md', 'abc');
      expect(await fs.stat('x/y.md')).toMatchObject({ path: 'x/y.md', kind: 'file', size: 3 });
      expect(await fs.stat('x')).toMatchObject({ path: 'x', kind: 'dir', size: 0 });
      expect(await fs.stat('nope.md')).toBeNull();
      expect(await fs.stat('x/nope.md')).toBeNull();
      expect(await fs.stat('')).toMatchObject({ kind: 'dir' });
    });

    it('overwrites files and updates mtime', async () => {
      const first = await fs.writeText('n.md', 'one');
      clock.advance(5000);
      const second = await fs.writeText('n.md', 'two, longer');
      expect(await fs.readText('n.md')).toBe('two, longer');
      expect(second.mtime).toBeGreaterThan(first.mtime);
      expect(second.size).toBe(11);
    });

    it('round-trips binary data and converts between text and bytes', async () => {
      const bytes = new Uint8Array([0, 1, 2, 250, 255, 128]);
      await fs.writeBinary('assets/img.bin', bytes);
      const back = await fs.readBinary('assets/img.bin');
      expect([...back]).toEqual([...bytes]);
      back[0] = 99;
      expect((await fs.readBinary('assets/img.bin'))[0]).toBe(0);
      await fs.writeBinary('t.txt', enc.encode('łódź'));
      expect(await fs.readText('t.txt')).toBe('łódź');
      await fs.writeText('u.md', 'ćma');
      expect([...(await fs.readBinary('u.md'))]).toEqual([...enc.encode('ćma')]);
    });

    it('does not keep a reference to the written byte array', async () => {
      const bytes = new Uint8Array([1, 2, 3]);
      await fs.writeBinary('b.bin', bytes);
      bytes[0] = 42;
      expect((await fs.readBinary('b.bin'))[0]).toBe(1);
    });

    it('rejects reads of missing files with a not-found error', async () => {
      const e = await rejection(fs.readText('missing.md'));
      expect(isNotFoundError(e)).toBe(true);
      expect(isNotFoundError(await rejection(fs.readBinary('dir/missing.bin')))).toBe(true);
    });

    it('rejects reading a directory as a file', async () => {
      await fs.mkdir('folder');
      await expect(fs.readText('folder')).rejects.toBeTruthy();
    });

    it('rejects writing over a directory or below a file', async () => {
      await fs.mkdir('folder');
      await expect(fs.writeText('folder', 'x')).rejects.toBeTruthy();
      await fs.writeText('file.md', 'x');
      await expect(fs.writeText('file.md/child.md', 'y')).rejects.toBeTruthy();
      expect(await fs.readText('file.md')).toBe('x');
    });

    it('rejects empty paths for writes', async () => {
      const e = await rejection(fs.writeText('', 'x'));
      expect(isFsError(e, 'EINVAL')).toBe(true);
      await expect(fs.writeText('/', 'x')).rejects.toBeTruthy();
    });

    it('removes files and directories recursively; missing is fine', async () => {
      await fs.writeText('keep.md', 'k');
      await fs.writeText('gone/a.md', 'a');
      await fs.writeText('gone/deep/b.md', 'b');
      await fs.remove('gone');
      await fs.remove('keep-missing.md');
      await fs.remove('no/such/dir');
      const paths = (await fs.list()).map((s) => s.path);
      expect(paths).toContain('keep.md');
      expect(paths.some((p) => p.startsWith('gone'))).toBe(false);
      await fs.remove('keep.md');
      expect(await fs.stat('keep.md')).toBeNull();
    });

    it('remove does not touch siblings sharing a name prefix', async () => {
      await fs.writeText('notes/a.md', 'a');
      await fs.writeText('notes-old/b.md', 'b');
      await fs.writeText('notes.md', 'c');
      await fs.remove('notes');
      expect(await fs.readText('notes-old/b.md')).toBe('b');
      expect(await fs.readText('notes.md')).toBe('c');
    });

    it('renames files, creating parent directories', async () => {
      await fs.writeText('a.md', 'treść');
      await fs.rename('a.md', 'nowy/folder/b.md');
      expect(await fs.stat('a.md')).toBeNull();
      expect(await fs.readText('nowy/folder/b.md')).toBe('treść');
    });

    it('renames directories with their whole subtree', async () => {
      await fs.writeText('Projekty/Dom.md', 'dom');
      await fs.writeText('Projekty/Praca/Plan.md', 'plan');
      await fs.writeBinary('Projekty/Praca/img.bin', new Uint8Array([7]));
      await fs.writeText('Projekty-old.md', 'stays');
      await fs.rename('Projekty', 'Archiwum/Projekty 2025');
      expect(await fs.readText('Archiwum/Projekty 2025/Dom.md')).toBe('dom');
      expect(await fs.readText('Archiwum/Projekty 2025/Praca/Plan.md')).toBe('plan');
      expect([...(await fs.readBinary('Archiwum/Projekty 2025/Praca/img.bin'))]).toEqual([7]);
      expect(await fs.stat('Projekty')).toBeNull();
      expect(await fs.readText('Projekty-old.md')).toBe('stays');
    });

    it('rename: missing source fails with not-found', async () => {
      expect(isNotFoundError(await rejection(fs.rename('missing.md', 'x.md')))).toBe(true);
    });

    it('rename: overwrites an existing file, refuses an existing directory', async () => {
      await fs.writeText('a.md', 'A');
      await fs.writeText('b.md', 'B');
      await fs.rename('a.md', 'b.md');
      expect(await fs.readText('b.md')).toBe('A');
      expect(await fs.stat('a.md')).toBeNull();
      await fs.writeText('dir/x.md', 'x');
      await fs.writeText('c.md', 'C');
      await expect(fs.rename('c.md', 'dir')).rejects.toBeTruthy();
      expect(await fs.readText('c.md')).toBe('C');
    });

    it('rename: refuses moving a directory into itself', async () => {
      await fs.writeText('d/x.md', 'x');
      const e = await rejection(fs.rename('d', 'd/inner'));
      expect(isFsError(e, 'EINVAL')).toBe(true);
      expect(await fs.readText('d/x.md')).toBe('x');
    });

    it('rename to the same path is a no-op', async () => {
      await fs.writeText('same.md', 's');
      await fs.rename('same.md', 'same.md');
      expect(await fs.readText('same.md')).toBe('s');
    });

    it('mkdir is recursive and idempotent; refuses an existing file', async () => {
      await fs.mkdir('a/b/c');
      await fs.mkdir('a/b/c');
      expect((await fs.stat('a/b'))?.kind).toBe('dir');
      expect((await fs.stat('a/b/c'))?.kind).toBe('dir');
      await fs.writeText('f.md', 'x');
      await expect(fs.mkdir('f.md')).rejects.toBeTruthy();
    });

    it('handles many concurrent writes to different files', async () => {
      await Promise.all(Array.from({ length: 25 }, (_, i) => fs.writeText(`bulk/note ${i}.md`, `treść ${i}`)));
      const files = (await fs.list()).filter((s) => s.kind === 'file' && s.path.startsWith('bulk/'));
      expect(files).toHaveLength(25);
      expect(await fs.readText('bulk/note 13.md')).toBe('treść 13');
    });

    it('keeps the last of concurrent writes to the same file', async () => {
      await Promise.all(Array.from({ length: 10 }, (_, i) => fs.writeText('race.md', `v${i}`)));
      expect(await fs.readText('race.md')).toMatch(/^v\d$/);
    });

    it('stores empty files', async () => {
      const st = await fs.writeText('empty.md', '');
      expect(st.size).toBe(0);
      expect(await fs.readText('empty.md')).toBe('');
    });
  });
}
