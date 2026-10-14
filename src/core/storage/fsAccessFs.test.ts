import { afterEach, describe, expect, it, vi } from 'vitest';
import type { FsEvent } from '../types';
import { describeAdapterContract, makeClock } from './adapterContract.testutil';
import { isFsError } from './errors';
import { createFakeRoot, fakeWrite } from './fakeFsAccess.testutil';
import { FsAccessFs, diffSnapshots } from './fsAccessFs';
import { verifyPermission } from './handles';

describeAdapterContract('FsAccessFs (move unsupported for dirs)', (clock) => new FsAccessFs(createFakeRoot('Notatki', { now: clock.now, move: 'files' })));
describeAdapterContract('FsAccessFs (no move)', (clock) => new FsAccessFs(createFakeRoot('Notatki', { now: clock.now, move: 'none' })));
describeAdapterContract('FsAccessFs (move everywhere)', (clock) => new FsAccessFs(createFakeRoot('Notatki', { now: clock.now, move: 'all' })));
describeAdapterContract(
  'FsAccessFs (case-insensitive folder)',
  (clock) => new FsAccessFs(createFakeRoot('Notatki', { now: clock.now, move: 'files', caseInsensitive: true })),
);

describe('diffSnapshots', () => {
  it('reports creates, modifications and deletions', () => {
    const prev = new Map([
      ['a.md', '1:1'],
      ['b.md', '1:1'],
      ['c.md', '1:1'],
    ]);
    const next = new Map([
      ['a.md', '1:1'],
      ['b.md', '2:5'],
      ['d.md', '3:1'],
    ]);
    expect(diffSnapshots(prev, next)).toEqual([
      { type: 'modify', path: 'b.md' },
      { type: 'create', path: 'd.md' },
      { type: 'delete', path: 'c.md' },
    ]);
    expect(diffSnapshots(next, next)).toEqual([]);
  });
});

describe('FsAccessFs specifics', () => {
  afterEach(() => {
    vi.useRealTimers();
  });

  it('labels with the folder name and reports kind', () => {
    const fs = new FsAccessFs(createFakeRoot('Mój Brulion'));
    expect(fs.label).toBe('Mój Brulion');
    expect(fs.kind).toBe('fsaccess');
    expect(new FsAccessFs(createFakeRoot('x'), { label: 'Inny' }).label).toBe('Inny');
  });

  it('uses move() when available and falls back to copy+delete otherwise', async () => {
    const withMove = createFakeRoot('a', { move: 'all' });
    const fsMove = new FsAccessFs(withMove);
    await fsMove.writeText('d/x.md', 'x');
    await fsMove.rename('d', 'e');
    expect(withMove.context.moves).toBe(1);
    expect(await fsMove.readText('e/x.md')).toBe('x');

    const filesOnly = createFakeRoot('b', { move: 'files' });
    const fsCopy = new FsAccessFs(filesOnly);
    await fsCopy.writeText('d/x.md', 'x');
    await fsCopy.writeText('d/sub/y.md', 'y');
    await fsCopy.rename('d', 'e');
    expect(filesOnly.context.moves).toBe(0);
    expect(await fsCopy.readText('e/sub/y.md')).toBe('y');
    expect(await fsCopy.stat('d')).toBeNull();
  });

  it('skips .git and node_modules when listing', async () => {
    const root = createFakeRoot();
    await fakeWrite(root, '.git/HEAD', 'ref');
    await fakeWrite(root, 'node_modules/x/index.js', 'x');
    await fakeWrite(root, 'note.md', 'n');
    const fs = new FsAccessFs(root);
    expect((await fs.list()).map((s) => s.path)).toEqual(['note.md']);
  });

  it('writes atomically: the file keeps old content until the stream is closed', async () => {
    const root = createFakeRoot();
    const fs = new FsAccessFs(root);
    await fs.writeText('a.md', 'old');
    const commits = root.context.commits;
    await fs.writeText('a.md', 'nowa treść');
    expect(root.context.commits).toBe(commits + 1);
    expect(await fs.readText('a.md')).toBe('nowa treść');
  });

  it('aborts the writable when writing fails and keeps the old content', async () => {
    const root = createFakeRoot();
    const fs = new FsAccessFs(root);
    await fs.writeText('a.md', 'old');
    const dir = root;
    const fh = await dir.getFileHandle('a.md');
    const original = fh.createWritable.bind(fh);
    const abort = vi.fn(async () => {});
    fh.createWritable = async () => {
      const w = await original();
      return {
        write: async () => {
          throw new DOMException('disk full', 'QuotaExceededError');
        },
        close: w.close,
        abort,
      };
    };
    await expect(fs.writeText('a.md', 'new')).rejects.toThrow('disk full');
    expect(abort).toHaveBeenCalled();
    expect(await fs.readText('a.md')).toBe('old');
  });

  it('polls for external changes and reports create/modify/delete', async () => {
    vi.useFakeTimers();
    const clock = makeClock(1000);
    const root = createFakeRoot('v', { now: clock.now });
    await fakeWrite(root, 'a.md', 'A');
    await fakeWrite(root, 'b.md', 'B');
    const fs = new FsAccessFs(root, { pollInterval: 1000 });
    const events: FsEvent[][] = [];
    const stop = fs.watch((e) => events.push(e));
    await vi.advanceTimersByTimeAsync(0); // initial snapshot
    clock.advance(10);
    await fakeWrite(root, 'a.md', 'A changed');
    await fakeWrite(root, 'folder/c.md', 'C');
    await root.removeEntry('b.md');
    await vi.advanceTimersByTimeAsync(1000);
    expect(events).toHaveLength(1);
    expect(events[0]).toEqual(
      expect.arrayContaining([
        { type: 'modify', path: 'a.md' },
        { type: 'create', path: 'folder/c.md' },
        { type: 'delete', path: 'b.md' },
      ]),
    );
    expect(events[0]).toHaveLength(3);
    await vi.advanceTimersByTimeAsync(3000);
    expect(events).toHaveLength(1);
    stop();
    await fakeWrite(root, 'z.md', 'Z');
    await vi.advanceTimersByTimeAsync(3000);
    expect(events).toHaveLength(1);
  });

  it('does not echo its own writes and removals to watchers', async () => {
    const clock = makeClock(1000);
    const root = createFakeRoot('v', { now: clock.now });
    const fs = new FsAccessFs(root, { pollInterval: 60_000 });
    const events: FsEvent[] = [];
    const stop = fs.watch((e) => events.push(...e));
    await fs.checkNow();
    clock.advance(5);
    await fs.writeText('own.md', 'mine');
    await fs.writeText('own2.md', 'mine');
    await fs.remove('own2.md');
    await fs.checkNow();
    expect(events).toEqual([]);
    clock.advance(5);
    await fakeWrite(root, 'own.md', 'theirs');
    await fs.checkNow();
    expect(events).toEqual([{ type: 'modify', path: 'own.md' }]);
    stop();
  });

  it('a failing listing during polling only warns once', async () => {
    const warn = vi.spyOn(console, 'warn').mockImplementation(() => {});
    const root = createFakeRoot();
    const fs = new FsAccessFs(root, { pollInterval: 60_000 });
    const stop = fs.watch(() => {});
    await fs.checkNow();
    root.entries = () => {
      throw new DOMException('gone', 'NotAllowedError');
    };
    await fs.checkNow();
    await fs.checkNow();
    expect(warn).toHaveBeenCalledTimes(1);
    stop();
    warn.mockRestore();
  });

  it('stat reports files and directories, null for missing', async () => {
    const clock = makeClock(7777);
    const root = createFakeRoot('v', { now: clock.now });
    const fs = new FsAccessFs(root);
    await fs.writeText('d/f.md', 'abc');
    expect(await fs.stat('d/f.md')).toEqual({ path: 'd/f.md', kind: 'file', size: 3, mtime: 7777 });
    expect(await fs.stat('d')).toEqual({ path: 'd', kind: 'dir', size: 0, mtime: 0 });
    expect(await fs.stat('d/f.md/x')).toBeNull();
    expect(await fs.stat('zz/f.md')).toBeNull();
  });
});

describe('FsAccessFs case-only renames', () => {
  const variants = [
    ['case-insensitive, files movable', { caseInsensitive: true, move: 'files' }],
    ['case-insensitive, no move()', { caseInsensitive: true, move: 'none' }],
    ['case-insensitive, move() everywhere', { caseInsensitive: true, move: 'all' }],
    ['case-sensitive', { caseInsensitive: false, move: 'files' }],
  ] as const;

  for (const [label, opts] of variants) {
    it(`renames a file by case only without losing it (${label})`, async () => {
      const root = createFakeRoot('v', opts);
      const fs = new FsAccessFs(root);
      await fs.writeText('Notatka o żółwiu.md', 'treść żółwia');
      await fs.rename('Notatka o żółwiu.md', 'notatka o Żółwiu.md');
      const files = (await fs.list()).filter((s) => s.kind === 'file').map((s) => s.path);
      expect(files).toEqual(['notatka o Żółwiu.md']);
      expect(await fs.readText('notatka o Żółwiu.md')).toBe('treść żółwia');
    });

    it(`renames a folder by case only (${label})`, async () => {
      const root = createFakeRoot('v', opts);
      const fs = new FsAccessFs(root);
      await fs.writeText('Projekty/Dom.md', 'dom');
      await fs.writeText('Projekty/Praca/Plan.md', 'plan');
      await fs.rename('Projekty', 'projekty');
      const paths = (await fs.list()).map((s) => s.path);
      expect(paths).toEqual(['projekty', 'projekty/Dom.md', 'projekty/Praca', 'projekty/Praca/Plan.md']);
      expect(await fs.readText('projekty/Praca/Plan.md')).toBe('plan');
    });
  }

  it('case-sensitive folder: a case-only rename onto a distinct file overwrites it like any rename', async () => {
    const fs = new FsAccessFs(createFakeRoot('v', { caseInsensitive: false }));
    await fs.writeText('A.md', 'duże');
    await fs.writeText('a.md', 'małe');
    await fs.rename('A.md', 'a.md');
    expect((await fs.list()).map((s) => s.path)).toEqual(['a.md']);
    expect(await fs.readText('a.md')).toBe('duże');
  });

  it('case-sensitive folder: a failed case-only rename onto a distinct folder leaves the source in place', async () => {
    const fs = new FsAccessFs(createFakeRoot('v', { caseInsensitive: false }));
    await fs.writeText('A.md', 'plik');
    await fs.writeText('a.md/inside.md', 'folder');
    await expect(fs.rename('A.md', 'a.md')).rejects.toBeTruthy();
    expect((await fs.list()).map((s) => s.path)).toEqual(['A.md', 'a.md', 'a.md/inside.md']);
    expect(await fs.readText('A.md')).toBe('plik');
  });

  it('case-insensitive folder: refuses moving a folder into itself spelled differently', async () => {
    const fs = new FsAccessFs(createFakeRoot('v', { caseInsensitive: true }));
    await fs.writeText('Dir/x.md', 'x');
    await expect(fs.rename('Dir', 'dir/sub')).rejects.toSatisfy((e: unknown) => isFsError(e, 'EINVAL'));
    expect(await fs.readText('Dir/x.md')).toBe('x');
  });

  it('a case-only rename is not echoed to watchers as external changes', async () => {
    const clock = makeClock(1000);
    const fs = new FsAccessFs(createFakeRoot('v', { now: clock.now, caseInsensitive: true }), { pollInterval: 60_000 });
    await fs.writeText('Ala.md', 'a');
    const events: FsEvent[] = [];
    const stop = fs.watch((e) => events.push(...e));
    await fs.checkNow();
    await fs.rename('Ala.md', 'ala.md');
    await fs.checkNow();
    // The rename itself shows up as delete + create (polling cannot tell), but never a lost file.
    expect(events.filter((e) => e.type === 'create').map((e) => e.path)).toEqual(['ala.md']);
    expect(events.every((e) => !e.path.includes('.brulion-tmp-'))).toBe(true);
    stop();
  });
});

describe('verifyPermission', () => {
  it('treats handles without the permission API as granted', async () => {
    expect(await verifyPermission(createFakeRoot())).toBe(true);
  });

  it('returns true when already granted', async () => {
    expect(await verifyPermission(createFakeRoot('x', { permission: 'granted' }))).toBe(true);
  });

  it('does not request unless asked', async () => {
    const root = createFakeRoot('x', { permission: 'prompt', permissionAfterRequest: 'granted' });
    expect(await verifyPermission(root, 'readwrite', false)).toBe(false);
    expect(root.permission).toBe('prompt');
    expect(await verifyPermission(root, 'readwrite', true)).toBe(true);
    expect(root.permission).toBe('granted');
  });

  it('returns false when the user denies', async () => {
    const root = createFakeRoot('x', { permission: 'prompt', permissionAfterRequest: 'denied' });
    expect(await verifyPermission(root, 'read', true)).toBe(false);
  });

  it('returns false (and warns) when the permission API throws', async () => {
    const warn = vi.spyOn(console, 'warn').mockImplementation(() => {});
    const root = createFakeRoot('x', { permission: 'prompt' });
    root.queryPermission = async () => {
      throw new Error('SecurityError');
    };
    expect(await verifyPermission(root)).toBe(false);
    expect(warn).toHaveBeenCalled();
    warn.mockRestore();
  });
});
