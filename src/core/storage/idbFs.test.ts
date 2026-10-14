import { describe, expect, it } from 'vitest';
import type { FsEvent } from '../types';
import { describeAdapterContract, makeClock } from './adapterContract.testutil';
import { openCachedDb } from './idb';
import { IDB_FS_DB, IdbFs, newestMtime } from './idbFs';

let counter = 0;
const uniqueId = (): string => `vault-${Date.now()}-${counter++}`;

describeAdapterContract('IdbFs', (clock) => new IdbFs(uniqueId(), undefined, { now: clock.now, broadcast: false }));

function waitFor(cond: () => boolean, timeout = 2000): Promise<void> {
  return new Promise((resolve, reject) => {
    const start = Date.now();
    const tick = (): void => {
      if (cond()) resolve();
      else if (Date.now() - start > timeout) reject(new Error('timeout'));
      else setTimeout(tick, 5);
    };
    tick();
  });
}

describe('IdbFs specifics', () => {
  it('has a default label and kind', () => {
    const fs = new IdbFs('abc');
    expect(fs.kind).toBe('idb');
    expect(fs.label).toContain('abc');
    expect(new IdbFs('abc', 'Przeglądarka').label).toBe('Przeglądarka');
  });

  it('rejects an empty vault id', () => {
    expect(() => new IdbFs('')).toThrow();
  });

  it('persists data across instances', async () => {
    const id = uniqueId();
    await new IdbFs(id, undefined, { broadcast: false }).writeText('dziennik/2026-10-14.md', '- [ ] zadanie ⏰ 2026-10-14 09:00');
    const again = new IdbFs(id, undefined, { broadcast: false });
    expect(await again.readText('dziennik/2026-10-14.md')).toBe('- [ ] zadanie ⏰ 2026-10-14 09:00');
  });

  it('isolates vaults from each other', async () => {
    const a = new IdbFs(uniqueId(), undefined, { broadcast: false });
    const b = new IdbFs(uniqueId(), undefined, { broadcast: false });
    await a.writeText('same.md', 'A');
    await b.writeText('same.md', 'B');
    expect(await a.readText('same.md')).toBe('A');
    expect(await b.readText('same.md')).toBe('B');
    await a.remove('same.md');
    expect(await b.readText('same.md')).toBe('B');
    expect((await a.list()).length).toBe(0);
  });

  it('does not confuse vault ids that are prefixes of each other', async () => {
    const base = uniqueId();
    const a = new IdbFs(base, undefined, { broadcast: false });
    const b = new IdbFs(`${base}x`, undefined, { broadcast: false });
    await a.writeText('a.md', 'a');
    await b.writeText('b.md', 'b');
    expect((await a.list()).map((s) => s.path)).toEqual(['a.md']);
    expect((await b.list()).map((s) => s.path)).toEqual(['b.md']);
  });

  it('deleteVault removes everything of one vault only', async () => {
    const id = uniqueId();
    const other = new IdbFs(uniqueId(), undefined, { broadcast: false });
    const fs = new IdbFs(id, undefined, { broadcast: false });
    await fs.writeText('a/b.md', 'x');
    await fs.mkdir('empty');
    await other.writeText('keep.md', 'k');
    await IdbFs.deleteVault(id);
    expect(await fs.list()).toEqual([]);
    expect(await other.readText('keep.md')).toBe('k');
  });

  it('implied directory mtime is the newest descendant', async () => {
    const clock = makeClock(1000);
    const fs = new IdbFs(uniqueId(), undefined, { now: clock.now, broadcast: false });
    await fs.writeText('d/a.md', 'a');
    clock.advance(500);
    await fs.writeText('d/sub/b.md', 'b');
    const dir = (await fs.list()).find((s) => s.path === 'd');
    expect(dir).toMatchObject({ kind: 'dir', mtime: 1500 });
    expect(await fs.stat('d')).toMatchObject({ kind: 'dir', mtime: 1500 });
  });

  it('a failed write leaves nothing behind (transaction aborted)', async () => {
    const fs = new IdbFs(uniqueId(), undefined, { broadcast: false });
    await fs.writeText('file.md', 'x');
    await expect(fs.writeText('file.md/child.md', 'y')).rejects.toBeTruthy();
    expect((await fs.list()).map((s) => s.path)).toEqual(['file.md']);
  });

  it('stores binary data as bytes and keeps paths with Polish characters', async () => {
    const fs = new IdbFs(uniqueId(), undefined, { broadcast: false });
    await fs.writeBinary('załączniki/zdjęcie.png', new Uint8Array([137, 80, 78, 71]));
    expect([...(await fs.readBinary('załączniki/zdjęcie.png'))]).toEqual([137, 80, 78, 71]);
    expect((await fs.stat('załączniki/zdjęcie.png'))?.size).toBe(4);
  });

  it('broadcasts changes to other instances of the same vault (other tabs)', async () => {
    const id = uniqueId();
    const tabA = new IdbFs(id);
    const tabB = new IdbFs(id);
    const otherVault = new IdbFs(uniqueId());
    const seenB: FsEvent[] = [];
    const seenA: FsEvent[] = [];
    const seenOther: FsEvent[] = [];
    const stopB = tabB.watch((e) => seenB.push(...e));
    const stopA = tabA.watch((e) => seenA.push(...e));
    const stopO = otherVault.watch((e) => seenOther.push(...e));
    try {
      await tabA.writeText('a.md', 'A');
      await tabA.writeText('a.md', 'A2');
      await tabA.rename('a.md', 'b.md');
      await tabA.remove('b.md');
      await waitFor(() => seenB.length >= 4);
      expect(seenB).toEqual([
        { type: 'create', path: 'a.md' },
        { type: 'modify', path: 'a.md' },
        { type: 'rename', path: 'b.md', oldPath: 'a.md' },
        { type: 'delete', path: 'b.md' },
      ]);
      // Own changes are not echoed to the writer, other vaults hear nothing.
      await new Promise((r) => setTimeout(r, 30));
      expect(seenA).toEqual([]);
      expect(seenOther).toEqual([]);
    } finally {
      stopA();
      stopB();
      stopO();
      tabA.close();
      tabB.close();
      otherVault.close();
    }
  });

  it('rename of an implied directory keeps contents and mtimes', async () => {
    const clock = makeClock(42);
    const fs = new IdbFs(uniqueId(), undefined, { now: clock.now, broadcast: false });
    await fs.writeText('stary/a.md', 'a');
    clock.advance(1000);
    await fs.rename('stary', 'nowy');
    expect(await fs.stat('nowy/a.md')).toMatchObject({ mtime: 42, size: 1 });
    expect(await fs.stat('stary')).toBeNull();
  });
});

describe('IdbFs folders behave like on disk', () => {
  it('keeps a folder when its last file is removed or moved away', async () => {
    const fs = new IdbFs(uniqueId(), undefined, { broadcast: false });
    await fs.writeText('Projekty/Plan ćwiczeń.md', 'p');
    await fs.writeText('Archiwum/Stary/x.md', 'x');
    await fs.remove('Projekty/Plan ćwiczeń.md');
    await fs.rename('Archiwum/Stary/x.md', 'x.md');
    expect(await fs.stat('Projekty')).toMatchObject({ kind: 'dir' });
    expect(await fs.stat('Archiwum/Stary')).toMatchObject({ kind: 'dir' });
    expect((await fs.list()).map((s) => s.path)).toEqual(['Archiwum', 'Archiwum/Stary', 'Projekty', 'x.md']);
    await fs.rename('Archiwum', 'Kosz/Archiwum');
    expect((await fs.list()).map((s) => s.path)).toEqual(['Kosz', 'Kosz/Archiwum', 'Kosz/Archiwum/Stary', 'Projekty', 'x.md']);
    await fs.remove('Projekty');
    await fs.remove('Kosz');
    expect((await fs.list()).map((s) => s.path)).toEqual(['x.md']);
  });
});

describe('newestMtime', () => {
  it('handles folders with very many descendants (no argument-spread stack overflow)', () => {
    const records = Array.from({ length: 300_000 }, (_, i) => ({ mtime: (i * 7919) % 1_000_003 }));
    records[123_456] = { mtime: 5_000_000 };
    expect(newestMtime(records)).toBe(5_000_000);
    // Allocating 300k records can take seconds on a loaded Raspberry Pi.
  }, 30_000);

  it('returns 0 for no records', () => {
    expect(newestMtime([])).toBe(0);
  });
});

describe('IdbFs when the browser drops the database connection', () => {
  it('reopens the database and keeps working (closed with or without notice)', async () => {
    const fs = new IdbFs(uniqueId(), undefined, { broadcast: false });
    await fs.writeText('Dziennik/środa.md', '- przed');
    // Without notice (e.g. Safari "Connection to Indexed Database server lost").
    (await openCachedDb(IDB_FS_DB, 1, () => {})).close();
    expect(await fs.readText('Dziennik/środa.md')).toBe('- przed');
    // Evicted (browser terminated the connection, `terminated` callback).
    await IdbFs.closeDatabase();
    await fs.writeText('Dziennik/środa.md', '- po');
    (await openCachedDb(IDB_FS_DB, 1, () => {})).close();
    expect((await fs.list()).map((s) => s.path)).toEqual(['Dziennik', 'Dziennik/środa.md']);
    expect(await fs.readText('Dziennik/środa.md')).toBe('- po');
  });
});
